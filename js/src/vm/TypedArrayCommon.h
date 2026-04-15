/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 4 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef vm_TypedArrayCommon_h
#define vm_TypedArrayCommon_h

/* Utilities and common inline code for TypedArray */

#include "mozilla/Assertions.h"
#include "mozilla/FloatingPoint.h"

#include <algorithm>
#include <cstring>
#include <type_traits>

#if (defined(JS_CODEGEN_X64) || defined(JS_CODEGEN_X86)) && \
    (defined(_M_X64) || defined(__SSE2__) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2))
#  include <emmintrin.h>
#  define JS_TYPEDARRAY_HAS_SSE2 1
#endif

#include "jsarray.h"
#include "jscntxt.h"
#include "jsnum.h"

#include "jit/AtomicOperations.h"

#include "js/Conversions.h"
#include "js/Value.h"

#include "vm/NativeObject.h"
#include "vm/TypedArrayObject.h"

namespace js {

// ValueIsLength happens not to be according to ES6, which mandates
// the use of ToLength, which in turn includes ToNumber, ToInteger,
// and clamping.  ValueIsLength is used in the current TypedArray code
// but will disappear when that code is made spec-compliant.

inline bool
ValueIsLength(const Value& v, uint32_t* len)
{
    if (v.isInt32()) {
        int32_t i = v.toInt32();
        if (i < 0)
            return false;
        *len = i;
        return true;
    }

    if (v.isDouble()) {
        double d = v.toDouble();
        if (mozilla::IsNaN(d))
            return false;

        uint32_t length = uint32_t(d);
        if (d != double(length))
            return false;

        *len = length;
        return true;
    }

    return false;
}

template<typename To, typename From>
inline To
ConvertNumber(From src);

template<>
inline int8_t
ConvertNumber<int8_t, float>(float src)
{
    return JS::ToInt8(src);
}

template<>
inline uint8_t
ConvertNumber<uint8_t, float>(float src)
{
    return JS::ToUint8(src);
}

template<>
inline uint8_clamped
ConvertNumber<uint8_clamped, float>(float src)
{
    return uint8_clamped(src);
}

template<>
inline int16_t
ConvertNumber<int16_t, float>(float src)
{
    return JS::ToInt16(src);
}

template<>
inline uint16_t
ConvertNumber<uint16_t, float>(float src)
{
    return JS::ToUint16(src);
}

template<>
inline int32_t
ConvertNumber<int32_t, float>(float src)
{
    return JS::ToInt32(src);
}

template<>
inline uint32_t
ConvertNumber<uint32_t, float>(float src)
{
    return JS::ToUint32(src);
}

template <>
inline int64_t
ConvertNumber<int64_t, float>(float src)
{
    return JS::ToInt64(src);
}

template <>
inline uint64_t
ConvertNumber<uint64_t, float>(float src) 
{
    return JS::ToUint64(src);
}

template<> inline int8_t
ConvertNumber<int8_t, double>(double src)
{
    return JS::ToInt8(src);
}

template<>
inline uint8_t
ConvertNumber<uint8_t, double>(double src)
{
    return JS::ToUint8(src);
}

template<>
inline uint8_clamped
ConvertNumber<uint8_clamped, double>(double src)
{
    return uint8_clamped(src);
}

template<>
inline int16_t
ConvertNumber<int16_t, double>(double src)
{
    return JS::ToInt16(src);
}

template<>
inline uint16_t
ConvertNumber<uint16_t, double>(double src)
{
    return JS::ToUint16(src);
}

template<>
inline int32_t
ConvertNumber<int32_t, double>(double src)
{
    return JS::ToInt32(src);
}

template<>
inline uint32_t
ConvertNumber<uint32_t, double>(double src)
{
    return JS::ToUint32(src);
}

template <>
inline int64_t
ConvertNumber<int64_t, double>(double src)
{
    return JS::ToInt64(src);
}

template <>
inline uint64_t
ConvertNumber<uint64_t, double>(double src)
{
    return JS::ToUint64(src);
}

template<typename To, typename From>
inline To
ConvertNumber(From src)
{
    static_assert(!mozilla::IsFloatingPoint<From>::value ||
                  (mozilla::IsFloatingPoint<From>::value && mozilla::IsFloatingPoint<To>::value),
                  "conversion from floating point to int should have been handled by "
                  "specializations above");
    return To(src);
}

#ifdef JS_TYPEDARRAY_HAS_SSE2
static inline void
SSE2ConvertFloatToUint8Clamped(uint8_clamped* dest, const float* src, uint32_t count)
{
    const __m128 fzero = _mm_set1_ps(0.0f);
    const __m128 fmax = _mm_set1_ps(255.0f);
    const __m128i izero = _mm_setzero_si128();
    const __m128i i255 = _mm_set1_epi16(255);

    uint32_t i = 0;
    for (; i + 4 <= count; i += 4) {
        __m128 values = _mm_loadu_ps(src + i);

        // Keep exact scalar behavior for NaN lanes.
        if (_mm_movemask_ps(_mm_cmpunord_ps(values, values))) {
            for (uint32_t j = 0; j < 4; ++j)
                dest[i + j] = uint8_clamped(src[i + j]);
            continue;
        }

        values = _mm_min_ps(_mm_max_ps(values, fzero), fmax);
        __m128i ints = _mm_cvtps_epi32(values);
        __m128i packed16 = _mm_packs_epi32(ints, izero);
        packed16 = _mm_min_epi16(_mm_max_epi16(packed16, izero), i255);
        __m128i packed8 = _mm_packus_epi16(packed16, izero);

        uint32_t out = static_cast<uint32_t>(_mm_cvtsi128_si32(packed8));
        ::memcpy(reinterpret_cast<uint8_t*>(dest + i), &out, sizeof(out));
    }

    for (; i < count; ++i)
        dest[i] = uint8_clamped(src[i]);
}

static inline void
SSE2ConvertDoubleToUint8Clamped(uint8_clamped* dest, const double* src, uint32_t count)
{
    const __m128d dzero = _mm_set1_pd(0.0);
    const __m128d dmax = _mm_set1_pd(255.0);
    const __m128i izero = _mm_setzero_si128();
    const __m128i i255 = _mm_set1_epi16(255);

    uint32_t i = 0;
    for (; i + 2 <= count; i += 2) {
        __m128d values = _mm_loadu_pd(src + i);

        // Keep exact scalar behavior for NaN lanes.
        if (_mm_movemask_pd(_mm_cmpunord_pd(values, values))) {
            for (uint32_t j = 0; j < 2; ++j)
                dest[i + j] = uint8_clamped(src[i + j]);
            continue;
        }

        values = _mm_min_pd(_mm_max_pd(values, dzero), dmax);
        __m128i ints = _mm_cvtpd_epi32(values);
        __m128i packed16 = _mm_packs_epi32(ints, izero);
        packed16 = _mm_min_epi16(_mm_max_epi16(packed16, izero), i255);
        __m128i packed8 = _mm_packus_epi16(packed16, izero);

        uint16_t out = static_cast<uint16_t>(static_cast<uint32_t>(_mm_cvtsi128_si32(packed8)) & 0xFFFFu);
        ::memcpy(reinterpret_cast<uint8_t*>(dest + i), &out, sizeof(out));
    }

    for (; i < count; ++i)
        dest[i] = uint8_clamped(src[i]);
}

static inline void
SSE2ConvertInt8ToUint8Clamped(uint8_clamped* dest, const int8_t* src, uint32_t count)
{
    const __m128i izero = _mm_setzero_si128();
    uint8_t* out = reinterpret_cast<uint8_t*>(dest);

    uint32_t i = 0;
    for (; i + 16 <= count; i += 16) {
        __m128i values = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i));
        __m128i negatives = _mm_cmpgt_epi8(izero, values);
        __m128i clamped = _mm_andnot_si128(negatives, values);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(out + i), clamped);
    }

    for (; i < count; ++i)
        dest[i] = uint8_clamped(src[i]);
}

static inline void
SSE2ConvertInt16ToUint8Clamped(uint8_clamped* dest, const int16_t* src, uint32_t count)
{
    uint8_t* out = reinterpret_cast<uint8_t*>(dest);

    uint32_t i = 0;
    for (; i + 16 <= count; i += 16) {
        __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i));
        __m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i + 8));
        __m128i packed = _mm_packus_epi16(a, b);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(out + i), packed);
    }

    for (; i < count; ++i)
        dest[i] = uint8_clamped(src[i]);
}

static inline void
SSE2ConvertUint16ToUint8Clamped(uint8_clamped* dest, const uint16_t* src, uint32_t count)
{
    const __m128i i255 = _mm_set1_epi16(255);
    uint8_t* out = reinterpret_cast<uint8_t*>(dest);

    uint32_t i = 0;
    for (; i + 16 <= count; i += 16) {
        __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i));
        __m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i + 8));

        __m128i aExcess = _mm_subs_epu16(a, i255);
        __m128i bExcess = _mm_subs_epu16(b, i255);
        __m128i aClamped = _mm_sub_epi16(a, aExcess);
        __m128i bClamped = _mm_sub_epi16(b, bExcess);

        __m128i packed = _mm_packus_epi16(aClamped, bClamped);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(out + i), packed);
    }

    for (; i < count; ++i)
        dest[i] = uint8_clamped(src[i]);
}

static inline void
SSE2ConvertInt32ToUint8Clamped(uint8_clamped* dest, const int32_t* src, uint32_t count)
{
    const __m128i izero = _mm_setzero_si128();
    const __m128i i255 = _mm_set1_epi16(255);
    uint8_t* out = reinterpret_cast<uint8_t*>(dest);

    uint32_t i = 0;
    for (; i + 8 <= count; i += 8) {
        __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i));
        __m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i + 4));
        __m128i words = _mm_packs_epi32(a, b);
        words = _mm_min_epi16(_mm_max_epi16(words, izero), i255);
        __m128i bytes = _mm_packus_epi16(words, izero);
        _mm_storel_epi64(reinterpret_cast<__m128i*>(out + i), bytes);
    }

    for (; i < count; ++i)
        dest[i] = uint8_clamped(src[i]);
}

static inline void
SSE2ConvertUint32ToUint8Clamped(uint8_clamped* dest, const uint32_t* src, uint32_t count)
{
    const __m128i izero = _mm_setzero_si128();
    const __m128i maskHigh = _mm_set1_epi32(0xFFFFFF00u);
    const __m128i maskLow = _mm_set1_epi32(0x000000FFu);
    const __m128i i255d = _mm_set1_epi32(255);
    uint8_t* out = reinterpret_cast<uint8_t*>(dest);

    uint32_t i = 0;
    for (; i + 8 <= count; i += 8) {
        __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i));
        __m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i + 4));

        __m128i aFits = _mm_cmpeq_epi32(_mm_and_si128(a, maskHigh), izero);
        __m128i bFits = _mm_cmpeq_epi32(_mm_and_si128(b, maskHigh), izero);

        __m128i aLow = _mm_and_si128(a, maskLow);
        __m128i bLow = _mm_and_si128(b, maskLow);

        __m128i aClamped = _mm_or_si128(_mm_and_si128(aFits, aLow), _mm_andnot_si128(aFits, i255d));
        __m128i bClamped = _mm_or_si128(_mm_and_si128(bFits, bLow), _mm_andnot_si128(bFits, i255d));

        __m128i words = _mm_packs_epi32(aClamped, bClamped);
        __m128i bytes = _mm_packus_epi16(words, izero);
        _mm_storel_epi64(reinterpret_cast<__m128i*>(out + i), bytes);
    }

    for (; i < count; ++i)
        dest[i] = uint8_clamped(src[i]);
}
#endif

template<typename NativeType> struct TypeIDOfType;
template<> struct TypeIDOfType<int8_t> { static const Scalar::Type id = Scalar::Int8; };
template<> struct TypeIDOfType<uint8_t> { static const Scalar::Type id = Scalar::Uint8; };
template<> struct TypeIDOfType<int16_t> { static const Scalar::Type id = Scalar::Int16; };
template<> struct TypeIDOfType<uint16_t> { static const Scalar::Type id = Scalar::Uint16; };
template<> struct TypeIDOfType<int32_t> { static const Scalar::Type id = Scalar::Int32; };
template<> struct TypeIDOfType<uint32_t> { static const Scalar::Type id = Scalar::Uint32; };
template<> struct TypeIDOfType<int64_t> { static const Scalar::Type id = Scalar::BigInt64; };
template<> struct TypeIDOfType<uint64_t> { static const Scalar::Type id = Scalar::BigUint64; };
template<> struct TypeIDOfType<float> { static const Scalar::Type id = Scalar::Float32; };
template<> struct TypeIDOfType<double> { static const Scalar::Type id = Scalar::Float64; };
template<> struct TypeIDOfType<uint8_clamped> { static const Scalar::Type id = Scalar::Uint8Clamped; };

class SharedOps
{
  public:
    template<typename T>
    static T load(SharedMem<T*> addr) {
        return js::jit::AtomicOperations::loadSafeWhenRacy(addr);
    }

    template<typename T>
    static void store(SharedMem<T*> addr, T value) {
        js::jit::AtomicOperations::storeSafeWhenRacy(addr, value);
    }

    template<typename T>
    static void memcpy(SharedMem<T*> dest, SharedMem<T*> src, size_t size) {
        js::jit::AtomicOperations::memcpySafeWhenRacy(dest, src, size);
    }

    template<typename T>
    static void memmove(SharedMem<T*> dest, SharedMem<T*> src, size_t size) {
        js::jit::AtomicOperations::memmoveSafeWhenRacy(dest, src, size);
    }

    template<typename T>
    static void podCopy(SharedMem<T*> dest, SharedMem<T*> src, size_t nelem) {
        js::jit::AtomicOperations::podCopySafeWhenRacy(dest, src, nelem);
    }

    template<typename T>
    static void podMove(SharedMem<T*> dest, SharedMem<T*> src, size_t nelem) {
        js::jit::AtomicOperations::podMoveSafeWhenRacy(dest, src, nelem);
    }

    static SharedMem<void*> extract(TypedArrayObject* obj) {
        return obj->viewDataEither();
    }
};

class UnsharedOps
{
  public:
    template<typename T>
    static T load(SharedMem<T*> addr) {
        return *addr.unwrapUnshared();
    }

    template<typename T>
    static void store(SharedMem<T*> addr, T value) {
        *addr.unwrapUnshared() = value;
    }

    template<typename T>
    static void memcpy(SharedMem<T*> dest, SharedMem<T*> src, size_t size) {
        ::memcpy(dest.unwrapUnshared(), src.unwrapUnshared(), size);
    }

    template<typename T>
    static void memmove(SharedMem<T*> dest, SharedMem<T*> src, size_t size) {
        ::memmove(dest.unwrapUnshared(), src.unwrapUnshared(), size);
    }

    template<typename T>
    static void podCopy(SharedMem<T*> dest, SharedMem<T*> src, size_t nelem) {
        static_assert(std::is_trivially_copyable<T>::value,
                      "podCopy requires trivially copyable element type");
        if (nelem == 0)
            return;

        // Keep this on memcpy so platform CRT implementations can use their
        // best vectorized copy routines (SSE2/AVX/etc.) where available.
        ::memcpy(dest.unwrapUnshared(), src.unwrapUnshared(), nelem * sizeof(T));
    }

    template<typename T>
    static void podMove(SharedMem<T*> dest, SharedMem<T*> src, size_t n) {
        static_assert(std::is_trivially_copyable<T>::value,
                      "podMove requires trivially copyable element type");
        if (n == 0)
            return;

        // memmove handles overlap and still maps to optimized runtime copies.
        ::memmove(dest.unwrapUnshared(), src.unwrapUnshared(), n * sizeof(T));
    }

    static SharedMem<void*> extract(TypedArrayObject* obj) {
        return SharedMem<void*>::unshared(obj->viewDataUnshared());
    }
};

template<class SpecificArray, typename Ops>
class ElementSpecific
{
    typedef typename SpecificArray::ElementType T;
    typedef typename SpecificArray::SomeTypedArray SomeTypedArray;

  public:
    /*
     * Copy |source|'s elements into |target|, starting at |target[offset]|.
     * Act as if the assignments occurred from a fresh copy of |source|, in
     * case the two memory ranges overlap.
     */
    static bool
    setFromTypedArray(JSContext* cx,
                      Handle<SomeTypedArray*> target, HandleObject source,
                      uint32_t offset)
    {
        MOZ_ASSERT(SpecificArray::ArrayTypeID() == target->type(),
                   "calling wrong setFromTypedArray specialization");

        MOZ_ASSERT(offset <= target->length());
        MOZ_ASSERT(source->as<TypedArrayObject>().length() <= target->length() - offset);

        if (source->is<SomeTypedArray>()) {
            Rooted<SomeTypedArray*> src(cx, source.as<SomeTypedArray>());
            if (SomeTypedArray::sameBuffer(target, src))
                return setFromOverlappingTypedArray(cx, target, src, offset);
        }

        SharedMem<T*> dest =
            target->template as<TypedArrayObject>().viewDataEither().template cast<T*>() + offset;
        uint32_t count = source->as<TypedArrayObject>().length();

        if (source->as<TypedArrayObject>().type() == target->type()) {
            Ops::podCopy(dest, source->as<TypedArrayObject>().viewDataEither().template cast<T*>(),
                         count);
            return true;
        }

        // Inhibit unaligned accesses on ARM (bug 1097253, a compiler bug).
#ifdef __arm__
#  define JS_VOLATILE_ARM volatile
#else
#  define JS_VOLATILE_ARM
#endif

        SharedMem<void*> data = Ops::extract(source.as<TypedArrayObject>());
        switch (source->as<TypedArrayObject>().type()) {
          case Scalar::Int8: {
            SharedMem<JS_VOLATILE_ARM int8_t*> src = data.cast<JS_VOLATILE_ARM int8_t*>();
#ifdef JS_TYPEDARRAY_HAS_SSE2
                        if (std::is_same<T, uint8_clamped>::value && std::is_same<Ops, UnsharedOps>::value) {
                                SSE2ConvertInt8ToUint8Clamped(reinterpret_cast<uint8_clamped*>(dest.unwrapUnshared()),
                                                                                            data.cast<int8_t*>().unwrapUnshared(),
                                                                                            count);
                                break;
                        }
#endif
            for (uint32_t i = 0; i < count; ++i)
                Ops::store(dest++, ConvertNumber<T>(Ops::load(src++)));
            break;
          }
          case Scalar::Uint8:
          case Scalar::Uint8Clamped: {
            SharedMem<JS_VOLATILE_ARM uint8_t*> src = data.cast<JS_VOLATILE_ARM uint8_t*>();
                        if (std::is_same<T, uint8_clamped>::value && std::is_same<Ops, UnsharedOps>::value) {
                                Ops::podCopy(dest, data.cast<T*>(), count);
                                break;
                        }
            for (uint32_t i = 0; i < count; ++i)
                Ops::store(dest++, ConvertNumber<T>(Ops::load(src++)));
            break;
          }
          case Scalar::Int16: {
            SharedMem<JS_VOLATILE_ARM int16_t*> src = data.cast<JS_VOLATILE_ARM int16_t*>();
#ifdef JS_TYPEDARRAY_HAS_SSE2
                        if (std::is_same<T, uint8_clamped>::value && std::is_same<Ops, UnsharedOps>::value) {
                                SSE2ConvertInt16ToUint8Clamped(reinterpret_cast<uint8_clamped*>(dest.unwrapUnshared()),
                                                                                             data.cast<int16_t*>().unwrapUnshared(),
                                                                                             count);
                                break;
                        }
#endif
            for (uint32_t i = 0; i < count; ++i)
                Ops::store(dest++, ConvertNumber<T>(Ops::load(src++)));
            break;
          }
          case Scalar::Uint16: {
            SharedMem<JS_VOLATILE_ARM uint16_t*> src = data.cast<JS_VOLATILE_ARM uint16_t*>();
#ifdef JS_TYPEDARRAY_HAS_SSE2
                        if (std::is_same<T, uint8_clamped>::value && std::is_same<Ops, UnsharedOps>::value) {
                                SSE2ConvertUint16ToUint8Clamped(reinterpret_cast<uint8_clamped*>(dest.unwrapUnshared()),
                                                                                                data.cast<uint16_t*>().unwrapUnshared(),
                                                                                                count);
                                break;
                        }
#endif
            for (uint32_t i = 0; i < count; ++i)
                Ops::store(dest++, ConvertNumber<T>(Ops::load(src++)));
            break;
          }
          case Scalar::Int32: {
            SharedMem<JS_VOLATILE_ARM int32_t*> src = data.cast<JS_VOLATILE_ARM int32_t*>();
#ifdef JS_TYPEDARRAY_HAS_SSE2
                        if (std::is_same<T, uint8_clamped>::value && std::is_same<Ops, UnsharedOps>::value) {
                                SSE2ConvertInt32ToUint8Clamped(reinterpret_cast<uint8_clamped*>(dest.unwrapUnshared()),
                                                                                             data.cast<int32_t*>().unwrapUnshared(),
                                                                                             count);
                                break;
                        }
#endif
            for (uint32_t i = 0; i < count; ++i)
                Ops::store(dest++, ConvertNumber<T>(Ops::load(src++)));
            break;
          }
          case Scalar::Uint32: {
            SharedMem<JS_VOLATILE_ARM uint32_t*> src = data.cast<JS_VOLATILE_ARM uint32_t*>();
#ifdef JS_TYPEDARRAY_HAS_SSE2
                        if (std::is_same<T, uint8_clamped>::value && std::is_same<Ops, UnsharedOps>::value) {
                                SSE2ConvertUint32ToUint8Clamped(reinterpret_cast<uint8_clamped*>(dest.unwrapUnshared()),
                                                                                                data.cast<uint32_t*>().unwrapUnshared(),
                                                                                                count);
                                break;
                        }
#endif
            for (uint32_t i = 0; i < count; ++i)
                Ops::store(dest++, ConvertNumber<T>(Ops::load(src++)));
            break;
          }
          case Scalar::BigInt64: {
            SharedMem<int64_t*> src = data.cast<int64_t*>();
            for (uint32_t i = 0; i < count; ++i)
                Ops::store(dest++, ConvertNumber<T>(Ops::load(src++)));
            break;
          }
          case Scalar::BigUint64: {
            SharedMem<uint64_t*> src = data.cast<uint64_t*>();
            for (uint32_t i = 0; i < count; ++i)
                Ops::store(dest++, ConvertNumber<T>(Ops::load(src++)));
            break;
          }
          case Scalar::Float32: {
            SharedMem<JS_VOLATILE_ARM float*> src = data.cast<JS_VOLATILE_ARM float*>();
#ifdef JS_TYPEDARRAY_HAS_SSE2
                        if (std::is_same<T, uint8_clamped>::value && std::is_same<Ops, UnsharedOps>::value) {
                                SSE2ConvertFloatToUint8Clamped(reinterpret_cast<uint8_clamped*>(dest.unwrapUnshared()),
                                                                                             data.cast<float*>().unwrapUnshared(),
                                                                                             count);
                                break;
                        }
#endif
            for (uint32_t i = 0; i < count; ++i)
                Ops::store(dest++, ConvertNumber<T>(Ops::load(src++)));
            break;
          }
          case Scalar::Float64: {
            SharedMem<JS_VOLATILE_ARM double*> src = data.cast<JS_VOLATILE_ARM double*>();
#ifdef JS_TYPEDARRAY_HAS_SSE2
                        if (std::is_same<T, uint8_clamped>::value && std::is_same<Ops, UnsharedOps>::value) {
                                SSE2ConvertDoubleToUint8Clamped(reinterpret_cast<uint8_clamped*>(dest.unwrapUnshared()),
                                                                                                data.cast<double*>().unwrapUnshared(),
                                                                                                count);
                                break;
                        }
#endif
            for (uint32_t i = 0; i < count; ++i)
                Ops::store(dest++, ConvertNumber<T>(Ops::load(src++)));
            break;
          }
          default:
            MOZ_CRASH("setFromTypedArray with a typed array with bogus type");
        }

#undef JS_VOLATILE_ARM

        return true;
    }

    /*
     * Copy |source[0]| to |source[len]| (exclusive) elements into the typed
     * array |target|, starting at index |offset|.  |source| must not be a
     * typed array.
     */
    static bool
    setFromNonTypedArray(JSContext* cx, Handle<SomeTypedArray*> target, HandleObject source,
                         uint32_t len, uint32_t offset = 0)
    {
        MOZ_ASSERT(target->type() == SpecificArray::ArrayTypeID(),
                   "target type and NativeType must match");
        MOZ_ASSERT(!source->is<TypedArrayObject>(),
                   "use setFromTypedArray instead of this method");

        uint32_t i = 0;
        if (source->isNative()) {
            // Attempt fast-path infallible conversion of dense elements up to
            // the first potentially side-effectful lookup or conversion.
            uint32_t bound = Min(source->as<NativeObject>().getDenseInitializedLength(), len);

            SharedMem<T*> dest =
                target->template as<TypedArrayObject>().viewDataEither().template cast<T*>() + offset;

            MOZ_ASSERT(!canConvertInfallibly(MagicValue(JS_ELEMENTS_HOLE), target->type()),
                       "the following loop must abort on holes");

            const Value* srcValues = source->as<NativeObject>().getDenseElements();
            for (; i < bound; i++) {
                if (!canConvertInfallibly(srcValues[i], target->type()))
                    break;
                Ops::store(dest + i, infallibleValueToNative(srcValues[i]));
            }
            if (i == len)
                return true;
        }

        // Convert and copy any remaining elements generically.
        RootedValue v(cx);
        for (; i < len; i++) {
            if (!GetElement(cx, source, source, i, &v))
                return false;

            T n;
            if (!valueToNative(cx, v, &n))
                return false;

            len = Min(len, target->length());
            if (i >= len)
                break;

            // Compute every iteration in case getElement/valueToNative is wacky.
            SharedMem<T*> dest =
                target->template as<TypedArrayObject>().viewDataEither().template cast<T*>() +
                offset + i;
            Ops::store(dest, n);
        }

        return true;
    }

    /*
     * Copy |source| into the typed array |target|.
     */
    static bool
    initFromIterablePackedArray(JSContext* cx, Handle<SomeTypedArray*> target,
                                HandleArrayObject source)
    {
        MOZ_ASSERT(target->type() == SpecificArray::ArrayTypeID(),
                   "target type and NativeType must match");
        MOZ_ASSERT(IsPackedArray(source), "source array must be packed");
        MOZ_ASSERT(source->getDenseInitializedLength() <= target->length());

        uint32_t len = source->getDenseInitializedLength();
        uint32_t i = 0;

        // Attempt fast-path infallible conversion of dense elements up to the
        // first potentially side-effectful conversion.

        SharedMem<T*> dest =
            target->template as<TypedArrayObject>().viewDataEither().template cast<T*>();

        const Value* srcValues = source->getDenseElements();
        for (; i < len; i++) {
            if (!canConvertInfallibly(srcValues[i], target->type()))
                break;
            Ops::store(dest + i, infallibleValueToNative(srcValues[i]));
        }
        if (i == len)
            return true;

        // Convert any remaining elements by first collecting them into a
        // temporary list, and then copying them into the typed array.
        AutoValueVector values(cx);
        if (!values.append(srcValues + i, len - i))
            return false;

        RootedValue v(cx);
        for (uint32_t j = 0; j < values.length(); i++, j++) {
            v = values[j];

            T n;
            if (!valueToNative(cx, v, &n))
                return false;

            // |target| is a newly allocated typed array and not yet visible to
            // content script, so valueToNative can't detach the underlying
            // buffer.
            MOZ_ASSERT(i < target->length());

            // Compute every iteration in case GC moves the data.
            SharedMem<T*> newDest =
                target->template as<TypedArrayObject>().viewDataEither().template cast<T*>();
            Ops::store(newDest + i, n);
        }

        return true;
    }

  private:
    static bool
    setFromOverlappingTypedArray(JSContext* cx,
                                 Handle<SomeTypedArray*> target,
                                 Handle<SomeTypedArray*> source,
                                 uint32_t offset)
    {
        MOZ_ASSERT(SpecificArray::ArrayTypeID() == target->type(),
                   "calling wrong setFromTypedArray specialization");
        MOZ_ASSERT(SomeTypedArray::sameBuffer(target, source),
                   "the provided arrays don't actually overlap, so it's "
                   "undesirable to use this method");

        MOZ_ASSERT(offset <= target->length());
        MOZ_ASSERT(source->length() <= target->length() - offset);

        SharedMem<T*> dest =
            target->template as<TypedArrayObject>().viewDataEither().template cast<T*>() + offset;
        uint32_t len = source->length();

        if (source->type() == target->type()) {
            SharedMem<T*> src =
                source->template as<TypedArrayObject>().viewDataEither().template cast<T*>();
            Ops::podMove(dest, src, len);
            return true;
        }

        if (std::is_same<T, uint8_clamped>::value &&
            (source->type() == Scalar::Uint8 || source->type() == Scalar::Uint8Clamped))
        {
            SharedMem<T*> src =
                source->template as<TypedArrayObject>().viewDataEither().template cast<T*>();
            Ops::podMove(dest, src, len);
            return true;
        }

        // Copy |source| in case it overlaps the target elements being set.
        size_t sourceByteLen = len * source->bytesPerElement();
        void* data = target->zone()->template pod_malloc<uint8_t>(sourceByteLen);
        if (!data)
            return false;
        Ops::memcpy(SharedMem<void*>::unshared(data),
                    source->template as<TypedArrayObject>().viewDataEither(),
                    sourceByteLen);

        switch (source->type()) {
          case Scalar::Int8: {
            int8_t* src = static_cast<int8_t*>(data);
#ifdef JS_TYPEDARRAY_HAS_SSE2
                        if (std::is_same<T, uint8_clamped>::value && std::is_same<Ops, UnsharedOps>::value) {
                                SSE2ConvertInt8ToUint8Clamped(reinterpret_cast<uint8_clamped*>(dest.unwrapUnshared()), src, len);
                                break;
                        }
#endif
            for (uint32_t i = 0; i < len; ++i)
                Ops::store(dest++, ConvertNumber<T>(*src++));
            break;
          }
          case Scalar::Uint8:
          case Scalar::Uint8Clamped: {
            uint8_t* src = static_cast<uint8_t*>(data);
                        if (std::is_same<T, uint8_clamped>::value && std::is_same<Ops, UnsharedOps>::value) {
                                Ops::podCopy(dest, SharedMem<void*>::unshared(src).template cast<T*>(), len);
                                break;
                        }
            for (uint32_t i = 0; i < len; ++i)
                Ops::store(dest++, ConvertNumber<T>(*src++));
            break;
          }
          case Scalar::Int16: {
            int16_t* src = static_cast<int16_t*>(data);
#ifdef JS_TYPEDARRAY_HAS_SSE2
                        if (std::is_same<T, uint8_clamped>::value && std::is_same<Ops, UnsharedOps>::value) {
                                SSE2ConvertInt16ToUint8Clamped(reinterpret_cast<uint8_clamped*>(dest.unwrapUnshared()), src, len);
                                break;
                        }
#endif
            for (uint32_t i = 0; i < len; ++i)
                Ops::store(dest++, ConvertNumber<T>(*src++));
            break;
          }
          case Scalar::Uint16: {
            uint16_t* src = static_cast<uint16_t*>(data);
#ifdef JS_TYPEDARRAY_HAS_SSE2
                        if (std::is_same<T, uint8_clamped>::value && std::is_same<Ops, UnsharedOps>::value) {
                                SSE2ConvertUint16ToUint8Clamped(reinterpret_cast<uint8_clamped*>(dest.unwrapUnshared()), src, len);
                                break;
                        }
#endif
            for (uint32_t i = 0; i < len; ++i)
                Ops::store(dest++, ConvertNumber<T>(*src++));
            break;
          }
          case Scalar::Int32: {
            int32_t* src = static_cast<int32_t*>(data);
#ifdef JS_TYPEDARRAY_HAS_SSE2
                        if (std::is_same<T, uint8_clamped>::value && std::is_same<Ops, UnsharedOps>::value) {
                                SSE2ConvertInt32ToUint8Clamped(reinterpret_cast<uint8_clamped*>(dest.unwrapUnshared()), src, len);
                                break;
                        }
#endif
            for (uint32_t i = 0; i < len; ++i)
                Ops::store(dest++, ConvertNumber<T>(*src++));
            break;
          }
          case Scalar::Uint32: {
            uint32_t* src = static_cast<uint32_t*>(data);
#ifdef JS_TYPEDARRAY_HAS_SSE2
                        if (std::is_same<T, uint8_clamped>::value && std::is_same<Ops, UnsharedOps>::value) {
                                SSE2ConvertUint32ToUint8Clamped(reinterpret_cast<uint8_clamped*>(dest.unwrapUnshared()), src, len);
                                break;
                        }
#endif
            for (uint32_t i = 0; i < len; ++i)
                Ops::store(dest++, ConvertNumber<T>(*src++));
            break;
          }
          case Scalar::BigInt64: {
            int64_t* src = static_cast<int64_t*>(data);
            for (uint32_t i = 0; i < len; ++i)
                Ops::store(dest++, ConvertNumber<T>(*src++));
            break;
          }
          case Scalar::BigUint64: {
            uint64_t* src = static_cast<uint64_t*>(data);
            for (uint32_t i = 0; i < len; ++i)
                Ops::store(dest++, ConvertNumber<T>(*src++));
            break;
          }
          case Scalar::Float32: {
            float* src = static_cast<float*>(data);
#ifdef JS_TYPEDARRAY_HAS_SSE2
                        if (std::is_same<T, uint8_clamped>::value && std::is_same<Ops, UnsharedOps>::value) {
                                SSE2ConvertFloatToUint8Clamped(reinterpret_cast<uint8_clamped*>(dest.unwrapUnshared()), src, len);
                                break;
                        }
#endif
            for (uint32_t i = 0; i < len; ++i)
                Ops::store(dest++, ConvertNumber<T>(*src++));
            break;
          }
          case Scalar::Float64: {
            double* src = static_cast<double*>(data);
#ifdef JS_TYPEDARRAY_HAS_SSE2
                        if (std::is_same<T, uint8_clamped>::value && std::is_same<Ops, UnsharedOps>::value) {
                                SSE2ConvertDoubleToUint8Clamped(reinterpret_cast<uint8_clamped*>(dest.unwrapUnshared()), src, len);
                                break;
                        }
#endif
            for (uint32_t i = 0; i < len; ++i)
                Ops::store(dest++, ConvertNumber<T>(*src++));
            break;
          }
          default:
            MOZ_CRASH("setFromOverlappingTypedArray with a typed array with bogus type");
        }

        js_free(data);
        return true;
    }

    static bool
    canConvertInfallibly(const Value& v, Scalar::Type type)
    {
        if (type == Scalar::BigInt64 || type == Scalar::BigUint64) {
            return false;
        }
        return v.isNumber() || v.isBoolean() || v.isNull() || v.isUndefined();
    }

    static T
    infallibleValueToNative(const Value& v)
    {
        if (v.isInt32())
            return T(v.toInt32());
        if (v.isDouble())
            return doubleToNative(v.toDouble());
        if (v.isBoolean())
            return T(v.toBoolean());
        if (v.isNull())
            return T(0);

        MOZ_ASSERT(v.isUndefined());
        return TypeIsFloatingPoint<T>() ? T(JS::GenericNaN()) : T(0);
    }

    static bool
    valueToNative(JSContext* cx, HandleValue v, T* result)
    {
        MOZ_ASSERT(!v.isMagic());

        if (MOZ_LIKELY(canConvertInfallibly(v, TypeIDOfType<T>::id))) {
            *result = infallibleValueToNative(v);
            return true;
        }

        if (std::is_same<T, int64_t>::value) {
            JS_TRY_VAR_OR_RETURN_FALSE(cx, *result, ToBigInt64(cx, v));
            return true;
        }

        if (std::is_same<T, uint64_t>::value) {
            JS_TRY_VAR_OR_RETURN_FALSE(cx, *result, ToBigUint64(cx, v));
            return true;
        }

        double d;
        MOZ_ASSERT(v.isString() || v.isObject() || v.isSymbol());
        if (!(v.isString() ? StringToNumber(cx, v.toString(), &d) : ToNumber(cx, v, &d)))
            return false;

        *result = doubleToNative(d);
        return true;
    }

    static T
    doubleToNative(double d)
    {
        if (TypeIsFloatingPoint<T>()) {
#ifdef JS_MORE_DETERMINISTIC
            // The JS spec doesn't distinguish among different NaN values, and
            // it deliberately doesn't specify the bit pattern written to a
            // typed array when NaN is written into it.  This bit-pattern
            // inconsistency could confuse deterministic testing, so always
            // canonicalize NaN values in more-deterministic builds.
            d = JS::CanonicalizeNaN(d);
#endif
            return T(d);
        }
        if (MOZ_UNLIKELY(mozilla::IsNaN(d)))
            return T(0);
        if (SpecificArray::ArrayTypeID() == Scalar::Uint8Clamped)
            return T(d);
        if (TypeIsUnsigned<T>())
            return T(JS::ToUint32(d));
        return T(JS::ToInt32(d));
    }
};

template<typename SomeTypedArray>
class TypedArrayMethods
{
    static_assert(mozilla::IsSame<SomeTypedArray, TypedArrayObject>::value,
                  "methods must be shared/unshared-specific, not "
                  "element-type-specific");

    typedef typename SomeTypedArray::BufferType BufferType;

    typedef typename SomeTypedArray::template OfType<int8_t>::Type Int8ArrayType;
    typedef typename SomeTypedArray::template OfType<uint8_t>::Type Uint8ArrayType;
    typedef typename SomeTypedArray::template OfType<int16_t>::Type Int16ArrayType;
    typedef typename SomeTypedArray::template OfType<uint16_t>::Type Uint16ArrayType;
    typedef typename SomeTypedArray::template OfType<int32_t>::Type Int32ArrayType;
    typedef typename SomeTypedArray::template OfType<uint32_t>::Type Uint32ArrayType;
    typedef typename SomeTypedArray::template OfType<int64_t>::Type BigInt64ArrayType;
    typedef typename SomeTypedArray::template OfType<uint64_t>::Type BigUint64ArrayType;
    typedef typename SomeTypedArray::template OfType<float>::Type Float32ArrayType;
    typedef typename SomeTypedArray::template OfType<double>::Type Float64ArrayType;
    typedef typename SomeTypedArray::template OfType<uint8_clamped>::Type Uint8ClampedArrayType;

  public:
    /* set(array[, offset]) */
    static bool
    set(JSContext* cx, const CallArgs& args)
    {
        MOZ_ASSERT(SomeTypedArray::is(args.thisv()));

        Rooted<SomeTypedArray*> target(cx, &args.thisv().toObject().as<SomeTypedArray>());

        // The first argument must be either a typed array or arraylike.
        if (args.length() == 0 || !args[0].isObject()) {
            JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr, JSMSG_TYPED_ARRAY_BAD_ARGS);
            return false;
        }

        int32_t offset = 0;
        if (args.length() > 1) {
            if (!ToInt32(cx, args[1], &offset))
                return false;

            if (offset < 0 || uint32_t(offset) > target->length()) {
                // the given offset is bogus
                JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr, JSMSG_BAD_INDEX);
                return false;
            }
        }

        RootedObject arg0(cx, &args[0].toObject());
        if (arg0->is<TypedArrayObject>()) {
            if (arg0->as<TypedArrayObject>().length() > target->length() - offset) {
                JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr, JSMSG_BAD_ARRAY_LENGTH);
                return false;
            }

            if (!setFromTypedArray(cx, target, arg0, offset))
                return false;
        } else {
            uint32_t len;
            if (!GetLengthProperty(cx, arg0, &len))
                return false;

            if (uint32_t(offset) > target->length() || len > target->length() - offset) {
                JS_ReportErrorNumberASCII(cx, GetErrorMessage, nullptr, JSMSG_BAD_ARRAY_LENGTH);
                return false;
            }

            if (!setFromNonTypedArray(cx, target, arg0, len, offset))
                return false;
        }

        args.rval().setUndefined();
        return true;
    }

     static bool
     setFromTypedArray(JSContext* cx, Handle<SomeTypedArray*> target, HandleObject source,
                       uint32_t offset = 0)
     {
         MOZ_ASSERT(source->is<TypedArrayObject>(), "use setFromNonTypedArray");

         bool isShared = target->isSharedMemory() || source->as<TypedArrayObject>().isSharedMemory();

         switch (target->type()) {
           case Scalar::Int8:
             if (isShared)
                 return ElementSpecific<Int8ArrayType, SharedOps>::setFromTypedArray(cx, target, source, offset);
             return ElementSpecific<Int8ArrayType, UnsharedOps>::setFromTypedArray(cx, target, source, offset);
           case Scalar::Uint8:
             if (isShared)
                 return ElementSpecific<Uint8ArrayType, SharedOps>::setFromTypedArray(cx, target, source, offset);
             return ElementSpecific<Uint8ArrayType, UnsharedOps>::setFromTypedArray(cx, target, source, offset);
           case Scalar::Int16:
             if (isShared)
                 return ElementSpecific<Int16ArrayType, SharedOps>::setFromTypedArray(cx, target, source, offset);
             return ElementSpecific<Int16ArrayType, UnsharedOps>::setFromTypedArray(cx, target, source, offset);
           case Scalar::Uint16:
             if (isShared)
                 return ElementSpecific<Uint16ArrayType, SharedOps>::setFromTypedArray(cx, target, source, offset);
             return ElementSpecific<Uint16ArrayType, UnsharedOps>::setFromTypedArray(cx, target, source, offset);
           case Scalar::Int32:
             if (isShared)
                 return ElementSpecific<Int32ArrayType, SharedOps>::setFromTypedArray(cx, target, source, offset);
             return ElementSpecific<Int32ArrayType, UnsharedOps>::setFromTypedArray(cx, target, source, offset);
           case Scalar::Uint32:
             if (isShared)
                 return ElementSpecific<Uint32ArrayType, SharedOps>::setFromTypedArray(cx, target, source, offset);
             return ElementSpecific<Uint32ArrayType, UnsharedOps>::setFromTypedArray(cx, target, source, offset);
           case Scalar::BigInt64:
             if (isShared)
                 return ElementSpecific<BigInt64ArrayType, SharedOps>::setFromTypedArray(cx, target, source, offset);
               return ElementSpecific<BigInt64ArrayType, UnsharedOps>::setFromTypedArray(cx, target, source, offset);
           case Scalar::BigUint64:
             if (isShared)
                 return ElementSpecific<BigUint64ArrayType, SharedOps>::setFromTypedArray(cx, target, source, offset);
               return ElementSpecific<BigUint64ArrayType, UnsharedOps>::setFromTypedArray(cx, target, source, offset);
           case Scalar::Float32:
             if (isShared)
                 return ElementSpecific<Float32ArrayType, SharedOps>::setFromTypedArray(cx, target, source, offset);
             return ElementSpecific<Float32ArrayType, UnsharedOps>::setFromTypedArray(cx, target, source, offset);
           case Scalar::Float64:
             if (isShared)
                 return ElementSpecific<Float64ArrayType, SharedOps>::setFromTypedArray(cx, target, source, offset);
             return ElementSpecific<Float64ArrayType, UnsharedOps>::setFromTypedArray(cx, target, source, offset);
           case Scalar::Uint8Clamped:
             if (isShared)
                 return ElementSpecific<Uint8ClampedArrayType, SharedOps>::setFromTypedArray(cx, target, source, offset);
             return ElementSpecific<Uint8ClampedArrayType, UnsharedOps>::setFromTypedArray(cx, target, source, offset);
           case Scalar::Int64:
           case Scalar::MaxTypedArrayViewType:
             break;
         }

         MOZ_CRASH("nonsense target element type");
     }

    static bool
    setFromNonTypedArray(JSContext* cx, Handle<SomeTypedArray*> target, HandleObject source,
                         uint32_t len, uint32_t offset = 0)
    {
        MOZ_ASSERT(!source->is<TypedArrayObject>(), "use setFromTypedArray");

        bool isShared = target->isSharedMemory();

        switch (target->type()) {
          case Scalar::Int8:
            if (isShared)
                return ElementSpecific<Int8ArrayType, SharedOps>::setFromNonTypedArray(cx, target, source, len, offset);
            return ElementSpecific<Int8ArrayType, UnsharedOps>::setFromNonTypedArray(cx, target, source, len, offset);
          case Scalar::Uint8:
            if (isShared)
                return ElementSpecific<Uint8ArrayType, SharedOps>::setFromNonTypedArray(cx, target, source, len, offset);
            return ElementSpecific<Uint8ArrayType, UnsharedOps>::setFromNonTypedArray(cx, target, source, len, offset);
          case Scalar::Int16:
            if (isShared)
                return ElementSpecific<Int16ArrayType, SharedOps>::setFromNonTypedArray(cx, target, source, len, offset);
            return ElementSpecific<Int16ArrayType, UnsharedOps>::setFromNonTypedArray(cx, target, source, len, offset);
          case Scalar::Uint16:
            if (isShared)
                return ElementSpecific<Uint16ArrayType, SharedOps>::setFromNonTypedArray(cx, target, source, len, offset);
            return ElementSpecific<Uint16ArrayType, UnsharedOps>::setFromNonTypedArray(cx, target, source, len, offset);
          case Scalar::Int32:
            if (isShared)
                return ElementSpecific<Int32ArrayType, SharedOps>::setFromNonTypedArray(cx, target, source, len, offset);
            return ElementSpecific<Int32ArrayType, UnsharedOps>::setFromNonTypedArray(cx, target, source, len, offset);
          case Scalar::Uint32:
            if (isShared)
                return ElementSpecific<Uint32ArrayType, SharedOps>::setFromNonTypedArray(cx, target, source, len, offset);
            return ElementSpecific<Uint32ArrayType, UnsharedOps>::setFromNonTypedArray(cx, target, source, len, offset);
          case Scalar::BigInt64:
            if (isShared)
                return ElementSpecific<BigInt64ArrayType, SharedOps>::setFromNonTypedArray(cx, target, source, len, offset);
            return ElementSpecific<BigInt64ArrayType, UnsharedOps>::setFromNonTypedArray(cx, target, source, len, offset);
          case Scalar::BigUint64:
            if (isShared)
                return ElementSpecific<BigUint64ArrayType, SharedOps>::setFromNonTypedArray(cx, target, source, len, offset);
            return ElementSpecific<BigUint64ArrayType, UnsharedOps>::setFromNonTypedArray(cx, target, source, len, offset);
          case Scalar::Float32:
            if (isShared)
                return ElementSpecific<Float32ArrayType, SharedOps>::setFromNonTypedArray(cx, target, source, len, offset);
            return ElementSpecific<Float32ArrayType, UnsharedOps>::setFromNonTypedArray(cx, target, source, len, offset);
          case Scalar::Float64:
            if (isShared)
                return ElementSpecific<Float64ArrayType, SharedOps>::setFromNonTypedArray(cx, target, source, len, offset);
            return ElementSpecific<Float64ArrayType, UnsharedOps>::setFromNonTypedArray(cx, target, source, len, offset);
          case Scalar::Uint8Clamped:
            if (isShared)
                return ElementSpecific<Uint8ClampedArrayType, SharedOps>::setFromNonTypedArray(cx, target, source, len, offset);
            return ElementSpecific<Uint8ClampedArrayType, UnsharedOps>::setFromNonTypedArray(cx, target, source, len, offset);
          case Scalar::MaxTypedArrayViewType:
            break;
        }
        MOZ_CRASH("bad target array type");
    }

    static bool
    initFromIterablePackedArray(JSContext* cx, Handle<SomeTypedArray*> target,
                                HandleArrayObject source)
    {
        bool isShared = target->isSharedMemory();

        switch (target->type()) {
          case Scalar::Int8:
            if (isShared)
                return ElementSpecific<Int8ArrayType, SharedOps>::initFromIterablePackedArray(cx, target, source);
            return ElementSpecific<Int8ArrayType, UnsharedOps>::initFromIterablePackedArray(cx, target, source);
          case Scalar::Uint8:
            if (isShared)
                return ElementSpecific<Uint8ArrayType, SharedOps>::initFromIterablePackedArray(cx, target, source);
            return ElementSpecific<Uint8ArrayType, UnsharedOps>::initFromIterablePackedArray(cx, target, source);
          case Scalar::Int16:
            if (isShared)
                return ElementSpecific<Int16ArrayType, SharedOps>::initFromIterablePackedArray(cx, target, source);
            return ElementSpecific<Int16ArrayType, UnsharedOps>::initFromIterablePackedArray(cx, target, source);
          case Scalar::Uint16:
            if (isShared)
                return ElementSpecific<Uint16ArrayType, SharedOps>::initFromIterablePackedArray(cx, target, source);
            return ElementSpecific<Uint16ArrayType, UnsharedOps>::initFromIterablePackedArray(cx, target, source);
          case Scalar::Int32:
            if (isShared)
                return ElementSpecific<Int32ArrayType, SharedOps>::initFromIterablePackedArray(cx, target, source);
            return ElementSpecific<Int32ArrayType, UnsharedOps>::initFromIterablePackedArray(cx, target, source);
          case Scalar::Uint32:
            if (isShared)
                return ElementSpecific<Uint32ArrayType, SharedOps>::initFromIterablePackedArray(cx, target, source);
            return ElementSpecific<Uint32ArrayType, UnsharedOps>::initFromIterablePackedArray(cx, target, source);
          case Scalar::BigInt64:
            if (isShared)
                return ElementSpecific<BigInt64ArrayType, SharedOps>::initFromIterablePackedArray(cx, target, source);
            return ElementSpecific<BigInt64ArrayType, UnsharedOps>::initFromIterablePackedArray(cx, target, source);
          case Scalar::BigUint64:
            if (isShared)
                return ElementSpecific<BigUint64ArrayType, SharedOps>::initFromIterablePackedArray(cx, target, source);
            return ElementSpecific<BigUint64ArrayType, UnsharedOps>::initFromIterablePackedArray(cx, target, source);
          case Scalar::Float32:
            if (isShared)
                return ElementSpecific<Float32ArrayType, SharedOps>::initFromIterablePackedArray(cx, target, source);
            return ElementSpecific<Float32ArrayType, UnsharedOps>::initFromIterablePackedArray(cx, target, source);
          case Scalar::Float64:
            if (isShared)
                return ElementSpecific<Float64ArrayType, SharedOps>::initFromIterablePackedArray(cx, target, source);
            return ElementSpecific<Float64ArrayType, UnsharedOps>::initFromIterablePackedArray(cx, target, source);
          case Scalar::Uint8Clamped:
            if (isShared)
                return ElementSpecific<Uint8ClampedArrayType, SharedOps>::initFromIterablePackedArray(cx, target, source);
            return ElementSpecific<Uint8ClampedArrayType, UnsharedOps>::initFromIterablePackedArray(cx, target, source);
          case Scalar::Int64:
          case Scalar::MaxTypedArrayViewType:
            break;
        }
        MOZ_CRASH("bad target array type");
    }
};

} // namespace js

#endif // vm_TypedArrayCommon_h
