#pragma once
#include "../types.h"

#define ALIGN_POW2(v, alignment) (((v) + ((alignment) - 1)) & ~(((v) - (v)) + (alignment) - 1))

inline uintptr_t align_address(uintptr_t address, size_t alignment)
{
	size_t mask = alignment - 1;

	// Ensure that alignment is a power of 2
	ASSERT((alignment & mask) == 0);

	return (address + mask) & ~mask;
}

template <typename T>
inline T* align_ptr(T* ptr, size_t alignment)
{
	auto address = reinterpret_cast<uintptr_t>(ptr);
	auto aligned = align_address(address, alignment);
	return reinterpret_cast<T*>(aligned);
}

inline void zero_memory(void* memory, size_t size)
{
	byte *b = reinterpret_cast<byte*>(memory);
	while(size--)
	{
		*b++ = 0;
	}
}

//inline void* aligned_alloc(const AllocatorRef& ref, size_t size, size_t alignment)
//{
//	void* res = ref.alloc_cb(ref.allocator, size, alignment);
//	zero_memory(res, size);
//	return res;
//}


//template <typename T>
//inline T* alloc(VolatileAllocator allocator)
//{
//	uintptr_t type = allocator & 0x7;
//
//	allocator &= ~0xF;
//	switch(type)
//	{
//		case 0x1:
//		{
//			break;
//		}
//	}
//}


void init_application_memory();
void destroy_application_memory();

struct MemoryArena
{
	uintptr_t start = 0x0;
	uintptr_t pos = 0x0;
	size_t size = 0;
};

MemoryArena alloc_memory_arena(size_t size);
void free_memory_arena(MemoryArena* arena);

inline void reset_memory_arena(MemoryArena* arena)
{
	arena->pos = arena->start;
}

#define MEMORY_ARENA_PARAM MemoryArena* memory_arena
#define MEMORY_ARENA_FWD memory_arena

void* push_memory_arena_aligned(MEMORY_ARENA_PARAM, size_t size, size_t alignment = 1);

template<typename T>
T* push_memory_arena(MEMORY_ARENA_PARAM, size_t count = 1)
{
	return reinterpret_cast<T*>(push_memory_arena_aligned(MEMORY_ARENA_FWD, sizeof(T) * count, alignof(T)));
}
