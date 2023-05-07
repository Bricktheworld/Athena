#include "job_system.h"

Fiber
init_fiber(void* stack, size_t stack_size, void* proc, uintptr_t param)
{
	ASSERT(stack_size >= 1);
	ASSERT(stack != nullptr);
	Fiber ret = {0};

	ret.rip = proc;
	ret.rsp = reinterpret_cast<void*>((uintptr_t)stack + stack_size);
	ret.rcx = param;

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

JobSystem*
init_job_system(size_t job_queue_size)
{
	size_t arena_size = sizeof(JobSystem) +
	                    job_queue_size * 3 * sizeof(Job) +
	                    job_queue_size * (sizeof(SleepingJob) + sizeof(SleepingJob*)) +
	                    job_queue_size * (sizeof(JobStack) + sizeof(JobStack*)) +
	                    50 * (sizeof(JobCounter) + sizeof(JobCounter*)) + 8;

	MemoryArena memory_arena = alloc_memory_arena(arena_size);

	JobSystem* ret = push_memory_arena<JobSystem>(&memory_arena);
	zero_memory(ret, sizeof(JobSystem));

	ret->memory_arena = memory_arena;

	ret->high_priority = init_job_queue(&memory_arena, job_queue_size);
	ret->medium_priority = init_job_queue(&memory_arena, job_queue_size);
	ret->low_priority = init_job_queue(&memory_arena, job_queue_size);

	ret->sleeping_jobs = init_pool_allocator<SleepingJob>(&memory_arena, job_queue_size);
	ret->job_stacks = init_pool_allocator<JobStack>(&memory_arena, job_queue_size);
	ret->job_counters = init_pool_allocator<JobCounter>(&memory_arena, 50);

	return ret;
}

thread_local Fiber* tls_fiber = nullptr;
thread_local void* tls_fiber_rsp = nullptr;

void
job_system_yield_fiber()
{
	ASSERT(tls_fiber != nullptr);
	save_to_fiber(tls_fiber, tls_fiber_rsp);
}

static DWORD
job_worker(LPVOID param) 
{
	JobSystem* job_system = reinterpret_cast<JobSystem*>(param);
	for (;;)
	{
		Job job = {0};
		wait_for_next_job(job_system, &job);

		JobStack* stack = nullptr;
		{
			spin_acquire(&job_system->lock);
			defer { spin_release(&job_system->lock); };

			stack = pool_alloc(&job_system->job_stacks);
		}

		Fiber fiber = init_fiber(stack->memory, STACK_SIZE, job.entry, job.param);
		tls_fiber = &fiber;
		tls_fiber_rsp = fiber.rsp;

		launch_fiber(&fiber);

		while (fiber.yielded)
		{
			dbgln("Fiber yielded.");
			resume_fiber(&fiber, tls_fiber_rsp);
		}

		{
			dbgln("Job complete");

			{
				spin_acquire(&job_system->lock);
				defer { spin_release(&job_system->lock); };
//				if (job.counter != nullptr)
//				{
//					InterlockedDecrement(&job.counter->value);
//				}
	
				pool_free(&job_system->job_stacks, stack);
			}
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

void
destroy_job_system(JobSystem* job_system)
{
	free_memory_arena(&job_system->memory_arena);
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

void
kick_jobs(JobSystem* job_system,
          JobPriority priority,
          Job* jobs,
          size_t count,
          JobCounter** out_counter)
{
	if (out_counter != nullptr)
	{
		spin_acquire(&job_system->lock);
		JobCounter* counter = pool_alloc(&job_system->job_counters);
		spin_release(&job_system->lock);

		counter->value = count;

		*out_counter = counter;

		for (size_t i = 0; i < count; i++)
		{
			jobs[i].counter = counter;
		}
	}

	JobQueue* queue = get_queue(job_system, priority);

	enqueue_jobs(queue, jobs, count);
}

void
wait_for_next_job(JobSystem* job_system, Job* out)
{
	for (;;)
	{
		if (dequeue_job(&job_system->high_priority, out))
			break;

		if (dequeue_job(&job_system->medium_priority, out))
			break;

		if (dequeue_job(&job_system->low_priority, out))
			break;
	}
}

void
yield_for_counter(JobSystem* job_system, JobCounter* counter)
{
}
