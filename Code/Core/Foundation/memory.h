#pragma once
#include "Core/Foundation/types.h"

#define ALIGN_POW2(v, alignment) (((v) + ((alignment) - 1)) & ~(((v) - (v)) + (alignment) - 1))

inline bool
is_pow2(u64 v)
{
  return (v & ~(v - 1)) == v;
}

inline uintptr_t
align_address(uintptr_t address, size_t alignment)
{
  size_t mask = alignment - 1;

  // Ensure that alignment is a power of 2
  ASSERT((alignment & mask) == 0);

  return (address + mask) & ~mask;
}

template <typename T>
inline T*
align_ptr(T* ptr, size_t alignment)
{
  auto address = reinterpret_cast<uintptr_t>(ptr);
  auto aligned = align_address(address, alignment);
  return reinterpret_cast<T*>(aligned);
}

inline void
zero_memory(void* memory, size_t size)
{
  byte *b = reinterpret_cast<byte*>(memory);
  while(size--)
  {
    *b++ = 0;
  }
}

FOUNDATION_API void* reserve_commit_pages(size_t size, void* addr = 0);
FOUNDATION_API void  free_pages(void* ptr);

// Perhaps a better naming convention is in order, but there are basically 3 "tiers" of allocators,
// each harder to come by than the last.
// In general, all allocations fall under the following (from most to least common):
// 1. Small allocations of known size and short lifetime (reasonable worst-case size)
// 2. One-time allocations of known long lifetime
// 3. Allocations of unknown size and known lifetime
// 4. Allocations of known size and unknown lifetime
// 5. Allocations of unknown size and unknown lifetime
// 
// Stuff in 1 and 2 are easy to deal with in an AllocHeap and are in general going to be the
// overwhelming majority of allocations. Your allocation will probably be one of these (especially 1).
// and as such an AllocHeap is applicable in most places.
//
// However, sometimes the worst-case size is much larger than the average case and in that case,
// reallocation needs to be made available, and this is what the GrowableHeap is for. Stuff that
// has a reasonable average case size but will sometimes overflow and needs to be handled
// is a great candidate for a ReallocHeap
//
// If the allocation has a known size but unknown lifetime, then we'll want some way for the
// memory to be freed. This is what a FreeHeap is for, so that you can release the memory
// on demand while still not needing the ability to resize the memory. Pool allocation and
// stuff like assets are the common candidates for this sort of thing.
//
//
//
// The reason for separating all of these different allocator types is so that
// functions can statically tell the programmer what type of lowest common denominator
// allocator they need. If you have a ReallocFreeHeap that can be cast to any of the
// other heaps but the inverse is not true.
//
//
// Terminology:
// - Heap: A set of function pointers/APIs to allocate memory. The generic interface over
//   a custom implementation of an allocator.
// - Allocator: An allocator is actually what is responsible for allocating memory.
struct AllocHeap
{
  void* (*alloc_fn)  (void* allocator, size_t size, size_t alignment) = nullptr;
  void* allocator = nullptr;
};

struct FreeHeap
{
  void* (*alloc_fn)  (void* allocator, size_t size, size_t alignment) = nullptr;
  void  (*free_fn)   (void* allocator, void* ptr) = nullptr;
  void* allocator = nullptr;

  explicit operator AllocHeap() const
  { 
    AllocHeap ret = {0};
    ret.alloc_fn  = alloc_fn;
    ret.allocator = allocator;
    return ret;
  }
};

struct ReallocFreeHeap
{
  void* (*alloc_fn)  (void* allocator, size_t size, size_t alignment) = nullptr;
  void  (*free_fn)   (void* allocator, void* ptr) = nullptr;
  void* (*realloc_fn)(void* allocator, void* ptr, size_t size, size_t alignment) = nullptr;
  void* allocator = nullptr;

  operator FreeHeap() const
  { 
    FreeHeap ret  = {0};
    ret.alloc_fn  = alloc_fn;
    ret.free_fn   = free_fn;
    ret.allocator = allocator;
    return ret;
  }
};

#define HEAP_ALLOC(T, heap, count)( (T*)((AllocHeap)(heap)).alloc_fn(((AllocHeap)(heap)).allocator, (count) * sizeof(T), alignof(T)) )
#define HEAP_ALLOC_ALIGNED(heap, size, alignment)( (u8*)(heap).alloc_fn ((heap).allocator, (size), (alignment)) )
#define HEAP_FREE(heap, ptr)( (heap).free_fn((heap).allocator, ptr) )
#define HEAP_REALLOC(T, heap, ptr, count)( (T*)(heap).realloc_fn((heap).allocator, (ptr), (count) * sizeof(T), alignof(T)) )
#define HEAP_REALLOC_ALIGNED(heap, ptr, size, alignment)( (T*)(heap).realloc_fn((heap).allocator, (ptr), (size), (alignment)) )

FOUNDATION_API void* linear_alloc(void* linear_allocator, size_t size, size_t alignment);
struct LinearAllocator
{
  struct FilledBuffer
  {
    FilledBuffer* prev = nullptr;
  };

  uintptr_t     start        = 0x0;
  uintptr_t     pos          = 0x0;
  size_t        size         = 0;
  FilledBuffer* prev         = nullptr;

  // A typical setup might be to back a linear allocator with a pool allocator with pages
  // to allow for overflow.
  FreeHeap      backing_heap = {0};

  operator AllocHeap()
  {
    AllocHeap ret = {0};
    ret.alloc_fn  = &linear_alloc;
    ret.allocator = this;
    return ret;
  }
};
FOUNDATION_API LinearAllocator init_linear_allocator   (void* memory, size_t size);
FOUNDATION_API LinearAllocator init_linear_allocator   (FreeHeap heap, size_t size);
FOUNDATION_API void            reset_linear_allocator  (LinearAllocator* linear_allocator);
FOUNDATION_API void            destroy_linear_allocator(LinearAllocator* linear_allocator);

FOUNDATION_API void* pool_alloc(void* pool_allocator, size_t size, size_t alignment);
FOUNDATION_API void  pool_free (void* pool_allocator, void* ptr);
struct PoolAllocator
{
  struct FreeBlock
  {
    FreeBlock* next = nullptr;
  };

  void*      memory       = nullptr;
  FreeBlock* free_head    = nullptr;
  size_t     block_size   = 0;

  FreeHeap   backing_heap = {0};

  operator FreeHeap()
  {
    FreeHeap ret = {0};
    ret.alloc_fn  = &pool_alloc;
    ret.free_fn   = &pool_free;
    ret.allocator = this;
    return ret;
  }
};
FOUNDATION_API PoolAllocator init_pool_allocator   (void* memory, size_t size, size_t block_size, size_t alignment);
FOUNDATION_API PoolAllocator init_pool_allocator   (FreeHeap backing_heap, size_t size, size_t block_size, size_t alignment);
FOUNDATION_API void          destroy_pool_allocator(PoolAllocator* pool_allocator);

struct StackAllocator
{
  struct FilledBuffer
  {
    FilledBuffer* prev = nullptr;
  };

  uintptr_t     start        = 0x0;
  uintptr_t     pos          = 0x0;
  size_t        size         = 0;
  FilledBuffer* prev         = nullptr;

  FreeHeap      backing_heap = {0};
};
FOUNDATION_API StackAllocator init_stack_allocator   (void* memory, size_t size);
FOUNDATION_API StackAllocator init_stack_allocator   (FreeHeap heap, size_t size);
FOUNDATION_API void           destroy_stack_allocator(StackAllocator* allocator);

FOUNDATION_API void* push_stack(StackAllocator* allocator, size_t size, size_t alignment, size_t* out_allocated_size);
FOUNDATION_API void  pop_stack (StackAllocator* allocator, size_t size);


FOUNDATION_API void* os_alloc(void* os_allocator, size_t size, size_t alignment);
FOUNDATION_API void  os_free (void* os_allocator, void* ptr);
struct OSAllocator
{
  operator FreeHeap()
  {
    FreeHeap ret  = {0};
    ret.alloc_fn  = &os_alloc;
    ret.free_fn   = &os_free;
    ret.allocator = this;
    return ret;
  }
};

#define GLOBAL_HEAP ((FreeHeap)OSAllocator())

FOUNDATION_API OSAllocator init_os_allocator   ();
FOUNDATION_API void        destroy_os_allocator(OSAllocator* allocator);

struct TlsfAllocator
{
  struct BinNode
  {
    u64 offset         = 0;
    u64 size           = 0;

    u32 bin_prev       = 0;
    u32 bin_next       = 0;
    u32 neighbor_next  = 0;
    u32 neighbor_prev  = 0;
  };

  u64      size        = 0;
  void*    memory      = 0;

  u32      max_allocs  = 0;
  BinNode* nodes       = nullptr;
  u32*     free_stack  = nullptr;
  u32      free_offset = 0;
};

FOUNDATION_API TlsfAllocator init_tlsf_allocator(AllocHeap heap, void* memory, u64 size);
FOUNDATION_API void* tlsf_alloc(void* tlsf_allocator, u64 size, u32 alignment);
FOUNDATION_API void* tlsf_free (void* tlsf_allocator, void* ptr);

#if 0
struct MultiLevelPoolAllocator
{
};

struct TlsfAllocator
{
};
#endif


// ---- Sub allocators: Allocators but they give back metadata and aren't intrusive (need extra memory).
// Use them if you want to Suballocate a resource (you have a big GPU buffer and want to allocate pieces of it at a time)

struct SubAllocation
{
  u32 offset   = 0;
  u32 metadata = 0;
};

struct SubAllocHeap
{
  SubAllocation (*alloc_fn)  (void* allocator, size_t size, size_t alignment) = nullptr;
  void* allocator = nullptr;
};

struct SubFreeHeap
{
  SubAllocation (*alloc_fn)  (void* allocator, size_t size, size_t alignment) = nullptr;
  void  (*free_fn)   (void* allocator, SubAllocation) = nullptr;
  void* allocator = nullptr;
};
