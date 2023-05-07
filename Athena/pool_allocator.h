#pragma once
#include "types.h"
#include "memory/memory.h"


template <typename T>
struct PoolAllocator
{
	T* pool = nullptr;
	size_t size = 0;

	T** free = nullptr;
	size_t free_count = 0;
};


// If size is 0 then the pool allocator will encompass the entire
// memory arena.
template <typename T>
PoolAllocator<T>
init_pool_allocator(MEMORY_ARENA_PARAM, size_t size = 0)
{
	if (size == 0)
	{
		// This is just a safety check, it will almost
		// certainly never happen.
		ASSERT(memory_arena->size > 2);

		// This _should_ preserve the size being a power of 2, so it shouldn't
		// be an issue for alignment.
		size = memory_arena->size / 2;
	}

	PoolAllocator<T> ret = {0};
	ret.pool = push_memory_arena<T>(MEMORY_ARENA_FWD, size);
	ret.free = push_memory_arena<T*>(MEMORY_ARENA_FWD, size);

	zero_memory(ret.pool, sizeof(T) * size);
	for (size_t i = 0; i < size; i++)
	{
		ret.free[i] = ret.pool + i;
	}

	ret.size = size;
	ret.free_count = ret.size;

	return ret;
}

template <typename T>
T* pool_alloc(PoolAllocator<T>* allocator)
{
	ASSERT(allocator->free_count >= 1);

	allocator->free_count--;
	T* ret = allocator->free[allocator->free_count];

	zero_memory(ret, sizeof(T));

	return ret;
}

template <typename T>
void pool_free(PoolAllocator<T>* allocator, T* memory)
{
	ASSERT(allocator->pool <= memory && allocator->pool + allocator->size > memory);

	allocator->free[allocator->free_count] = memory;
	allocator->free_count++;
	zero_memory(memory, sizeof(T));
}
