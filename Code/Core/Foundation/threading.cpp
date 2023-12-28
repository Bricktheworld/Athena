#include "Core/Foundation/threading.h"
#include "Core/Foundation/memory.h"
#include "Core/Foundation/context.h"

#include "Core/Foundation/Containers/array.h"

struct ThreadEntryProcParams
{
  AllocHeap heap = {0};
  ThreadProc proc = nullptr;
  void* user_param = nullptr;
};

// Sets up a memory arena and other things before actually entering
static DWORD
thread_entry_proc(LPVOID void_param)
{
  ThreadEntryProcParams params = *reinterpret_cast<ThreadEntryProcParams*>(void_param);
  // We no longer want anything inside of this memory arena,
  // since it has all been copied out now.

  UNREACHABLE;
  // init_context(params.heap);

  u32 res = params.proc(params.user_param);
  return res;
}

Thread
create_thread(
  AllocHeap scratch_heap,
  size_t stack_size,
  ThreadProc proc,
  void* param,
  u8 core_index
) {
  ThreadEntryProcParams* params = HEAP_ALLOC(ThreadEntryProcParams, scratch_heap, 1);
  params->heap = scratch_heap;
  params->proc = proc;
  params->user_param = param;

  ASSERT(core_index < 32);
  Thread ret = {0};
  ret.handle = CreateThread(0, stack_size, &thread_entry_proc, params, 0, &ret.id);
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
  ASSERT(count <= MAXIMUM_WAIT_OBJECTS);

  ScratchAllocator scratch_allocator = alloc_scratch_arena();
  defer { free_scratch_arena(&scratch_allocator); };

  Array<HANDLE> handles = init_array<HANDLE>(scratch_allocator, count);
  for (size_t i = 0; i < count; i++)
  {
    *array_add(&handles) = threads[i].handle;
  }

  WaitForMultipleObjects(count, handles.memory, true, INFINITE);
}

void
rw_acquire_read(RWLock* lock)
{
  AcquireSRWLockShared(&lock->lock);
}

void
rw_release_read(RWLock* lock)
{
  ReleaseSRWLockShared(&lock->lock);
}

void
rw_acquire_write(RWLock* lock)
{
  AcquireSRWLockExclusive(&lock->lock);
}

void
rw_release_write(RWLock* lock)
{
  ReleaseSRWLockExclusive(&lock->lock);
}

void
mutex_acquire(Mutex* mutex)
{
  AcquireSRWLockExclusive(&mutex->lock);
}

void
mutex_release(Mutex* mutex)
{
  ReleaseSRWLockExclusive(&mutex->lock);
}

ThreadSignal
init_thread_signal()
{
  ThreadSignal ret = {0};
  InitializeConditionVariable(&ret.cond_var);
  return ret;
}

void
wait_for_thread_signal(ThreadSignal* signal)
{
  AcquireSRWLockExclusive(&signal->lock);
  SleepConditionVariableSRW(&signal->cond_var, &signal->lock, INFINITE, 0);
  ReleaseSRWLockExclusive(&signal->lock);
}

void
notify_one_thread_signal(ThreadSignal* signal)
{
  WakeConditionVariable(&signal->cond_var);
}

void
notify_all_thread_signal(ThreadSignal* signal)
{
  WakeAllConditionVariable(&signal->cond_var);
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
