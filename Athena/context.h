#pragma once
#include "memory/memory.h"

#define DEFAULT_SCRATCH_SIZE KiB(16)

struct Context
{
  MemoryArena scratch_arena = {0};
  Context* prev = nullptr;
};

Context init_context(MemoryArena arena);
void push_context(Context ctx);
Context pop_context();

MemoryArena alloc_scratch_arena();
void free_scratch_arena(MEMORY_ARENA_PARAM);

uintptr_t* context_get_scratch_arena_pos_ptr();

#define USE_SCRATCH_ARENA() \
  MemoryArena scratch_arena = alloc_scratch_arena(); \
  defer { free_scratch_arena(&scratch_arena); }

#define SCRATCH_ARENA_PASS &scratch_arena

