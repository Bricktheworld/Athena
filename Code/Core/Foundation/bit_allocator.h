#pragma once
#include "Core/Foundation/memory.h"
#include "Core/Foundation/Containers/option.h"

struct BitAllocator
{
  u64* bits    = nullptr;
  u32  max_val = 0;
};

FOUNDATION_API BitAllocator init_bit_allocator(AllocHeap heap, u32 max_val);
FOUNDATION_API Option<u32> bit_alloc(BitAllocator* allocator);
FOUNDATION_API bool bit_is_allocated(const BitAllocator& allocator, u32 val);
FOUNDATION_API void bit_free(BitAllocator* allocator, u32 val);

