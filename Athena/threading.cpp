#include "threading.h"
#include "array.h"
#include "memory/memory.h"

Thread
create_thread(size_t stack_size, ThreadProc proc, void* param, u8 core_index)
{
	ASSERT(core_index < 32);
	Thread ret = {0};
	ret.handle = CreateThread(0, stack_size, proc, param, 0, &ret.id);
	SetThreadAffinityMask(ret.handle, (1ULL << core_index));

	return ret;
}

void
destroy_thread(Thread* thread)
{
	CloseHandle(thread->handle);

	zero_memory(thread, sizeof(Thread));
}

u32
get_num_physical_cores()
{
	SYSTEM_INFO info = {0};
	GetSystemInfo(&info);
	return info.dwNumberOfProcessors;
}

void
set_thread_name(const Thread* thread, const wchar_t* name)
{
	HASSERT(SetThreadDescription(thread->handle, name));
}

void
set_current_thread_name(const wchar_t* name)
{
	HASSERT(SetThreadDescription(GetCurrentThread(), name));
}

void
join_threads(const Thread* threads, u32 count)
{
	MemoryArena arena = alloc_memory_arena(count * sizeof(HANDLE));
	defer { free_memory_arena(&arena); };

	Array handles = init_array<HANDLE>(&arena, count);
	for (size_t i = 0; i < count; i++)
	{
		*array_add(&handles) = threads[i].handle;
	}

	WaitForMultipleObjects(count, handles.memory, true, INFINITE);
}

void
spin_acquire(SpinLock* spin_lock)
{
	for (;;)
	{
		if (InterlockedCompareExchange(&spin_lock->value, 1, 0) == 0)
			break;
	}
}

bool
try_spin_acquire(SpinLock* spin_lock, u64 max_cycles)
{
	while (max_cycles-- != 0)
	{
		if (InterlockedCompareExchange(&spin_lock->value, 1, 0) == 0)
			return true;
	}

	return false;
}

void
spin_release(SpinLock* spin_lock)
{
	spin_lock->value = 0;
}
