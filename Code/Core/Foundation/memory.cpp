#include "Core/Foundation/memory.h"
#include "Core/Foundation/context.h"

#include <windows.h>

static void* g_memory_start = NULL;
static uintptr_t g_memory_pos = 0;

void
init_application_memory(size_t heap_size)
{
  ASSERT(g_memory_start == NULL);
  g_memory_start = VirtualAlloc(0, heap_size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
  ASSERT(g_memory_start != nullptr);
  g_memory_pos = (uintptr_t)g_memory_start;
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
  ret.start = g_memory_pos; // double_ended_push(&g_game_stack, DOUBLE_ENDED_LOWER, size);
  ret.pos = ret.start;
  ret.size = size;
  ret.use_ctx_pos = false;

  g_memory_pos += size;

  return ret;
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