#pragma once
#include "ring_buffer.h"
#include "hash_table.h"
#include "context.h"
#include "threading.h"
#include "pool_allocator.h"
#include "array.h"
#include <immintrin.h>

struct Fiber
{
  void* rip = 0;
  void* rsp = 0;
  void* rbx = 0;
  void* rbp = 0;
  void* r12 = 0;
  void* r13 = 0;
  void* r14 = 0;
  void* r15 = 0;
  void* rdi = 0;
  void* rsi = 0;

  void* rcx = 0;

  u8 yielded = false;

  __m128i xmm6;
  __m128i xmm7;
  __m128i xmm8;
  __m128i xmm9;
  __m128i xmm10;
  __m128i xmm11;
  __m128i xmm12;
  __m128i xmm13;
  __m128i xmm14;
  __m128i xmm15;

  // This stuff is the TIB content
  // https://en.wikipedia.org/wiki/Win32_Thread_Information_Block
  void* stack_low = 0;
  void* stack_high = 0;
  void* fiber_local = 0;
  void* deallocation_stack = 0; // ???? I have no fucking clue what this does
};

Fiber init_fiber(void* stack, size_t stack_size, void* proc, void* param);
extern "C" void launch_fiber(Fiber* fiber);
extern "C" void resume_fiber(Fiber* fiber, void* stack_high);
extern "C" void save_to_fiber(Fiber* out, void* stack_high);
extern "C" void* get_rsp();

//typedef void (*JobEntry)(uintptr_t);

// TODO(Brandon): I have no idea why, but dx12 calls eat massive
// amounts of stack space, so this is a temporary solution.
// What I actually want is to be able to specify whether a job needs
// _more_ stack space than a typical maybe 16 KiB, not have the default
// be a worst case -_-
#define STACK_SIZE KiB(128)
// Literally just a hunk of memory lol.
struct JobStack
{
  alignas(16) byte memory[STACK_SIZE];
  byte scratch_buf[DEFAULT_SCRATCH_SIZE];
};

typedef u64 JobHandle;

void yield_to_counter(JobHandle counter);

struct JobDebugInfo 
{
  const char* file = nullptr;
  int line = 0;
};
#define JOB_DEBUG_INFO_STRUCT JobDebugInfo{__FILE__, __LINE__}

struct JobEntry
{
  void (*func_ptr)(void*);
  u8 params[256]{0};
  u8 param_offset = 0;
};

struct JobDesc
{
  JobHandle completion_signal = 0;

  JobEntry entry = {0};

  JobDebugInfo debug_info = {0};
};

struct WorkingJob
{
  JobDesc job;
  Fiber fiber;

  JobStack* stack = nullptr;
  Context ctx;

  WorkingJob* next = nullptr;
};

struct WorkingJobQueue
{
  WorkingJob* head = nullptr;
  WorkingJob* tail = nullptr;
};

struct JobCounter
{
  volatile u64 value = 0;
  JobHandle id = 0;
  WorkingJobQueue waiting_jobs = {0};
  Option<ThreadSignal*> completion_signal = None;
};

struct JobQueue
{
  RingBuffer queue;
  SpinLock lock;
};

struct JobSystem
{
  JobQueue high_priority;
  JobQueue medium_priority;
  JobQueue low_priority;

  // TODO(Brandon): Ideally, these pools would just be atomic
  // and not SpinLocked.

  SpinLocked<Pool<JobStack>> job_stack_allocator;

  SpinLocked<Pool<WorkingJob>> working_job_allocator;

  SpinLocked<HashTable<JobHandle, JobCounter>> job_counters;
  SpinLocked<WorkingJobQueue> working_jobs_queue;

  volatile JobHandle current_job_counter_id = 1;

  bool should_exit = 0;
};

enum JobPriority : u8
{
  kJobPriorityHigh,
  kJobPriorityMedium,
  kJobPriorityLow,

  kJobPriorityCount,
};

JobSystem* init_job_system(MEMORY_ARENA_PARAM, size_t job_queue_size);

// These must be called _inside_ of a job.
JobSystem* get_job_system();
void kill_job_system(JobSystem* job_system);

Array<Thread> spawn_job_system_workers(MEMORY_ARENA_PARAM, JobSystem* job_system);

bool job_has_completed(JobHandle handle, JobSystem* job_system = nullptr);

JobHandle _kick_jobs(JobPriority priority,
                        JobDesc* jobs,
                        size_t count,
                        JobDebugInfo debug_info,
                        Option<ThreadSignal*> thread_signal = None);

inline JobHandle
_kick_single_job(JobPriority priority,
                 JobDesc desc,
                 JobDebugInfo debug_info,
                 Option<ThreadSignal*> thread_signal = None)
{
  return _kick_jobs(priority, &desc, 1, debug_info, thread_signal);
}

inline void
blocking_kick_job_descs(JobPriority priority,
                        JobDesc* jobs,
                        size_t count,
                        JobDebugInfo debug_info)
{
  ThreadSignal signal = init_thread_signal();
  _kick_jobs(priority, jobs, count, debug_info, &signal);
  wait_for_thread_signal(&signal);
}

inline void
_blocking_kick_single_job(JobPriority priority,
                          JobDesc desc,
                          JobDebugInfo debug_info)
{
  blocking_kick_job_descs(priority, &desc, 1, debug_info);
}


template <typename F>
void closure_callback(void* f)
{
  (*(F*)f)();
}

template <typename F>
inline JobDesc
init_job_desc_from_closure(F func)
{
  JobDesc ret = {0};
  static_assert(sizeof(F) <= sizeof(ret.entry.params));

  u8* aligned = align_ptr(ret.entry.params, alignof(F));
  memcpy(aligned, &func, sizeof(func));

  ret.entry.param_offset = u8(uintptr_t(aligned) - uintptr_t(ret.entry.params));
  ret.entry.func_ptr = &closure_callback<F>;
  return ret;
}

#define kick_job_descs(priority, job_descs, count, ...) _kick_jobs(priority, job_descs, count, JOB_DEBUG_INFO_STRUCT, __VA_ARGS__)
#define kick_closure_job(priority, closure) _kick_single_job(priority, init_job_desc_from_closure(closure), JOB_DEBUG_INFO_STRUCT)
#define kick_job(priority, function_call) kick_closure_job(priority, [=]() { function_call; })
#define blocking_kick_closure_job(priority, closure) _blocking_kick_single_job(priority, init_job_desc_from_closure(closure), JOB_DEBUG_INFO_STRUCT)
#define blocking_kick_job(priority, function_call) blocking_kick_closure_job(priority, [&]() { function_call; })

template <typename F>
void yield_async(F func)
{
  JobHandle counter = kick_closure_job(kJobPriorityLow, func);
  yield_to_counter(counter);
}
