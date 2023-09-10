#include "memory.h"
#include "../context.h"
#include <windows.h>

static void* g_memory_start = NULL;

enum struct MemoryLocation : u8
{
  GAME_MEM,
  GAME_VRAM,

  DEBUG_MEM,
  DEBUG_VRAM,
};

struct DoubleEndedStack
{
  uintptr_t upper = 0x0;
  uintptr_t lower = 0x0;

  uintptr_t start = 0x0;
  uintptr_t end = 0x0;

  uintptr_t last_low_allocation = 0x0;
  uintptr_t last_high_allocation = 0x0;
};

DoubleEndedStack
init_double_ended(void* start, size_t size)
{
  uintptr_t start_addr = align_address(reinterpret_cast<uintptr_t>(start), 8);

  size = ALIGN_POW2(size, sizeof(uintptr_t));

  DoubleEndedStack ret = {0};
  ret.start = start_addr;

  ret.end = ret.start + size;

  ret.lower = ret.start;
  ret.upper = ret.end;

  ret.last_low_allocation = ret.start;
  ret.last_high_allocation = ret.end;

  return ret;
}

enum DoubleEndedStackLocation : u8
{
  DOUBLE_ENDED_LOWER = 0x0,
  DOUBLE_ENDED_UPPER = 0x1,
};

#ifdef DEBUG
#define OVERRUN_PROTECTION_SIZE KiB(1)
#else
#define OVERRUN_PROTECTION_SIZE 0
#endif

static_assert((OVERRUN_PROTECTION_SIZE & 0x7) == 0);

// TODO(Brandon): Technically have to account for overflow if size is unreasonably large.
uintptr_t
double_ended_push(DoubleEndedStack* s, DoubleEndedStackLocation location, size_t size)
{
  ASSERT(size > 0);

  // We're doing this here because we want to store with each push of the stack
  // the last allocated piece of memory. This allows us to do that.
  // We also tack on a bit more memory to detect buffer overruns.
  size_t scratch_size = OVERRUN_PROTECTION_SIZE + sizeof(uintptr_t);
  size += scratch_size;

  size = ALIGN_POW2(size, sizeof(uintptr_t));

  uintptr_t ret = 0x0;

  // For both the low and high regions of the stack, the overrun detection
  // zone will be _after_ the allocated region. In the future, I might
  // even add support for region detection for the previous memory
  // addresses, but for 90% of bugs it's going to be after, so this will
  // work fine for now.
  void* overrun_detection_zone = nullptr;

  // For the previous allocation address, in the high end of the stack
  // it will live 8 bytes _before_ the returned memory address
  // and for the low end it will live _after_ the overrun detection
  // region. In other words, the address will live in the memory region
  // closest to the pointer. It is done this way to make it easy to
  // detect on pop what the previous allocation address was.
  if (location == DOUBLE_ENDED_LOWER)
  {
    ASSERT(s->lower + size <= s->end);
    ret = s->lower;

    overrun_detection_zone = reinterpret_cast<void*>(s->lower + size - scratch_size);

    s->lower += size;

    // Store our allocation location for popping later.
    *reinterpret_cast<uintptr_t*>(s->lower - sizeof(uintptr_t)) = s->last_low_allocation;

    s->last_low_allocation = ret;
  }
  else
  {
    ASSERT(s->upper >= s->start + size);
    overrun_detection_zone = reinterpret_cast<void*>(s->upper - OVERRUN_PROTECTION_SIZE);

    s->upper -= size;

    ret = s->upper + sizeof(uintptr_t);

    *reinterpret_cast<uintptr_t*>(s->upper) = s->last_high_allocation;

    s->last_high_allocation = ret;
  }

  memset(overrun_detection_zone, 0xCC, OVERRUN_PROTECTION_SIZE);

  ASSERT(s->lower <= s->upper);

  return ret;
}

void
double_ended_pop(DoubleEndedStack* s, DoubleEndedStackLocation location, uintptr_t memory)
{
  uintptr_t* prev_allocation = reinterpret_cast<uintptr_t*>(location == DOUBLE_ENDED_LOWER ? 
                                                            s->lower - sizeof(uintptr_t) : s->upper);
  if (location == DOUBLE_ENDED_LOWER)
  {
    zero_memory(reinterpret_cast<void*>(memory), s->lower - memory);
    s->lower = memory;
    s->last_low_allocation = *prev_allocation;
  }
  else
  {
    zero_memory(reinterpret_cast<void*>(memory), s->upper - memory);
    s->upper = memory;
    s->last_high_allocation = *prev_allocation;
  }

  ASSERT(s->lower <= s->upper);
}

//static const MemoryMapEntry MEMORY_MAP[] =
//{
//  {MemoryLocation::GAME_MEM, }
//};

// We'll just allocate a gig of memory LOL
static constexpr size_t HEAP_SIZE = GiB(2);

static DoubleEndedStack g_game_stack = {0};

void
init_application_memory()
{
  ASSERT(g_memory_start == NULL);
  g_memory_start = VirtualAlloc(0, HEAP_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  ASSERT(g_memory_start != nullptr);
  g_game_stack = init_double_ended(g_memory_start, GiB(2));
}

void
destroy_application_memory()
{
  VirtualFree(g_memory_start, 0, MEM_RELEASE);
}

MemoryArena
alloc_memory_arena(size_t size)
{
  MemoryArena ret = {0};
  ret.start = double_ended_push(&g_game_stack, DOUBLE_ENDED_LOWER, size);
  ret.pos = ret.start;
  ret.size = size;
  ret.use_ctx_pos = false;

  return ret;
}

void
free_memory_arena(MEMORY_ARENA_PARAM)
{
  double_ended_pop(&g_game_stack, DOUBLE_ENDED_LOWER, memory_arena->start);
}

void
reset_memory_arena(MEMORY_ARENA_PARAM) 
{
  uintptr_t* pos = memory_arena_pos_ptr(MEMORY_ARENA_FWD);
  ASSERT(*pos >= memory_arena->start);
  *pos = memory_arena->start;
//  zero_memory(reinterpret_cast<void*>(memory_arena->start), memory_arena->size);
}

uintptr_t*
memory_arena_pos_ptr(MEMORY_ARENA_PARAM)
{
  return memory_arena->use_ctx_pos ? context_get_scratch_arena_pos_ptr() : &memory_arena->pos;
}

void*
push_memory_arena_aligned(MEMORY_ARENA_PARAM, size_t size, size_t alignment)
{
  uintptr_t* pos = memory_arena_pos_ptr(MEMORY_ARENA_FWD);
  // TODO(Brandon): We probably want some overrun protection here too.
  uintptr_t memory_start = align_address(*pos, alignment);

  uintptr_t new_pos = memory_start + size;

  ASSERT(new_pos <= memory_arena->start + memory_arena->size);

  *pos = new_pos;

  void* ret = reinterpret_cast<void*>(memory_start);
//  zero_memory(ret, size);

  return ret;
}

MemoryArena
sub_alloc_memory_arena(MEMORY_ARENA_PARAM, size_t size, size_t alignment)
{
  MemoryArena ret = {0};

  ret.start = reinterpret_cast<uintptr_t>(push_memory_arena_aligned(MEMORY_ARENA_FWD, size, alignment));
  ret.pos = ret.start;
  ret.size = size;
  ret.use_ctx_pos = false;

  return ret;
}