#include "Core/Foundation/context.h"

thread_local Context g_Ctx = {0};

#define CTX_IS_INITIALIZED (g_Ctx.scratch_allocator.start != 0x0)
#define ASSERT_CTX_INIT() ASSERT_MSG_FATAL(CTX_IS_INITIALIZED, "Attempting to use scratch arena when context not initialized!")

Context
init_context(AllocHeap heap, FreeHeap overflow_heap)
{
  UNREFERENCED_PARAMETER(overflow_heap);
  ASSERT(!CTX_IS_INITIALIZED);

  static constexpr u64 kDefaultScratchSize = KiB(64);

  Context ret = {0};
  ret.scratch_allocator = init_stack_allocator(HEAP_ALLOC_ALIGNED(heap, kDefaultScratchSize, 1), kDefaultScratchSize);

  if (!CTX_IS_INITIALIZED)
  {
    g_Ctx = ret;
  }

  return ret;
}

ScratchAllocator
alloc_scratch_arena()
{
  ASSERT_CTX_INIT();

  ScratchAllocator ret = {0};
  ret.allocated = 0;
  ret.backing_allocator = &g_Ctx.scratch_allocator;
  ret.expected_start = ret.backing_allocator->pos;

  return ret;
}

void
free_scratch_arena(ScratchAllocator* self)
{
  ASSERT_CTX_INIT();

//  reset_memory_arena(MEMORY_ARENA_FWD);
  pop_stack(self->backing_allocator, self->allocated);

  // If you hit this assertion, you're passing the scratch arena to functions that themselves use a scratch arena
  ASSERT(self->expected_start == self->backing_allocator->pos);
}

void*
scratch_alloc(void* scratch_allocator, size_t size, size_t alignment)
{
  ScratchAllocator* self = (ScratchAllocator*)scratch_allocator;

  size_t allocated_size = size;
  void* ret = push_stack(self->backing_allocator, size, alignment, &allocated_size);

  self->allocated += allocated_size;
  return ret;
}