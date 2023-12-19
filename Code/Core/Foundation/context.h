#pragma once
#include "Core/Foundation/memory.h"

#define DEFAULT_SCRATCH_SIZE KiB(16)

struct Context
{
  MemoryArena scratch_arena = {0};
  Context* prev = nullptr;
};

FOUNDATION_API Context init_context(MemoryArena arena);
FOUNDATION_API void push_context(Context ctx);
FOUNDATION_API Context pop_context();

FOUNDATION_API MemoryArena alloc_scratch_arena();
FOUNDATION_API void free_scratch_arena(MEMORY_ARENA_PARAM);

FOUNDATION_API uintptr_t* context_get_scratch_arena_pos_ptr();

#define USE_SCRATCH_ARENA() \
  MemoryArena scratch_arena = alloc_scratch_arena(); \
  defer { free_scratch_arena(&scratch_arena); }

#define SCRATCH_ARENA_PASS &scratch_arena

