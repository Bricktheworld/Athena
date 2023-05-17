#pragma once
#include "ring_buffer.h"
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

	// NOTE(Brandon): Technically if you have more than one param
	// this fiber system won't actually account for that. Maybe 
	// be more comprehensive in the future? Or maybe we just say fibers
	// should pull from stack :shrug:
	uintptr_t rcx = 0;

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

Fiber init_fiber(void* stack, size_t stack_size, void* proc, uintptr_t param);
extern "C" void launch_fiber(Fiber* fiber);
extern "C" void resume_fiber(Fiber* fiber, void* stack_high);
extern "C" void save_to_fiber(Fiber* out, void* stack_high);
extern "C" void* get_rsp();

typedef void (*JobEntry)(uintptr_t);

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

typedef u64 JobCounterID;

void yield_to_counter(JobCounterID counter);

struct JobDebugInfo 
{
	const char* file = nullptr;
	int line = 0;
};
#define JOB_DEBUG_INFO_STRUCT JobDebugInfo{__FILE__, __LINE__}

#if 0 
enum JobPtrPolicy : u8
{
	JOB_POLICY_READ,
	JOB_POLICY_WRITE,

	// TODO(Brandon): Eventually we'll want to allow multiple jobs to write
	// to the same ptr at the same time through a SpinLock.
	JOB_POLICY_SHARED,

	JOB_POLICY_COUNT,
};
#endif

struct Job
{
	JobEntry entry = nullptr;
	uintptr_t param = 0;

	JobCounterID completion_signal = 0;

	JobDebugInfo debug_info = {0};
};

struct WorkingJob
{
	Job job;
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
	JobCounterID id = 0;
	WorkingJobQueue waiting_jobs = {0};
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

	// TODO(Brandon): We really want this to be a hashmap from
	// JobCounterID to actual JobCounter.
	SpinLocked<Array<JobCounter>> job_counters;

	SpinLocked<WorkingJobQueue> working_jobs_queue;

	volatile JobCounterID current_job_counter_id = 1;

	bool should_exit = 0;
};

enum JobPriority : u8
{
	JOB_PRIORITY_HIGH,
	JOB_PRIORITY_MEDIUM,
	JOB_PRIORITY_LOW,

	JOB_PRIORITY_COUNT,
};

JobSystem* init_job_system(MEMORY_ARENA_PARAM, size_t job_queue_size);

// These must be called _inside_ of a job.
JobSystem* get_job_system();
void kill_job_system(JobSystem* job_system);

Array<Thread> spawn_job_system_workers(MEMORY_ARENA_PARAM, JobSystem* job_system);

JobCounterID _kick_jobs(JobPriority priority,
                        Job* jobs,
                        size_t count,
                        JobDebugInfo debug_info,
                        JobSystem* job_system = nullptr);

#define kick_jobs(priority, jobs, count, ...) _kick_jobs(priority, jobs, count, JOB_DEBUG_INFO_STRUCT, __VA_ARGS__)
#define kick_job(priority, job, ...) kick_jobs(priority, &job, 1, __VA_ARGS__)
