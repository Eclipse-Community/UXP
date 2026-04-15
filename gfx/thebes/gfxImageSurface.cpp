/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */


#include "mozilla/MemoryReporting.h"
#if defined(HAVE_POSIX_MEMALIGN)
#include "gfxAlphaRecovery.h"
#endif
#include "gfxImageSurface.h"

#include "cairo.h"
#include "mozilla/gfx/2D.h"
#include "mozilla/gfx/HelpersCairo.h"
#include "gfx2DGlue.h"
#include <algorithm>

// SSE2 optimization support
#ifdef MOZILLA_MAY_SUPPORT_SSE2
#include <emmintrin.h>
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <xmmintrin.h>
#endif
#endif

using namespace mozilla;
using namespace mozilla::gfx;

gfxImageSurface::gfxImageSurface()
  : mSize(0, 0),
    mOwnsData(false),
    mFormat(SurfaceFormat::UNKNOWN),
    mStride(0)
{
}

void
gfxImageSurface::InitFromSurface(cairo_surface_t *csurf)
{
    if (!csurf || cairo_surface_status(csurf)) {
        MakeInvalid();
        return;
    }

    mSize.width = cairo_image_surface_get_width(csurf);
    mSize.height = cairo_image_surface_get_height(csurf);
    mData = cairo_image_surface_get_data(csurf);
    mFormat = CairoFormatToGfxFormat(cairo_image_surface_get_format(csurf));
    mOwnsData = false;
    mStride = cairo_image_surface_get_stride(csurf);

    Init(csurf, true);
}

gfxImageSurface::gfxImageSurface(unsigned char *aData, const IntSize& aSize,
                                 long aStride, gfxImageFormat aFormat)
{
    InitWithData(aData, aSize, aStride, aFormat);
}

void
gfxImageSurface::MakeInvalid()
{
    mSize = IntSize(-1, -1);
    mData = nullptr;
    mStride = 0;
}

void
gfxImageSurface::InitWithData(unsigned char *aData, const IntSize& aSize,
                              long aStride, gfxImageFormat aFormat)
{
    mSize = aSize;
    mOwnsData = false;
    mData = aData;
    mFormat = aFormat;
    mStride = aStride;

    if (!Factory::CheckSurfaceSize(aSize))
        MakeInvalid();

    cairo_format_t cformat = GfxFormatToCairoFormat(mFormat);
    cairo_surface_t *surface =
        cairo_image_surface_create_for_data((unsigned char*)mData,
                                            cformat,
                                            mSize.width,
                                            mSize.height,
                                            mStride);

    // cairo_image_surface_create_for_data can return a 'null' surface
    // in out of memory conditions. The gfxASurface::Init call checks
    // the surface it receives to see if there is an error with the
    // surface and handles it appropriately. That is why there is
    // no check here.
    Init(surface);
}

static void*
TryAllocAlignedBytes(size_t aSize)
{
    // Use fallible allocators here
#if defined(HAVE_POSIX_MEMALIGN)
    void* ptr;
    // Try to align for fast alpha recovery.  This should only help
    // cairo too, can't hurt.
    return moz_posix_memalign(&ptr,
                              1 << gfxAlphaRecovery::GoodAlignmentLog2(),
                              aSize) ?
             nullptr : ptr;
#else
    // Oh well, hope that luck is with us in the allocator
    return malloc(aSize);
#endif
}

gfxImageSurface::gfxImageSurface(const IntSize& size, gfxImageFormat format, bool aClear)
 : mSize(size), mData(nullptr), mFormat(format)
{
    AllocateAndInit(0, 0, aClear);
}

// SSE2-optimized memset for large aligned buffers
#ifdef MOZILLA_MAY_SUPPORT_SSE2
static inline void
MemsetSSE2(unsigned char* aData, int aValue, size_t aSize)
{
    if (aSize < 128 || !mozilla::supports_sse2()) {
        memset(aData, aValue, aSize);
        return;
    }

    unsigned char* ptr = aData;
    
    // Align to 16-byte boundary
    size_t alignedStart = 16 - (NS_PTR_TO_UINT32(ptr) & 0xf);
    if (alignedStart < 16) {
        memset(ptr, aValue, alignedStart);
        ptr += alignedStart;
        aSize -= alignedStart;
    }

    // Fill with SSE2 (16 bytes at a time)
    if (aValue == 0) {
        __m128i zero = _mm_setzero_si128();
        size_t sse2Bytes = (aSize / 16) * 16;
        for (size_t i = 0; i < sse2Bytes; i += 16) {
            _mm_stream_si128((__m128i*)(ptr + i), zero);
        }
        ptr += sse2Bytes;
        aSize -= sse2Bytes;
    } else {
        // For non-zero values, replicate to fill 16 bytes
        uint32_t pattern = aValue | (aValue << 8) | (aValue << 16) | (aValue << 24);
        __m128i fillValue = _mm_set_epi32(pattern, pattern, pattern, pattern);
        size_t sse2Bytes = (aSize / 16) * 16;
        for (size_t i = 0; i < sse2Bytes; i += 16) {
            _mm_stream_si128((__m128i*)(ptr + i), fillValue);
        }
        ptr += sse2Bytes;
        aSize -= sse2Bytes;
    }

    // Handle remaining bytes
    if (aSize > 0) {
        memset(ptr, aValue, aSize);
    }
}
#endif // MOZILLA_MAY_SUPPORT_SSE2

void 
gfxImageSurface::AllocateAndInit(long aStride, int32_t aMinimalAllocation,
                                 bool aClear)
{
    // The callers should set mSize and mFormat.
    MOZ_ASSERT(!mData);
    mData = nullptr;
    mOwnsData = false;

    mStride = aStride > 0 ? aStride : ComputeStride();
    if (aMinimalAllocation < mSize.height * mStride)
        aMinimalAllocation = mSize.height * mStride;

    if (!Factory::CheckSurfaceSize(mSize))
        MakeInvalid();

    // if we have a zero-sized surface, just leave mData nullptr
    if (mSize.height * mStride > 0) {

        // This can fail to allocate memory aligned as we requested,
        // or it can fail to allocate any memory at all.
        mData = (unsigned char *) TryAllocAlignedBytes(aMinimalAllocation);
        if (!mData)
            return;
        if (aClear) {
#ifdef MOZILLA_MAY_SUPPORT_SSE2
            MemsetSSE2(mData, 0, aMinimalAllocation);
#else
            memset(mData, 0, aMinimalAllocation);
#endif
        }
    }

    mOwnsData = true;

    cairo_format_t cformat = GfxFormatToCairoFormat(mFormat);
    cairo_surface_t *surface =
        cairo_image_surface_create_for_data((unsigned char*)mData,
                                            cformat,
                                            mSize.width,
                                            mSize.height,
                                            mStride);

    Init(surface);

    if (mSurfaceValid) {
        RecordMemoryUsed(mSize.height * ComputeStride() +
                         sizeof(gfxImageSurface));
    }
}

gfxImageSurface::gfxImageSurface(const IntSize& size, gfxImageFormat format,
                                 long aStride, int32_t aExtraBytes, bool aClear)
 : mSize(size), mData(nullptr), mFormat(format)
{
    AllocateAndInit(aStride, aExtraBytes, aClear);
}

gfxImageSurface::gfxImageSurface(cairo_surface_t *csurf)
{
    mSize.width = cairo_image_surface_get_width(csurf);
    mSize.height = cairo_image_surface_get_height(csurf);
    mData = cairo_image_surface_get_data(csurf);
    mFormat = CairoFormatToGfxFormat(cairo_image_surface_get_format(csurf));
    mOwnsData = false;
    mStride = cairo_image_surface_get_stride(csurf);

    Init(csurf, true);
}

gfxImageSurface::~gfxImageSurface()
{
    if (mOwnsData)
        free(mData);
}

/*static*/ long
gfxImageSurface::ComputeStride(const IntSize& aSize, gfxImageFormat aFormat)
{
    long stride;

    if (aFormat == SurfaceFormat::A8R8G8B8_UINT32)
        stride = aSize.width * 4;
    else if (aFormat == SurfaceFormat::X8R8G8B8_UINT32)
        stride = aSize.width * 4;
    else if (aFormat == SurfaceFormat::R5G6B5_UINT16)
        stride = aSize.width * 2;
    else if (aFormat == SurfaceFormat::A8)
        stride = aSize.width;
    else {
        NS_WARNING("Unknown format specified to gfxImageSurface!");
        stride = aSize.width * 4;
    }

    stride = ((stride + 3) / 4) * 4;

    return stride;
}

size_t
gfxImageSurface::SizeOfExcludingThis(mozilla::MallocSizeOf aMallocSizeOf) const
{
    size_t n = gfxASurface::SizeOfExcludingThis(aMallocSizeOf);
    if (mOwnsData) {
        n += aMallocSizeOf(mData);
    }
    return n;
}

size_t
gfxImageSurface::SizeOfIncludingThis(mozilla::MallocSizeOf aMallocSizeOf) const
{
    return aMallocSizeOf(this) + SizeOfExcludingThis(aMallocSizeOf);
}

bool
gfxImageSurface::SizeOfIsMeasured() const
{
    return true;
}

// SSE2-optimized memory copy for aligned large buffers
#ifdef MOZILLA_MAY_SUPPORT_SSE2
static inline void
CopyForStrideSSE2(unsigned char* aDest, unsigned char* aSrc, const IntSize& aSize, long aDestStride, long aSrcStride)
{
    if (aDestStride == aSrcStride && mozilla::supports_sse2()) {
        size_t totalBytes = static_cast<size_t>(aSrcStride) * aSize.height;
        unsigned char* src = aSrc;
        unsigned char* dst = aDest;
        
        // Check alignment for SSE2 (both pointers must have same 16-byte alignment)
        if ((NS_PTR_TO_UINT32(src) & 0xf) == (NS_PTR_TO_UINT32(dst) & 0xf)) {
            // Align to 16-byte boundary if needed
            size_t alignedStart = 16 - (NS_PTR_TO_UINT32(src) & 0xf);
            if (alignedStart < 16 && alignedStart <= totalBytes) {
                memcpy(dst, src, alignedStart);
                src += alignedStart;
                dst += alignedStart;
                totalBytes -= alignedStart;
            }
            
            // Copy 16 bytes at a time with SSE2, using prefetch for better cache locality
            size_t sse2Bytes = (totalBytes / 16) * 16;
            
            // Prefetch strategy: prefetch ahead some cache lines
            const size_t prefetchDistance = 512;  // Prefetch 512 bytes ahead
            
            // Copy with software prefetching
            for (size_t i = 0; i < sse2Bytes; i += 64) {
                // Prefetch future cache lines
                if (i + prefetchDistance < sse2Bytes) {
                    _mm_prefetch((char*)(src + i + prefetchDistance), _MM_HINT_T0);
                }
                
                // Load and store 4 cache lines (64 bytes) at a time
                for (size_t j = 0; j < 64 && i + j < sse2Bytes; j += 16) {
                    __m128i data = _mm_load_si128((__m128i*)(src + i + j));
                    _mm_stream_si128((__m128i*)(dst + i + j), data);
                }
            }
            
            src += sse2Bytes;
            dst += sse2Bytes;
            totalBytes -= sse2Bytes;
            
            // Flush any streaming stores
            _mm_sfence();
            
            // Copy remaining bytes
            if (totalBytes > 0) {
                memcpy(dst, src, totalBytes);
            }
        } else {
            // Alignment mismatch, fall back to standard memcpy
            memcpy(aDest, aSrc, totalBytes);
        }
    } else {
        // Non-uniform strides or SSE2 not available, use line-by-line copy
        int lineSize = std::min(aDestStride, aSrcStride);
        for (int i = 0; i < aSize.height; i++) {
            unsigned char* src = aSrc + aSrcStride * i;
            unsigned char* dst = aDest + aDestStride * i;
            memcpy(dst, src, lineSize);
        }
    }
}
#endif // MOZILLA_MAY_SUPPORT_SSE2

// helper function for the CopyFrom methods
static void
CopyForStride(unsigned char* aDest, unsigned char* aSrc, const IntSize& aSize, long aDestStride, long aSrcStride)
{
#ifdef MOZILLA_MAY_SUPPORT_SSE2
    CopyForStrideSSE2(aDest, aSrc, aSize, aDestStride, aSrcStride);
#else
    if (aDestStride == aSrcStride) {
        memcpy (aDest, aSrc, aSrcStride * aSize.height);
    } else {
        int lineSize = std::min(aDestStride, aSrcStride);
        for (int i = 0; i < aSize.height; i++) {
            unsigned char* src = aSrc + aSrcStride * i;
            unsigned char* dst = aDest + aDestStride * i;
            memcpy (dst, src, lineSize);
        }
    }
#endif
}

// helper function for the CopyFrom methods
static bool
FormatsAreCompatible(gfxImageFormat a1, gfxImageFormat a2)
{
    if (a1 != a2 &&
        !(a1 == SurfaceFormat::A8R8G8B8_UINT32 &&
          a2 == SurfaceFormat::X8R8G8B8_UINT32) &&
        !(a1 == SurfaceFormat::X8R8G8B8_UINT32 &&
          a2 == SurfaceFormat::A8R8G8B8_UINT32)) {
        return false;
    }

    return true;
}

bool
gfxImageSurface::CopyFrom (SourceSurface *aSurface)
{
    RefPtr<DataSourceSurface> data = aSurface->GetDataSurface();

    if (!data) {
        return false;
    }

    IntSize size(data->GetSize().width, data->GetSize().height);
    if (size != mSize) {
        return false;
    }

    if (!FormatsAreCompatible(SurfaceFormatToImageFormat(aSurface->GetFormat()),
                              mFormat)) {
        return false;
    }

    CopyForStride(mData, data->GetData(), size, mStride, data->Stride());

    return true;
}


bool
gfxImageSurface::CopyFrom(gfxImageSurface *other)
{
    if (other->mSize != mSize) {
        return false;
    }

    if (!FormatsAreCompatible(other->mFormat, mFormat)) {
        return false;
    }

    CopyForStride(mData, other->mData, mSize, mStride, other->mStride);

    return true;
}

bool
gfxImageSurface::CopyTo(SourceSurface *aSurface) {
    RefPtr<DataSourceSurface> data = aSurface->GetDataSurface();

    if (!data) {
        return false;
    }

    IntSize size(data->GetSize().width, data->GetSize().height);
    if (size != mSize) {
        return false;
    }

    if (!FormatsAreCompatible(SurfaceFormatToImageFormat(aSurface->GetFormat()),
                              mFormat)) {
        return false;
    }

    CopyForStride(data->GetData(), mData, size, data->Stride(), mStride);

    return true;
}

already_AddRefed<DataSourceSurface>
gfxImageSurface::CopyToB8G8R8A8DataSourceSurface()
{
  RefPtr<DataSourceSurface> dataSurface =
    Factory::CreateDataSourceSurface(IntSize(GetSize().width, GetSize().height),
                                     SurfaceFormat::B8G8R8A8);
  if (dataSurface) {
    CopyTo(dataSurface);
  }
  return dataSurface.forget();
}

already_AddRefed<gfxSubimageSurface>
gfxImageSurface::GetSubimage(const gfxRect& aRect)
{
    gfxRect r(aRect);
    r.Round();
    MOZ_ASSERT(gfxRect(0, 0, mSize.width, mSize.height).Contains(r));

    gfxImageFormat format = Format();

    unsigned char* subData = Data() +
        (Stride() * (int)r.Y()) +
        (int)r.X() * gfxASurface::BytePerPixelFromFormat(Format());

    if (format == SurfaceFormat::A8R8G8B8_UINT32 &&
        GetOpaqueRect().Contains(aRect)) {
        format = SurfaceFormat::X8R8G8B8_UINT32;
    }

    RefPtr<gfxSubimageSurface> image =
        new gfxSubimageSurface(this, subData,
                               IntSize((int)r.Width(), (int)r.Height()),
                               format);

    return image.forget();
}

gfxSubimageSurface::gfxSubimageSurface(gfxImageSurface* aParent,
                                       unsigned char* aData,
                                       const IntSize& aSize,
                                       gfxImageFormat aFormat)
  : gfxImageSurface(aData, aSize, aParent->Stride(), aFormat)
  , mParent(aParent)
{
}

already_AddRefed<gfxImageSurface>
gfxImageSurface::GetAsImageSurface()
{
  RefPtr<gfxImageSurface> surface = this;
  return surface.forget();
}
