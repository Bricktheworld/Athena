#include "job_system.h"
#include "context.h"
#include "profiling.h"

static JobSystem* g_job_system = nullptr;

thread_local u64 tls_worker_fiber_id = 0;
thread_local u64 tls_job_fiber_id = 0;

#if 0
extern "C" void on_fiber_enter()
{
  if (tls_worker_fiber_id == 0)
    return;

  profiler::end_switch_to_fiber(tls_worker_fiber_id);
}

extern "C" void on_fiber_exit()
{
  if (tls_worker_fiber_id == 0 || tls_job_fiber_id == 0)
    return;

  profiler::begin_switch_to_fiber(tls_worker_fiber_id, tls_job_fiber_id);
}
#endif

Fiber
init_fiber(void* stack, size_t stack_size, void* proc, void* param)
{
  ASSERT(stack_size >= 1);
  ASSERT(stack != nullptr);
  Fiber ret = {0};

  uintptr_t stack_high       = (uintptr_t)stack + stack_size;
  ASSERT((stack_high & 0xF) == 0x0);

  ret.rip                = proc;
  ret.rsp                = reinterpret_cast<void*>(stack_high);
  ret.stack_high         = ret.rsp;
  ret.stack_low          = stack;
  ret.deallocation_stack = stack;
  ret.rcx                = param;

  return ret;
}

static JobQueue
init_job_queue(MEMORY_ARENA_PARAM, size_t size)
{
  JobQueue ret = {0};
  size *= sizeof(JobDesc);
  ret.queue = init_ring_buffer(MEMORY_ARENA_FWD, alignof(JobDesc), size);

  return ret;
}

static void
enqueue_jobs(JobQueue* job_queue, const JobDesc* jobs, size_t count)
{
  spin_acquire(&job_queue->lock);
  defer { spin_release(&job_queue->lock); };

  ring_buffer_push(&job_queue->queue, jobs, count * sizeof(JobDesc));
}

static bool
dequeue_job(JobQueue* job_queue, JobDesc* out)
{
  ASSERT(out != nullptr);
  spin_acquire(&job_queue->lock);
  defer { spin_release(&job_queue->lock); };

  return try_ring_buffer_pop(&job_queue->queue, sizeof(JobDesc), out);
}

static void
enqueue_working_job(WorkingJobQueue* queue, WorkingJob* job)
{
  ASSERT(job->next == nullptr);

  if (queue->head == nullptr) 
  {
    queue->head = queue->tail = job;
    return;
  }

  ASSERT(queue->tail != nullptr);

  queue->tail = queue->tail->next = job;
}

static void
enqueue_working_jobs(WorkingJobQueue* queue, WorkingJobQueue* jobs) 
{
  if (queue->head == nullptr) 
  {
    *queue = *jobs;
    return;
  }

  ASSERT(queue->tail != nullptr);

  queue->tail->next = jobs->head;
  queue->tail = jobs->tail;
}

static bool
dequeue_working_job(WorkingJobQueue* queue, WorkingJob** out)
{
  ASSERT(out != nullptr);

  if (queue->head == nullptr)
    return false;

  *out = queue->head;
  queue->head = queue->head->next;

  if (queue->head == nullptr)
  {
    queue->tail = nullptr;
  }

  (*out)->next = nullptr;

  return true;
}

enum JobType : u8
{
  JOB_TYPE_INVALID,
  JOB_TYPE_LAUNCH,
  JOB_TYPE_WORKING,

  JOB_TYPE_COUNT,
};

static JobType
wait_for_next_job(JobSystem* job_system, JobDesc* job_out, WorkingJob** working_job_out)
{
  while (!job_system->should_exit)
  {
    if (ACQUIRE(&job_system->working_jobs_queue, auto* q) {
      return dequeue_working_job(q, working_job_out); 
    })
      return JOB_TYPE_WORKING;

    if (dequeue_job(&job_system->high_priority, job_out))
      return JOB_TYPE_LAUNCH;

    if (dequeue_job(&job_system->medium_priority, job_out))
      return JOB_TYPE_LAUNCH;
  }
  return JOB_TYPE_INVALID;
}

static JobType
wait_for_async_job(JobSystem* job_system, JobDesc* job_out)
{
  while (!job_system->should_exit)
  {
    if (dequeue_job(&job_system->low_priority, job_out))
      return JOB_TYPE_LAUNCH;
  }

  return JOB_TYPE_INVALID;
}

JobSystem*
init_job_system(MEMORY_ARENA_PARAM, size_t job_queue_size)
{
  ASSERT(g_job_system == nullptr);
  JobSystem* ret = push_memory_arena<JobSystem>(MEMORY_ARENA_FWD);
  zero_memory(ret, sizeof(JobSystem));

  ret->high_priority = init_job_queue(MEMORY_ARENA_FWD, job_queue_size);
  ret->medium_priority = init_job_queue(MEMORY_ARENA_FWD, job_queue_size);
  ret->low_priority = init_job_queue(MEMORY_ARENA_FWD, job_queue_size);

  ret->job_stack_allocator = init_pool<JobStack>(MEMORY_ARENA_FWD, job_queue_size / 2);

  ret->working_job_allocator = init_pool<WorkingJob>(MEMORY_ARENA_FWD, job_queue_size / 2);
  ret->job_counters = init_hash_table<JobHandle, JobCounter>(MEMORY_ARENA_FWD, 128);

  g_job_system = ret;

  return ret;
}

thread_local Fiber* tls_fiber = nullptr;

enum YieldParamType : u8
{
  YIELD_PARAM_JOB_COUNTER,
};

struct YieldParam
{
  YieldParamType type = YIELD_PARAM_JOB_COUNTER;
  union
  {
    JobHandle job_counter;
  };
};

thread_local YieldParam tls_yield_param;

void
yield_to_counter(JobHandle counter)
{
  ASSERT(tls_fiber != nullptr);
  tls_yield_param.job_counter = counter;
  tls_yield_param.type = YIELD_PARAM_JOB_COUNTER;

  save_to_fiber(tls_fiber, tls_fiber->stack_high);
}

static void
signal_job_counter(JobSystem* job_system, JobHandle signal)
{
  Option<WorkingJobQueue> woken_jobs = ACQUIRE(&job_system->job_counters, auto* counters) -> Option<WorkingJobQueue>
  {
    JobCounter* counter = unwrap(hash_table_find(counters, signal));
    ASSERT(counter->value > 0);
    auto value = InterlockedDecrement(&counter->value);
    if (value == 0)
    {
      WorkingJobQueue waiting = counter->waiting_jobs;
      if (counter->completion_signal)
      {
        notify_all_thread_signal(unwrap(counter->completion_signal));
      }
      hash_table_erase(counters, signal);
      return waiting;
    }

    return None;
  };

  if (!woken_jobs)
    return;

  ACQUIRE(&job_system->working_jobs_queue, auto* working_jobs_queue)
  {
    enqueue_working_jobs(working_jobs_queue, &unwrap(woken_jobs));
  };
}

static void
working_job_wait_for_counter(JobSystem* job_system,
                             JobHandle signal,
                             WorkingJob* working_job)
{
  bool res = ACQUIRE(&job_system->job_counters, auto* job_counters)
  {
    auto maybe_counter = hash_table_find(job_counters, signal);
    if (!maybe_counter)
      return false;

    JobCounter* counter = unwrap(maybe_counter);
    enqueue_working_job(&counter->waiting_jobs, working_job);
    return true;
  };

  if (res)
    return;

  ACQUIRE(&job_system->working_jobs_queue, auto* working_jobs_queue)
  {
    enqueue_working_job(working_jobs_queue, working_job);
  };
}

static void
finish_job(JobSystem* job_system, JobStack* job_stack, JobHandle completion_signal)
{
  ACQUIRE(&job_system->job_stack_allocator, auto* allocator) {
    pool_free(allocator, job_stack);
  };

  signal_job_counter(job_system, completion_signal);
}

static void
yield_working_job(JobSystem* job_system, WorkingJob* working_job)
{
  switch (tls_yield_param.type)
  {
    case YIELD_PARAM_JOB_COUNTER: 
    {
      working_job_wait_for_counter(job_system, tls_yield_param.job_counter, working_job);
      break;
    }
    default:
      UNREACHABLE;
  }
}

static void
launch_job(JobSystem* job_system, JobDesc job, bool can_yield = true)
{
  JobStack* stack = ACQUIRE(&job_system->job_stack_allocator, auto* allocator)
  {
    return pool_alloc(allocator);
  };

  MemoryArena scratch_arena = {0};
  scratch_arena.start = reinterpret_cast<uintptr_t>(stack->scratch_buf);
  scratch_arena.size = DEFAULT_SCRATCH_SIZE;
  scratch_arena.pos = scratch_arena.start;

  Context ctx = init_context(scratch_arena);

  Fiber fiber = init_fiber(stack->memory, STACK_SIZE, job.entry.func_ptr, job.entry.params + job.entry.param_offset);
  tls_fiber = &fiber;

  auto* stack_high_before = fiber.stack_high;
  ASSERT(fiber.rip != nullptr);
  push_context(ctx);

  tls_job_fiber_id = job.completion_signal;

#if 0
  profiler::register_fiber(tls_job_fiber_id);
  profiler::begin_switch_to_fiber(tls_worker_fiber_id, tls_job_fiber_id);
#endif

  launch_fiber(&fiber);

#if 0
  profiler::end_switch_to_fiber(tls_job_fiber_id);
#endif

  ctx = pop_context();
  ASSERT(fiber.stack_high == stack_high_before);

  // The job actually finished, means we can recycle everything.
  if (!fiber.yielded)
  {
#if 0
    profiler::unregister_fiber(job.completion_signal);
#endif
    finish_job(job_system, stack, job.completion_signal);
    return;
  }

  ASSERT(can_yield);

  WorkingJob* working_job = ACQUIRE(&job_system->working_job_allocator, auto* allocator)
  {
    return pool_alloc(allocator);
  };

  working_job->job = job;
  working_job->fiber = fiber;
  working_job->stack = stack;
  working_job->next = nullptr;
  working_job->ctx = ctx;
  yield_working_job(job_system, working_job);
}

static void
resume_working_job(JobSystem* job_system, WorkingJob* working_job)
{
  push_context(working_job->ctx);
  tls_fiber = &working_job->fiber;

  tls_job_fiber_id = working_job->job.completion_signal;
#if 0
  profiler::begin_switch_to_fiber(tls_worker_fiber_id, tls_job_fiber_id);
#endif

  resume_fiber(&working_job->fiber, working_job->fiber.stack_high);

#if 0
  profiler::end_switch_to_fiber(tls_job_fiber_id);
#endif

  working_job->ctx = pop_context();

  // The job actually finished, means we can recycle everything.
  if (!working_job->fiber.yielded)
  {
    finish_job(job_system, working_job->stack, working_job->job.completion_signal);
    ACQUIRE(&job_system->working_job_allocator, auto* allocator)
    {
      pool_free(allocator, working_job);
    };
    return;
  }

  working_job->next = nullptr;
  yield_working_job(job_system, working_job);
}

struct JobWorkerThreadParams
{
  JobSystem* job_system = nullptr;
};

static u32
job_worker(void* param)
{
  tls_worker_fiber_id = (u64)param;
#if 0
  profiler::register_fiber(tls_worker_fiber_id);
#endif

  while (!g_job_system->should_exit)
  {
    JobDesc job = {0};
    WorkingJob* working_job = nullptr;
    JobType type = wait_for_next_job(g_job_system, &job, &working_job);
    switch(type)
    {
      case JOB_TYPE_LAUNCH:
      {
        launch_job(g_job_system, job);
      } break;
      case JOB_TYPE_WORKING:
      {
        resume_working_job(g_job_system, working_job);
      } break;
      case JOB_TYPE_INVALID:
      {
        return 0;
      } break;
      default:
        UNREACHABLE;
    }
  }
  return 0;
}

static u32
async_worker(void* param)
{
  tls_worker_fiber_id = (u64)param;
#if 0
  profiler::register_fiber(tls_worker_fiber_id);
#endif
  while(!g_job_system->should_exit)
  {
    JobDesc job = {0};
    JobType type = wait_for_async_job(g_job_system, &job);
    if (type == JOB_TYPE_INVALID)
      return 0;

    // The async worker shouldn't really have any yields.
    ASSERT(type == JOB_TYPE_LAUNCH);

    launch_job(g_job_system, job, false);
  }

  return 0;
}

Array<Thread>
spawn_job_system_workers(MEMORY_ARENA_PARAM, JobSystem* job_system)
{
  wchar_t name[128];
  s32 num_physical_cores = get_num_physical_cores();
  // TODO(Brandon): Scale these appropriately. This entails making the async workers
  // not spin and also programmatically fetching the number of _physical_ cores without
  // hyperthreading
  u32 worker_threads = 1; // max(1, num_physical_cores - 6);
  // These are for the async threads... they will be locked to a single core shared with the OS
  u32 async_threads = 1;

  Array ret = init_array<Thread>(MEMORY_ARENA_FWD, worker_threads);

  u64 fiber_id = -1;

  for (u32 i = 0; i < worker_threads; i++)
  {
    MemoryArena thread_scratch_arena = sub_alloc_memory_arena(MEMORY_ARENA_FWD, DEFAULT_SCRATCH_SIZE);
    Thread thread = create_thread(thread_scratch_arena, KiB(16), &job_worker, (void*)fiber_id, i);
    swprintf_s(name, 128, L"JobSystem Worker %d", i);
    set_thread_name(&thread, name);

    *array_add(&ret) = thread;
    fiber_id--;
  }

  u8 async_core_index = worker_threads;
  for (u32 i = 0; i < async_threads; i++)
  {
    MemoryArena thread_scratch_arena = sub_alloc_memory_arena(MEMORY_ARENA_FWD, KiB(4));
    Thread thread = create_thread(thread_scratch_arena, KiB(16), &async_worker, (void*)fiber_id, async_core_index);
    swprintf_s(name, 128, L"JobSystem Async Worker %d", i);
    set_thread_name(&thread, name);
    fiber_id--;
  }

  return ret;
}

bool
job_has_completed(JobHandle handle, JobSystem* job_system)
{
  if (job_system == nullptr)
  {
    job_system = g_job_system;
  }

  ASSERT(job_system != nullptr);

  return ACQUIRE(&job_system->job_counters, auto* counters) -> bool
  {
    return !hash_table_find(counters, handle);
  };
}

static JobQueue*
get_queue(JobSystem* job_system, JobPriority priority)
{
  JobQueue* ret = nullptr;
  switch(priority)
  {
    case kJobPriorityHigh:   ret = &job_system->high_priority; break;
    case kJobPriorityMedium: ret = &job_system->medium_priority; break;
    case kJobPriorityLow:    ret = &job_system->low_priority; break;
  }
  ASSERT(ret != nullptr);

  return ret;
}

static JobHandle
get_next_job_counter_id(JobSystem* job_system)
{
  return InterlockedIncrement(&job_system->current_job_counter_id);
}

JobHandle
_kick_jobs(JobPriority priority,
          JobDesc* jobs,
          size_t count,
          JobDebugInfo debug_info,
          Option<ThreadSignal*> thread_signal)
{
  ASSERT(g_job_system != nullptr);

  JobHandle ret = ACQUIRE(&g_job_system->job_counters, auto* job_counters)
  {
    JobHandle counter_id = get_next_job_counter_id(g_job_system);
    JobCounter* counter = hash_table_insert(job_counters, counter_id);
    counter->id = counter_id;
    counter->value = count;
    counter->waiting_jobs = { 0 };
    counter->completion_signal = thread_signal;
    return counter->id;
  };

  for (size_t i = 0; i < count; i++)
  {
    ASSERT(jobs[i].entry.func_ptr != nullptr);
    jobs[i].completion_signal = ret;
    jobs[i].debug_info = debug_info;
  }

  JobQueue* queue = get_queue(g_job_system, priority);

  enqueue_jobs(queue, jobs, count);

  return ret;
}

JobSystem*
get_job_system()
{
  ASSERT(g_job_system != nullptr);
  return g_job_system;
}

void
kill_job_system(JobSystem* job_system)
{
  job_system->should_exit = true;
}
