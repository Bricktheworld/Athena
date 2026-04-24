#include "Core/Foundation/context.h"

thread_local Context g_Ctx = {0};

#define CTX_IS_INITIALIZED (g_Ctx.scratch_allocator.memory != 0x0)
#define ASSERT_CTX_INIT() ASSERT_MSG_FATAL(CTX_IS_INITIALIZED, "Attempting to use scratch arena when context not initialized!")

Context
init_thread_context()
{
  ASSERT(!CTX_IS_INITIALIZED);

  static constexpr u64 kDefaultScratchCommit  = MiB(1);
  // Lots of room to overflow, since address space is pretty free
  static constexpr u64 kDefaultScratchReserve = GiB(1);

  Context ret = {0};
  ret.scratch_allocator = init_stack_allocator(kDefaultScratchCommit, kDefaultScratchReserve);

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
  ret.allocated         = 0;
  ret.backing_allocator = &g_Ctx.scratch_allocator;
  ret.expected_start    = ret.backing_allocator->pos;

  return ret;
}

void
free_scratch_arena(ScratchAllocator* self)
{
  ASSERT_CTX_INIT();

  pop_stack(self->backing_allocator, self->allocated);

  // If you hit this assertion, you're passing the scratch arena to functions that themselves use a scratch arena
  ASSERT_MSG_FATAL(
    self->expected_start == self->backing_allocator->pos,
    "You are passing a scratch arena to functions that, themselves, use scratch arenas!\n"
    "It is recommended that instead of freeing this scratch allocator you rely on reset_scratch_allocator to do the cleanup for you, or pass a different scratch allocator to the functions.\n"
    "It is also generally considered bad practice to both use a scratch arena _and_ take in an AllocHeap, you should do one or the other.\n"
    "If you want to take in an AllocHeap to know where to put resulting data for example, then you should just use `scratch_alloc` to allocate your scratch memory, a reset_scratch_allocator should clean up that memory later."
  );
}

void*
scratch_alloc(void* scratch_allocator, size_t size, size_t alignment)
{
  ScratchAllocator* self = (ScratchAllocator*)scratch_allocator;

  size_t allocated_size = size;
  void* ret = push_stack(self->backing_allocator, size, alignment, &allocated_size);

  ASSERT_MSG_FATAL(ret != nullptr, "Scratch allocator ran out of memory! Attempted to allocate %llu bytes, but %llu / %llu bytes already allocated.", size, (uintptr_t)(self->backing_allocator->pos - self->backing_allocator->memory), self->backing_allocator->reserve_size);

  self->allocated += allocated_size;
  return ret;
}

void*
scratch_alloc(size_t size, size_t alignment)
{
  ScratchAllocator scratch_arena = alloc_scratch_arena();
  // Intentionally not freeing it here because the point is that you reset the entire scratch allocator all at once
  return HEAP_ALLOC_ALIGNED((AllocHeap)scratch_arena, size, alignment);
}

void
reset_scratch_allocator()
{
  reset_stack(&g_Ctx.scratch_allocator);
}
