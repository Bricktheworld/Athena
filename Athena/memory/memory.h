#pragma once
#include "../types.h"

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

void init_application_memory();
void destroy_application_memory();

struct MemoryArena
{
  uintptr_t start = 0x0;
  uintptr_t pos = 0x0;

  size_t size = 0;
  bool use_ctx_pos = false;
};

#define MEMORY_ARENA_PARAM MemoryArena* memory_arena
#define MEMORY_ARENA_FWD memory_arena

MemoryArena alloc_memory_arena(size_t size);
void free_memory_arena(MEMORY_ARENA_PARAM);
void reset_memory_arena(MEMORY_ARENA_PARAM);

uintptr_t* memory_arena_pos_ptr(MEMORY_ARENA_PARAM);

void* push_memory_arena_aligned(MEMORY_ARENA_PARAM, size_t size, size_t alignment = 1);

template<typename T>
inline T*
push_memory_arena(MEMORY_ARENA_PARAM, size_t count = 1)
{
  return reinterpret_cast<T*>(push_memory_arena_aligned(MEMORY_ARENA_FWD, sizeof(T) * count, alignof(T)));
}

MemoryArena sub_alloc_memory_arena(MEMORY_ARENA_PARAM, size_t size, size_t alignment = 1);
