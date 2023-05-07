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

