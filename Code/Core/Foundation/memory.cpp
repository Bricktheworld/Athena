#include "Core/Foundation/memory.h"
#include "Core/Foundation/context.h"

#include <windows.h>

void*
reserve_commit_pages(size_t size, void* addr)
{
  return VirtualAlloc(addr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

void*
reserve_pages(size_t size, void* addr)
{
  return VirtualAlloc(addr, size, MEM_RESERVE, PAGE_READWRITE);
}

void
commit_pages(size_t size, void* addr)
{
  VirtualAlloc(addr, size, MEM_COMMIT, PAGE_READWRITE);
}

void
decommit_pages(size_t size, void* addr)
{
  VirtualAlloc(addr, size, MEM_DECOMMIT, PAGE_READWRITE);
}

void 
free_pages(void* ptr)
{
  VirtualFree(ptr, 0, MEM_RELEASE);
}

void*
linear_alloc(void* linear_allocator, size_t size, size_t alignment)
{
  LinearAllocator* self  = (LinearAllocator*)linear_allocator;

  ASSERT(self->pos >= self->start);

  uintptr_t memory_start = align_address(self->pos, alignment);

  uintptr_t new_pos      = memory_start + size;

#if 0
  if (new_pos > self->start + self->size)
  {
    return nullptr;
  }
#endif
  ASSERT_MSG_FATAL(
    new_pos <= self->start + self->size,
    "Linear allocator ran out of memory! Attempted to allocate 0x%llx"
    "bytes from linear allocator of size 0x%llx that only has 0x%llx"
    "bytes remaining. Either bump this allocator's memory size or"
    "figure out why it overflowed.",
    size, self->size, self->size - self->pos
  );

  self->pos = new_pos;

  return (void*)memory_start;
}

size_t
available_memory(void* linear_allocator)
{
  LinearAllocator* self = (LinearAllocator*)linear_allocator;

  if (self->pos >= self->start + self->size)
  {
    return 0;
  }


  return self->start + self->size - self->pos;
}

LinearAllocator
init_linear_allocator(void* memory, size_t size)
{
  LinearAllocator ret = {0};
  ret.start           = (uintptr_t)memory;
  ret.pos             = ret.start;
  ret.size            = size;

  ret.reserve_size    = 0;
  ret.commit_size     = 0;
  return ret;
}

LinearAllocator
init_linear_allocator(size_t commit_size, size_t reserve_size)
{
  commit_size  = ALIGN_POW2(commit_size,  kPageSize);
  reserve_size = ALIGN_POW2(reserve_size, kPageSize);
  ASSERT_MSG_FATAL(reserve_size >= commit_size, "It is not supported to create a linear allocator reserve size (%llu) smaller than it's initially committed size (%llu) (that wouldn't make any sense).", reserve_size, commit_size);

  void* memory = reserve_pages(reserve_size);
  commit_pages(commit_size, memory);

  LinearAllocator ret = {0};
  ret.start           = (uintptr_t)memory;
  ret.pos             = ret.start;
  ret.size            = commit_size;

  ret.reserve_size    = reserve_size;
  ret.commit_size     = commit_size;
  return ret;
}

void
reset_linear_allocator(LinearAllocator* self)
{
  ASSERT(self->pos >= self->start);
  self->pos = self->start;
}

void 
destroy_linear_allocator(LinearAllocator* self)
{
  ASSERT(self->pos >= self->start);

  if (self->commit_size != 0)
  {
    free_pages((void*)self->start);
  }
}

void*
pool_alloc(void* pool_allocator, size_t size, size_t alignment)
{
  PoolAllocator* self = (PoolAllocator*)pool_allocator;
  ASSERT(size <= self->block_size);

  auto* free_block = self->free_head;
  if (free_block == nullptr)
  {
    return nullptr;
  }

  uintptr_t aligned = align_address((uintptr_t)free_block, alignment);
  uintptr_t padding = aligned - (uintptr_t)free_block;
  ASSERT(size <= self->block_size - padding);

  self->free_head   = free_block->next;

  return (void*)aligned;
}

void
pool_free(void* pool_allocator, void* ptr)
{
  PoolAllocator* self = (PoolAllocator*)pool_allocator;

  uintptr_t offset      = (uintptr_t)ptr - (uintptr_t)self->memory;
  u64       block_index = offset / self->block_size;

  auto* block = (PoolAllocator::FreeBlock*)((u8*)self->memory + block_index * self->block_size);

  block->next = self->free_head;
  self->free_head = block;
}

PoolAllocator
init_pool_allocator(void* memory, size_t size, size_t block_size, size_t alignment)
{
  alignment = MAX(alignof(PoolAllocator::FreeBlock), alignment);
  // Needed for the intrusive linked list
  ASSERT(block_size >= sizeof(PoolAllocator::FreeBlock));

  PoolAllocator ret = {0};

  ret.memory     = memory;
  ret.block_size = block_size;

  u8* buf           = (u8*)align_ptr(ret.memory, alignment);
  uintptr_t padding = (uintptr_t)buf - (uintptr_t)ret.memory;
  ASSERT(padding < size);

  size             -= padding;

  u64 num_blocks    = size / block_size;
  ret.free_head     = (PoolAllocator::FreeBlock*)buf;

  for (u64 iblock = 0; iblock < num_blocks; iblock++)
  {
    auto* block = (PoolAllocator::FreeBlock*)(buf + iblock * block_size);
    block->next = iblock + 1 < num_blocks ? (PoolAllocator::FreeBlock*)(buf + (iblock + 1) * block_size) : nullptr;
  }

  return ret;
}

PoolAllocator
init_pool_allocator(FreeHeap backing_heap, size_t size, size_t block_size, size_t alignment)
{
  u8* memory        = HEAP_ALLOC_ALIGNED(backing_heap, size, alignment);
  PoolAllocator ret = init_pool_allocator(memory, size, block_size, alignment);
  ret.backing_heap  = backing_heap;
  return ret;
}

void
destroy_pool_allocator(PoolAllocator* self)
{
  if (!self->backing_heap.allocator)
    return;

  HEAP_FREE(self->backing_heap, self->memory);
}

StackAllocator
init_stack_allocator(void* memory, size_t size)
{
  StackAllocator ret = {0};

  ret.memory              = (uintptr_t)memory;
  ret.pos                 = ret.memory;
  ret.reserve_size        = 0;
  ret.commit_size         = size;
  ret.initial_commit_size = 0;

  return ret;
}

StackAllocator
init_stack_allocator(size_t commit_size, size_t reserve_size)
{
  commit_size  = ALIGN_POW2(commit_size,  kPageSize);
  reserve_size = ALIGN_POW2(reserve_size, kPageSize);

  ASSERT_MSG_FATAL(reserve_size >= commit_size, "It is not supported to create a linear allocator reserve size (%llu) smaller than it's initially committed size (%llu) (that wouldn't make any sense).", reserve_size, commit_size);

  void* memory = reserve_pages(reserve_size);
  commit_pages(commit_size, memory);

  StackAllocator ret      = {0};

  ret.memory              = (uintptr_t)memory;
  ret.pos                 = ret.memory;

  ret.reserve_size        = reserve_size;
  ret.commit_size         = commit_size;
  ret.initial_commit_size = commit_size;

  return ret;
}

void
destroy_stack_allocator(StackAllocator* self)
{
  ASSERT(self->pos >= self->memory);

  if (self->reserve_size != 0)
  {
    free_pages((void*)self->memory);
  }
}

void*
push_stack(StackAllocator* self, size_t size, size_t alignment, size_t* out_allocated_size)
{
  ASSERT(self->pos >= self->memory);

  uintptr_t memory_start = align_address(self->pos, alignment);
  uintptr_t padding      = memory_start - (uintptr_t)self->pos;

  uintptr_t new_pos      = memory_start + size;

  if (new_pos > self->memory + self->commit_size)
  {
    if (new_pos > self->memory + self->reserve_size)
    {
      return nullptr;
    }
    // Need to commit more pages! We overflowed :)
    // TODO(bshihabi): We should have some global atomic thing of like, how many total pages have been committed
    else
    {
      size_t new_commit_size = ALIGN_POW2(new_pos - self->memory, kPageSize);
      ASSERT_MSG_FATAL(new_commit_size > self->commit_size, "Something went wrong internally in the stack allocator! Tracking of committed vs reserved memory didn't line up.");
      self->commit_size = new_commit_size;
      commit_pages(self->commit_size, (void*)self->memory);

      // Now we're all good! The pages are committed and we can successfully return the pointer since the memory is now mapped
    }
  }

  self->pos = new_pos;

  *out_allocated_size      = (size_t)(padding + size);

  return (void*)memory_start;
}

void
pop_stack(StackAllocator* self, size_t size)
{
  ASSERT(self->pos >= self->memory + size);
  self->pos -= size;

  // If we go over the allocator by 4 pages, and we popped off a lot of memory, then we should clean it up
  static size_t kPageOverflowCountCleanupThreshold = 4;

  size_t memory_usage = self->pos - self->memory;

  // Do some clean up here for the pages
  //   1. If we committed more than our initial budget
  bool overflow_committed  = self->commit_size > self->initial_commit_size;
  //   2. And we're no longer using more than kPageOverflowCountCleanupThreshold pages (there are 4 free pages available)
  bool underused_committed = memory_usage + kPageOverflowCountCleanupThreshold * kPageSize < self->commit_size;
  //   We should decommit
  if (overflow_committed && underused_committed)
  {
    size_t    new_commit_size = ALIGN_POW2(memory_usage, kPageSize);
    uintptr_t decommit_start  = self->memory + new_commit_size;
    ASSERT_MSG_FATAL(new_commit_size > self->commit_size, "Something went wrong when calculating how much to decommit from stack allocator.");
    size_t    decommit_size   = self->commit_size - new_commit_size;
    ASSERT_MSG_FATAL((decommit_size % kPageSize) == 0, "Decommit size is not a power of kPageSize, so something went wrong in stack allocator.");
    decommit_pages(decommit_size, (void*)decommit_start);

    self->commit_size = new_commit_size;
  }
}

void
reset_stack(StackAllocator* self)
{
  ASSERT_MSG_FATAL(self->pos >= self->memory, "Something went wrong internally in the stack allcoator! pos < memory somehow. This is a bug.");
  size_t usage = (size_t)(self->pos - self->memory);
  pop_stack(self, usage);
}

void*
os_alloc(void* os_allocator, size_t size, size_t alignment)
{
  (void)os_allocator;
  return _aligned_malloc(size, alignment);
}

void 
os_free(void* os_allocator, void* ptr)
{
  (void)os_allocator;
  _aligned_free(ptr);
}

OSAllocator
init_os_allocator()
{
  OSAllocator ret = {};
  return ret;
}
void destroy_os_allocator(OSAllocator* self) 
{
  UNREFERENCED_PARAMETER(self);
}

// TODO(bshihabi): Finish this method
#if 0
TlsfAllocator
init_tlsf_allocator(AllocHeap heap, void* memory, u64 size)
{
  TlsfAllocator ret = {0};
}
#endif