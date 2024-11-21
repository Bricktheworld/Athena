#include "Core/Foundation/memory.h"
#include "Core/Foundation/context.h"

#include <windows.h>

void*
reserve_commit_pages(size_t size, void* addr)
{
  return VirtualAlloc(addr, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
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

  if (new_pos > self->start + self->size)
  {
    return nullptr;
  }

  self->pos = new_pos;

  return (void*)memory_start;
}

LinearAllocator
init_linear_allocator(void* memory, size_t size)
{
  LinearAllocator ret = {0};
  ret.start           = (uintptr_t)memory;
  ret.pos             = ret.start;
  ret.size            = size;
  ret.prev            = nullptr;
  ret.backing_heap    = {0};
  return ret;
}

LinearAllocator
init_linear_allocator(FreeHeap heap, size_t size)
{
  u8* memory          = HEAP_ALLOC(u8, heap, size);

  LinearAllocator ret = {0};
  ret.start           = (uintptr_t)memory;
  ret.pos             = ret.start;
  ret.size            = size;
  ret.prev            = nullptr;
  ret.backing_heap    = heap;

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

  if (!self->backing_heap.allocator)
    return;

  HEAP_FREE(self->backing_heap, (void*)self->start);
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

  ret.start           = (uintptr_t)memory;
  ret.pos             = ret.start;
  ret.size            = size;
  ret.prev            = nullptr;
  ret.backing_heap    = {0};

  return ret;
}

StackAllocator
init_stack_allocator(FreeHeap heap, size_t size)
{
  u8* memory          = HEAP_ALLOC(u8, heap, size);

  StackAllocator ret  = {0};
  ret.start           = (uintptr_t)memory;
  ret.pos             = ret.start;
  ret.size            = size;
  ret.prev            = nullptr;
  ret.backing_heap    = heap;

  return ret;
}

void
destroy_stack_allocator(StackAllocator* self)
{
  ASSERT(self->pos >= self->start);

  if (!self->backing_heap.allocator)
    return;

  HEAP_FREE(self->backing_heap, (void*)self->start);
}

void*
push_stack(StackAllocator* self, size_t size, size_t alignment, size_t* out_allocated_size)
{
  ASSERT(self->pos >= self->start);

  uintptr_t memory_start = align_address(self->pos, alignment);
  uintptr_t padding      = memory_start - (uintptr_t)self->pos;

  uintptr_t new_pos      = memory_start + size;

  if (new_pos > self->start + self->size)
  {
    return nullptr;
  }

  self->pos = new_pos;

  *out_allocated_size      = (size_t)(padding + size);

  return (void*)memory_start;
}

void
pop_stack(StackAllocator* self, size_t size)
{
  ASSERT(self->pos >= self->start + size);
  self->pos -= size;
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
