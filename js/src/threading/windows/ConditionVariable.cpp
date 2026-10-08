/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/Assertions.h"

#include <float.h>
#include <intrin.h>
#include <stdlib.h>
#include <windows.h>

#include "threading/ConditionVariable.h"
#include "threading/Mutex.h"
#include "threading/windows/MutexPlatformData.h"

// Some versions of the Windows SDK have a bug where some interlocked functions
// are not redefined as compiler intrinsics. Fix that for the interlocked
// functions that are used in this file.
#if defined(_MSC_VER) && !defined(InterlockedExchangeAdd)
#define InterlockedExchangeAdd(addend, value)                                  \
  _InterlockedExchangeAdd((volatile long*)(addend), (long)(value))
#endif

#if defined(_MSC_VER) && !defined(InterlockedIncrement)
#define InterlockedIncrement(addend)                                           \
  _InterlockedIncrement((volatile long*)(addend))
#endif

// Windows XP and Server 2003 don't support condition variables natively. The
// NativeImports class is responsible for detecting native support and
// retrieving the appropriate function pointers. It gets instantiated once,
// using a static initializer.
class ConditionVariableNativeImports
{
public:
  ConditionVariableNativeImports() {
    HMODULE kernel32_dll = GetModuleHandle("kernel32.dll");
    MOZ_RELEASE_ASSERT(kernel32_dll != NULL);

#define LOAD_SYMBOL(symbol) loadSymbol(kernel32_dll, #symbol, symbol)
    supported_ = LOAD_SYMBOL(InitializeConditionVariable) &&
                 LOAD_SYMBOL(WakeConditionVariable) &&
                 LOAD_SYMBOL(WakeAllConditionVariable) &&
                 LOAD_SYMBOL(SleepConditionVariableCS);
#undef LOAD_SYMBOL
  }

  inline bool supported() const {
    return supported_;
  }

  void(WINAPI* InitializeConditionVariable)(CONDITION_VARIABLE* ConditionVariable);
  void(WINAPI* WakeAllConditionVariable)(PCONDITION_VARIABLE ConditionVariable);
  void(WINAPI* WakeConditionVariable)(CONDITION_VARIABLE* ConditionVariable);
  BOOL(WINAPI* SleepConditionVariableCS)(CONDITION_VARIABLE* ConditionVariable,
                                         CRITICAL_SECTION* CriticalSection,
                                         DWORD dwMilliseconds);

private:
  template <typename T>
  inline bool loadSymbol(HMODULE module, const char* name, T& fn) {
    FARPROC ptr = GetProcAddress(module, name);
    if (!ptr)
      return false;

    fn = reinterpret_cast<T>(ptr);
    return true;
  }

  bool supported_;
};

static ConditionVariableNativeImports sNativeImports;

// Wrapper for native condition variable APIs.
struct ConditionVariableNative
{
  inline void initialize() {
    sNativeImports.InitializeConditionVariable(&cv_);
  }

  inline void destroy() {
    // Native condition variables don't require cleanup.
  }

  inline void notify_one() { sNativeImports.WakeConditionVariable(&cv_); }

  inline void notify_all() { sNativeImports.WakeAllConditionVariable(&cv_); }

  inline bool wait(CRITICAL_SECTION* cs, DWORD msec) {
    return sNativeImports.SleepConditionVariableCS(&cv_, cs, msec);
  }

private:
  CONDITION_VARIABLE cv_;
};

// Fast fallback condition variable support for Windows XP and Server 2003.
struct ConditionVariableFallback {
  uint32_t waiting;
  CRITICAL_SECTION lock_waiting;
  enum {
    SIGNAL = 0,
    BROADCAST = 1,
    MAX_EVENTS = 2
  } EVENTS;
  HANDLE events[MAX_EVENTS];
  HANDLE broadcast_block_event;

  void initialize() {
    waiting = 0;
    InitializeCriticalSection(&lock_waiting);

    events[SIGNAL] = CreateEventW(NULL, FALSE, FALSE, NULL);
    events[BROADCAST] = CreateEventW(NULL, TRUE, FALSE, NULL);
    broadcast_block_event = CreateEventW(NULL, TRUE, TRUE, NULL);
  }

  void destroy() {
    DeleteCriticalSection(&lock_waiting);

    CloseHandle(events[SIGNAL]);
    CloseHandle(events[BROADCAST]);
    CloseHandle(broadcast_block_event);
  }

 public:
  void notify_one() {
    EnterCriticalSection(&lock_waiting);
    if (waiting > 0) {
      SetEvent(events[SIGNAL]);
    }
    LeaveCriticalSection(&lock_waiting);
  }

  void notify_all() {
    EnterCriticalSection(&lock_waiting);

    // Protects us from broadcasting if there isn't any thread waiting to
    // open the block gate after this call has closed it.
    if (waiting > 0) {
      // Close block gate
      ResetEvent(broadcast_block_event);
      // Open broadcast gate
      SetEvent(events[BROADCAST]);
    }

    LeaveCriticalSection(&lock_waiting);
  }

  bool wait(CRITICAL_SECTION* userLock, DWORD msec) {
    int result;
    DWORD timeout = msec;

    // Block access if previous broadcast hasn't finished. This should normally
    // not affect total time spent in this function.
    WaitForSingleObject(broadcast_block_event, INFINITE);

    EnterCriticalSection(&lock_waiting);
    waiting++;
    LeaveCriticalSection(&lock_waiting);

    LeaveCriticalSection(userLock);

    result = WaitForMultipleObjects(2, events, FALSE, timeout);

    EnterCriticalSection(&lock_waiting);
    waiting--;

    if (waiting == 0) {
      // We're the last waiter to be notified or to stop waiting.
      // Reset state so next broadcast can proceed.
      ResetEvent(events[BROADCAST]);
      SetEvent(broadcast_block_event);
    }

    LeaveCriticalSection(&lock_waiting);

    EnterCriticalSection(userLock);

    // Return true if woken up, false when timed out.
    if (result == WAIT_TIMEOUT) {
      SetLastError(ERROR_TIMEOUT);
      return false;
    }
    return true;
  }
};

struct js::ConditionVariable::PlatformData
{
  union
  {
    ConditionVariableNative native;
    ConditionVariableFallback fallback;
  };
};

js::ConditionVariable::ConditionVariable()
{
  if (sNativeImports.supported())
    platformData()->native.initialize();
  else
    platformData()->fallback.initialize();
}

void
js::ConditionVariable::notify_one()
{
  if (sNativeImports.supported())
    platformData()->native.notify_one();
  else
    platformData()->fallback.notify_one();
}

void
js::ConditionVariable::notify_all()
{
  if (sNativeImports.supported())
    platformData()->native.notify_all();
  else
    platformData()->fallback.notify_all();
}

void
js::ConditionVariable::wait(UniqueLock<Mutex>& lock)
{
  CRITICAL_SECTION* cs = &lock.lock.platformData()->criticalSection;
  bool r;
  if (sNativeImports.supported())
    r = platformData()->native.wait(cs, INFINITE);
  else
    r = platformData()->fallback.wait(cs, INFINITE);
  MOZ_RELEASE_ASSERT(r);
}

js::CVStatus
js::ConditionVariable::wait_until(UniqueLock<Mutex>& lock,
                                  const mozilla::TimeStamp& abs_time)
{
  return wait_for(lock, abs_time - mozilla::TimeStamp::Now());
}

js::CVStatus
js::ConditionVariable::wait_for(UniqueLock<Mutex>& lock,
                                const mozilla::TimeDuration& rel_time)
{
  if (rel_time == mozilla::TimeDuration::Forever()) {
    wait(lock);
    return CVStatus::NoTimeout;
  }

  CRITICAL_SECTION* cs = &lock.lock.platformData()->criticalSection;

  // Note that DWORD is unsigned, so we have to be careful to clamp at 0.
  // If rel_time is Forever, then ToMilliseconds is +inf, resulting in INFINITE.
  // Don't round sub-millisecond waits to 0; round them to 1ms instead.
  double msecd = rel_time.ToMilliseconds();
  DWORD msec;
  if (msecd < 0.0) {
    msec = 0;
  } else if (msecd > UINT32_MAX) {
    msec = INFINITE;
  } else {
    msec = static_cast<DWORD>(msecd);
    // Round submillisecond waits to 1ms.
    if (msec == 0 && !rel_time.IsZero()) {
      msec = 1;
    }
  }

  BOOL r;
  if (sNativeImports.supported())
    r = platformData()->native.wait(cs, msec);
  else
    r = platformData()->fallback.wait(cs, msec);
  if (r)
    return CVStatus::NoTimeout;
  MOZ_RELEASE_ASSERT(GetLastError() == ERROR_TIMEOUT);
  return CVStatus::Timeout;
}

js::ConditionVariable::~ConditionVariable()
{
  if (sNativeImports.supported())
    platformData()->native.destroy();
  else
    platformData()->fallback.destroy();
}

inline js::ConditionVariable::PlatformData*
js::ConditionVariable::platformData()
{
  static_assert(sizeof platformData_ >= sizeof(PlatformData),
                "platformData_ is too small");
  return reinterpret_cast<PlatformData*>(platformData_);
}
