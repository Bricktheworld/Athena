#pragma once
#include "Core/Foundation/memory.h"

//#define DEFAULT_SCRATCH_SIZE KiB(16)


struct Context
{
  StackAllocator scratch_allocator = {0};
//  Context* prev = nullptr;
};

FOUNDATION_API void* scratch_alloc(void* scratch_allocator, size_t size, size_t alignment);
struct ScratchAllocator
{
  u64             allocated         = 0;
  uintptr_t       expected_start    = 0;
  StackAllocator* backing_allocator = nullptr;

  operator AllocHeap()
  {
    AllocHeap ret = {0};
    ret.alloc_fn  = &scratch_alloc;
    ret.allocator = this;
    return ret;
  }
};

FOUNDATION_API Context init_thread_context();

FOUNDATION_API ScratchAllocator alloc_scratch_arena();
FOUNDATION_API void  free_scratch_arena(ScratchAllocator* allocator);

// Quick and easy scratch allocate (needs to be reset every once in a while to prevent leaking)
FOUNDATION_API void* scratch_alloc(size_t size, size_t alignment = 0);
FOUNDATION_API void  reset_scratch_allocator();

