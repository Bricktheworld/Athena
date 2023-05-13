#pragma once
#include "types.h"

typedef DWORD (*ThreadProc)(LPVOID);

struct Thread
{
	HANDLE handle = nullptr;
	DWORD id = 0;
};

Thread create_thread(size_t stack_size, ThreadProc proc, void* param, u8 core_index);
void destroy_thread(Thread* thread);

struct SpinLock
{
	volatile u64 value = 0;
};


void spin_acquire(SpinLock* spin_lock);
bool try_spin_acquire(SpinLock* spin_lock, u64 max_cycles);
void spin_release(SpinLock* spin_lock);
u32 get_num_physical_cores();

template <typename T>
struct SpinLocked
{
	SpinLocked() = default;
	SpinLocked(const T& val) : m_value(val) {}

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

inline u32
test_and_set(volatile u32* dst, u32 val)
{
	u32 prev, compare_operand;
	do 
	{
		prev = InterlockedCompareExchange(dst, val, compare_operand);
	} while (compare_operand != prev);

	return prev;
}

inline s64
test_and_set(volatile s64* dst, s64 val)
{
	s64 prev, compare_operand;
	do 
	{
		prev = InterlockedCompareExchange64(dst, val, compare_operand);
	} while (compare_operand != prev);

	return prev;
}

#define ACQUIRE(lock, var) (*lock) * [&](var)

