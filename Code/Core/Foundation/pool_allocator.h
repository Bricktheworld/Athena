#pragma once
#include "Core/Foundation/types.h"
#include "Core/Foundation/memory.h"


template <typename T>
struct Pool
{
  union PoolItem
  {
    T   value;
    u32 next_free;
  };
  static_assert(offsetof(PoolItem, value) == 0);

  PoolItem* memory       = nullptr;
  u32       size       = 0;
  u32       first_free = 0;

};


// If size is 0 then the pool allocator will encompass the entire
// memory arena.
template <typename T>
Pool<T>
init_pool(AllocHeap heap, u32 size)
{
#if 0
  if (size == 0)
  {
    // This is just a safety check, it will almost
    // certainly never happen.
    ASSERT(memory_arena->size > 2);

    // This _should_ preserve the size being a power of 2, so it shouldn't
    // be an issue for alignment.
    size = memory_arena->size / 2;
  }
#endif
  ASSERT(size > 0);
  using PoolItem = typename Pool<T>::PoolItem;

  Pool<T> ret = {0};
  ret.memory = HEAP_ALLOC(PoolItem, heap, size); // push_memory_arena<T>(MEMORY_ARENA_FWD, size);

  zero_memory(ret.memory, sizeof(PoolItem) * size);
  for (u32 i = 0; i < size; i++)
  {
    ret.memory[i].next_free = i + 1;
  }

  ret.size       = size;
  ret.first_free = 0;

  return ret;
}

template <typename T>
T* pool_alloc(Pool<T>* pool)
{
  ASSERT(pool->first_free < pool->size);

  auto* ret        = pool->memory + pool->first_free;

  pool->first_free = ret->next_free;

  zero_memory(ret, sizeof(*ret));

  return &ret->value;
}

template <typename T>
void pool_free(Pool<T>* pool, T* memory)
{
  ASSERT(pool->memory <= memory && pool->memory + pool->size > memory);
  using PoolItem = typename Pool<T>::PoolItem;

  auto* item       = (PoolItem*)memory;
  u32   idx        = (u32)(item - pool->memory);

  item->next_free = pool->first_free;
  pool->first_free = idx;


  pool->free_count++;
  zero_memory(memory, sizeof(T));
}
