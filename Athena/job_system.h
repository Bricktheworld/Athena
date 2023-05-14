#pragma once
#include "ring_buffer.h"
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

static_assert(offsetof(Fiber, stack_low) == 0x100);
static_assert(offsetof(Fiber, stack_high) == 0x108);
static_assert(offsetof(Fiber, fiber_local) == 0x110);
static_assert(offsetof(Fiber, deallocation_stack) == 0x118);

Fiber init_fiber(void* stack, size_t stack_size, void* proc, uintptr_t param);
extern "C" void launch_fiber(Fiber* fiber);
extern "C" void resume_fiber(Fiber* fiber, void* stack_high);
extern "C" void save_to_fiber(Fiber* out, void* stack_high);
//#define yield job_system_yield_fiber()

typedef void (*JobEntry)(uintptr_t);

#define STACK_SIZE KiB(16)
// Literally just a hunk of memory lol.
struct JobStack
{
	alignas(16) byte memory[STACK_SIZE];
};

typedef u64 JobCounterID;

void yield_to_counter(JobCounterID counter);

struct JobDebugInfo 
{
	const char* file = nullptr;
	int line = 0;
};
#define JOB_DEBUG_INFO_STRUCT JobDebugInfo{__FILE__, __LINE__}

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

	SpinLocked<Pool<JobStack>> job_stack_allocator;

	SpinLocked<Pool<WorkingJob>> working_job_allocator;
	SpinLocked<Array<JobCounter>> job_counters;

	SpinLocked<WorkingJobQueue> working_jobs_queue;

	volatile JobCounterID current_job_counter_id = 1;

	SpinLock lock;
};

enum JobPriority : u8
{
	JOB_PRIORITY_HIGH,
	JOB_PRIORITY_MEDIUM,
	JOB_PRIORITY_LOW,

	JOB_PRIORITY_COUNT,
};

JobSystem* init_job_system(MEMORY_ARENA_PARAM, size_t job_queue_size);

void spawn_job_system_workers(JobSystem* job_system);

JobCounterID _kick_jobs(JobPriority priority,
                       Job* jobs,
                       size_t count,
                       JobDebugInfo debug_info,
                       JobSystem* job_system = nullptr);

#define kick_jobs(priority, jobs, count, ...) _kick_jobs(priority, jobs, count, JOB_DEBUG_INFO_STRUCT, __VA_ARGS__)
#define kick_job(priority, job, ...) kick_jobs(priority, &job, 1, __VA_ARGS__)


#if 0
#pragma section(".text")
__declspec(allocate(".text"))
static u8 save_to_fiber_x64[] =
{
	0x4C, 0x8B, 0x04, 0x24,              // mov         r8,qword ptr [rsp]
	0x4C, 0x89, 0x01,                    // mov         qword ptr [rcx],r8
	0x4C, 0x8D, 0x44, 0x24, 0x08,        // lea         r8,[rsp+8]
	0x4C, 0x89, 0x41, 0x08,              // mov         qword ptr [rcx+8],r8
	0x48, 0x89, 0x59, 0x10,              // mov         qword ptr [rcx+10h],rbx
	0x48, 0x89, 0x69, 0x18,              // mov         qword ptr [rcx+18h],rbp
	0x4C, 0x89, 0x61, 0x20,              // mov         qword ptr [rcx+20h],r12
	0x4C, 0x89, 0x69, 0x28,              // mov         qword ptr [rcx+28h],r13
	0x4C, 0x89, 0x71, 0x30,              // mov         qword ptr [rcx+30h],r14
	0x4C, 0x89, 0x79, 0x38,              // mov         qword ptr [rcx+38h],r15
	0x48, 0x89, 0x79, 0x40,              // mov         qword ptr [rcx+40h],rdi
	0x48, 0x89, 0x71, 0x48,              // mov         qword ptr [rcx+48h],rsi
	0x0F, 0x11, 0x71, 0x50,              // movups      xmmword ptr [rcx+50h],xmm6
	0x0F, 0x11, 0x79, 0x60,              // movups      xmmword ptr [rcx+60h],xmm7
	0x44, 0x0F, 0x11, 0x41, 0x70,        // movups      xmmword ptr [rcx+70h],xmm8
	0x44, 0x0F, 0x11, 0x89, 0x80, 0x00,  // movups      xmmword ptr [rcx+0000000000000080h],xmm9
	0x00, 0x00,                          //
	0x44, 0x0F, 0x11, 0x91, 0x90, 0x00,  // movups      xmmword ptr [rcx+0000000000000090h],xmm10
	0x00, 0x00,                          //
	0x44, 0x0F, 0x11, 0x99, 0xA0, 0x00,  // movups      xmmword ptr [rcx+00000000000000A0h],xmm11
	0x00, 0x00,                          //
	0x44, 0x0F, 0x11, 0xA1, 0xB0, 0x00,  // movups      xmmword ptr [rcx+00000000000000B0h],xmm12
	0x00, 0x00,                          //
	0x44, 0x0F, 0x11, 0xA9, 0xC0, 0x00,  // movups      xmmword ptr [rcx+00000000000000C0h],xmm13
	0x00, 0x00,                          //
	0x44, 0x0F, 0x11, 0xB1, 0xD0, 0x00,  // movups      xmmword ptr [rcx+00000000000000D0h],xmm14
	0x00, 0x00,                          //
	0x44, 0x0F, 0x11, 0xB9, 0xE0, 0x00,  // movups      xmmword ptr [rcx+00000000000000E0h],xmm15
	0x00, 0x00,                          //
	0x33, 0xC0,                          // xor         eax,eax
	0xC3,                                // ret
};

static void (*save_to_fiber)(Fiber*) = (void (*)(Fiber*))(u8*)save_to_fiber_x64;
#endif

