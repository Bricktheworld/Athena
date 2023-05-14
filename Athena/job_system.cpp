#include "job_system.h"

Fiber
init_fiber(void* stack, size_t stack_size, void* proc, uintptr_t param)
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
	size *= sizeof(Job);
	ret.queue = init_ring_buffer(MEMORY_ARENA_FWD, alignof(Job), size);

	return ret;
}

static void
enqueue_jobs(JobQueue* job_queue, const Job* jobs, size_t count)
{
	spin_acquire(&job_queue->lock);
	defer { spin_release(&job_queue->lock); };

	ring_buffer_push(&job_queue->queue, jobs, count * sizeof(Job));
}

static bool
dequeue_job(JobQueue* job_queue, Job* out)
{
	ASSERT(out != nullptr);
	spin_acquire(&job_queue->lock);
	defer { spin_release(&job_queue->lock); };

	return try_ring_buffer_pop(&job_queue->queue, sizeof(Job), out);
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

enum JobType
{
	JOB_TYPE_LAUNCH,
	JOB_TYPE_WORKING,
};

static JobType
wait_for_next_job(JobSystem* job_system, Job* job_out, WorkingJob** working_job_out)
{
	for (;;)
	{
		if (ACQUIRE(&job_system->working_jobs_queue, auto* q) {
			return dequeue_working_job(q, working_job_out); 
		})
			return JOB_TYPE_WORKING;

		if (dequeue_job(&job_system->high_priority, job_out))
			return JOB_TYPE_LAUNCH;

		if (dequeue_job(&job_system->medium_priority, job_out))
			return JOB_TYPE_LAUNCH;

		if (dequeue_job(&job_system->low_priority, job_out))
			return JOB_TYPE_LAUNCH;
	}
}

JobSystem*
init_job_system(MEMORY_ARENA_PARAM, size_t job_queue_size)
{
	JobSystem* ret = push_memory_arena<JobSystem>(MEMORY_ARENA_FWD);
	zero_memory(ret, sizeof(JobSystem));

	ret->high_priority = init_job_queue(MEMORY_ARENA_FWD, job_queue_size);
	ret->medium_priority = init_job_queue(MEMORY_ARENA_FWD, job_queue_size);
	ret->low_priority = init_job_queue(MEMORY_ARENA_FWD, job_queue_size);

	ret->job_stack_allocator = init_pool<JobStack>(MEMORY_ARENA_FWD, job_queue_size / 2);

	ret->working_job_allocator = init_pool<WorkingJob>(MEMORY_ARENA_FWD, job_queue_size / 2);
	ret->job_counters = init_array<JobCounter>(MEMORY_ARENA_FWD, 100);

	return ret;
}

thread_local JobSystem* tls_job_system = nullptr;
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
		JobCounterID job_counter;
	};
};

thread_local YieldParam tls_yield_param;

void
yield_to_counter(JobCounterID counter)
{
	ASSERT(tls_fiber != nullptr);
	tls_yield_param.job_counter = counter;
	tls_yield_param.type = YIELD_PARAM_JOB_COUNTER;
	save_to_fiber(tls_fiber, tls_fiber->stack_high);
}

static void
signal_job_counter(JobSystem* job_system, JobCounterID signal)
{
	Option<WorkingJobQueue> woken_jobs = ACQUIRE(&job_system->job_counters, Array<JobCounter>* counters) -> Option<WorkingJobQueue>
	{
		size_t index = unwrap(array_find(counters, it->id == signal));
		JobCounter* counter = array_at(counters, index);
		ASSERT(counter->value > 0);
		auto value = InterlockedDecrement(&counter->value);
		if (value == 0)
		{
			WorkingJobQueue waiting = counter->waiting_jobs;
			array_remove(counters, index);
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
                             JobCounterID signal,
                             WorkingJob* working_job)
{
	bool res = ACQUIRE(&job_system->job_counters, auto* job_counters)
	{
		auto maybe_counter = array_find_value(job_counters, it->id == signal);
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
finish_job(JobSystem* job_system, JobStack* job_stack, JobCounterID completion_signal)
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
launch_job(JobSystem* job_system, Job job)
{
	JobStack* stack = ACQUIRE(&job_system->job_stack_allocator, auto* allocator)
	{
		return pool_alloc(allocator);
	};

	Fiber fiber = init_fiber(stack->memory, STACK_SIZE, job.entry, job.param);
	tls_fiber = &fiber;

	auto* stack_high_before = fiber.stack_high;
	ASSERT(fiber.rip != nullptr);
	launch_fiber(&fiber);
	ASSERT(fiber.stack_high == stack_high_before);

	// The job actually finished, means we can recycle everything.
	if (!fiber.yielded)
	{
		finish_job(job_system, stack, job.completion_signal);
		return;
	}

	WorkingJob* working_job = ACQUIRE(&job_system->working_job_allocator, auto* allocator)
	{
		return pool_alloc(allocator);
	};

	working_job->job = job;
	working_job->fiber = fiber;
	working_job->stack = stack;
	working_job->next = nullptr;
	yield_working_job(job_system, working_job);
}

static void
resume_working_job(JobSystem* job_system, WorkingJob* working_job)
{
	resume_fiber(&working_job->fiber, working_job->fiber.stack_high);

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

static DWORD
job_worker(LPVOID param) 
{
	JobSystem* job_system = tls_job_system = reinterpret_cast<JobSystem*>(param);
	for (;;)
	{
		Job job = {0};
		WorkingJob* working_job = nullptr;
		JobType type = wait_for_next_job(job_system, &job, &working_job);
		switch(type)
		{
			case JOB_TYPE_LAUNCH:
			{
				launch_job(job_system, job);
			} break;
			case JOB_TYPE_WORKING:
			{
				resume_working_job(job_system, working_job);
			} break;
			default:
				UNREACHABLE;
		}
	}
}

void
spawn_job_system_workers(JobSystem* job_system)
{
	int count = get_num_physical_cores();
	for (int i = 0; i < count - 1; i++)
	{
		create_thread(KiB(16), &job_worker, job_system, i);
	}
}

static JobQueue*
get_queue(JobSystem* job_system, JobPriority priority)
{
	JobQueue* ret = nullptr;
	switch(priority)
	{
		case JOB_PRIORITY_HIGH:   ret = &job_system->high_priority; break;
		case JOB_PRIORITY_MEDIUM: ret = &job_system->medium_priority; break;
		case JOB_PRIORITY_LOW:    ret = &job_system->low_priority; break;
	}
	ASSERT(ret != nullptr);

	return ret;
}

JobCounterID
_kick_jobs(JobPriority priority,
          Job* jobs,
          size_t count,
          JobDebugInfo debug_info,
          JobSystem* job_system)
{
	if (job_system == nullptr)
	{
		job_system = tls_job_system;
	}

	ASSERT(job_system != nullptr);

	JobCounter* counter = ACQUIRE(&job_system->job_counters, auto* job_counters)
	{
		return array_add(job_counters);
	};

	counter->id = InterlockedIncrement(&job_system->current_job_counter_id);
	counter->value = count;
	counter->waiting_jobs = { 0 };

	JobCounterID ret = counter->id;
	for (size_t i = 0; i < count; i++)
	{
		ASSERT(jobs[i].entry != nullptr);
		jobs[i].completion_signal = ret;
		jobs[i].debug_info = debug_info;
	}

	JobQueue* queue = get_queue(job_system, priority);

	enqueue_jobs(queue, jobs, count);

	return ret;
}
