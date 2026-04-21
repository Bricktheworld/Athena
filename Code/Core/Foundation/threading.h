#pragma once
#include "Core/Foundation/types.h"
#include "Core/Foundation/memory.h"

#include <atomic>

typedef u32 (*ThreadProc)(void*);

// NOTE(bshihabi): I would've really preferred to use the intrinsics here, but it's just too complicated for me to reasonably do
template <typename T>
using Atomic = std::atomic<T>;

struct Thread
{
  HANDLE handle = nullptr;
  DWORD id = 0;
};

FOUNDATION_API Thread init_thread(
  AllocHeap heap,
  u64 stack_size,
  ThreadProc proc,
  void* param,
  u8 core_index
);

FOUNDATION_API void destroy_thread(Thread* thread);
FOUNDATION_API u32 get_num_physical_cores();
FOUNDATION_API void set_thread_name(const Thread* thread, const wchar_t* name);
FOUNDATION_API void set_current_thread_name(const wchar_t* name);
FOUNDATION_API void join_threads(const Thread* threads, u32 count);

struct RWLock
{
  SRWLOCK lock = {0};
};

FOUNDATION_API void rw_acquire_read(RWLock* lock);
FOUNDATION_API void rw_release_read(RWLock* lock);
FOUNDATION_API void rw_acquire_write(RWLock* lock);
FOUNDATION_API void rw_release_write(RWLock* lock);

struct Mutex
{
  SRWLOCK lock = {0};
};

FOUNDATION_API void mutex_acquire(Mutex* mutex);
FOUNDATION_API void mutex_release(Mutex* mutex);

struct ThreadSignal
{
  CONDITION_VARIABLE cond_var;
  SRWLOCK lock = {0};
};

FOUNDATION_API ThreadSignal init_thread_signal();
FOUNDATION_API void wait_for_thread_signal(ThreadSignal* signal);
FOUNDATION_API void notify_one_thread_signal(ThreadSignal* signal);
FOUNDATION_API void notify_all_thread_signal(ThreadSignal* signal);

struct SpinLock
{
  u64 value = 0;
};

FOUNDATION_API                    SpinLock init_spin_lock();
FOUNDATION_API                    void spin_acquire(SpinLock* spin_lock);
FOUNDATION_API DONT_IGNORE_RETURN bool try_spin_acquire(SpinLock* spin_lock, u64 max_cycles);
FOUNDATION_API                    void spin_release(SpinLock* spin_lock);

template <typename T>
struct SpinLocked
{
  SpinLocked() : m_lock(init_spin_lock()) {}
  SpinLocked(const T& val) : m_value(val), m_lock(init_spin_lock()) {}

  template <typename F>
  auto acquire(F f)
  {
    spin_acquire(&m_lock);
    defer { spin_release(&m_lock); };
    return f(&m_value);
  }

  T m_value;
  SpinLock m_lock;
};

#define ACQUIRE(lock, var) (*lock) * [&](var)

template <typename T, typename F, typename R>
struct __SpinUnlocked__
{
  R ret;

  __SpinUnlocked__(SpinLocked<T>* lock, F f)
  {
    spin_acquire(&lock->m_lock);
    ret = f(&lock->m_value);
    spin_release(&lock->m_lock);
  }

  operator R() { return ret; }
};

template <typename T, typename F>
struct __SpinUnlocked__<T, F, void>
{
  __SpinUnlocked__(SpinLocked<T>* lock, F f)
  {
    spin_acquire(&lock->m_lock);
    f(&lock->m_value);
    spin_release(&lock->m_lock);
  }
};

template <typename T, typename F>
auto operator*(SpinLocked<T>& lock, F f)
{
  return __SpinUnlocked__<T, F, decltype(f((T*)nullptr))>(&lock, f);
}

template <typename T>
inline T
atomic_load(const Atomic<T>& a)
{
  return a.load();
}

template <typename T>
inline void
atomic_store(Atomic<T>* lhs, T rhs)
{
  lhs->store(rhs);
}


template <typename T>
inline bool
atomic_compare_exchange(Atomic<T>* lhs, T rhs, T* expected)
{
  lhs->compare_exchange_weak(*expected, rhs);
}

