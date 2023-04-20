#pragma once
#include "../types.h"

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

enum struct AllocatorType : u8
{
	UNKNOWN = 0x0,
	FRAME = 0x1,
};

// Frame could either mean a literal stack frame or
struct FrameAllocator
{
	~FrameAllocator();

	uintptr_t base = 0;
	uintptr_t top = 0;
};

struct Allocator
{
};

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

void init_memory_arena();
void destroy_memory_arena();

