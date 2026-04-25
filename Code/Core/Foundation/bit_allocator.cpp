#include "Core/Foundation/bit_allocator.h"

BitAllocator
init_bit_allocator(AllocHeap heap, u32 capacity)
{
  BitAllocator ret;

  u32 qwords          = ALIGN_POW2(capacity, 64) / 64;
  ret.bits            = HEAP_ALLOC(u64, heap, qwords);
  zero_memory(ret.bits, qwords * sizeof(u64));
  ret.capacity         = capacity;
  ret.allocated_count = 0;

  return ret;
}

Option<u32>
bit_alloc(BitAllocator* allocator)
{
  u32 max_qword = ALIGN_POW2(allocator->capacity, 64) / 64;
  for (u32 i = 0; i < max_qword; i++)
  {
    u64* qword = allocator->bits + i;
    // Means completely allocated
    if (*qword == U64_MAX)
    {
      continue;
    }

    u32 idx = (u32)count_trailing_zeroes(~(*qword));
    u64 bit = 1ULL << idx;
    *qword |= bit;

    allocator->allocated_count++;

    return i * 64 + idx;
  }

  ASSERT_MSG_FATAL(false, "Bit allocator with max value of %u ran out of bits!", allocator->capacity);
  return None;
}

bool
bit_is_allocated(const BitAllocator& allocator, u32 idx)
{
  u64* qword = allocator.bits + (idx / 64);
  u64  bit   = 1ULL << idx;
  return (*qword) & bit;
}

void
bit_free(BitAllocator* allocator, u32 idx)
{
  u64* qword = allocator->bits + (idx / 64);
  u32  bit   = 1ULL << idx;
  ASSERT_MSG_FATAL(((*qword) & bit), "Freed bit %u was not allocated! Double bit_free detected", idx);

  ASSERT_MSG_FATAL(allocator->allocated_count > 0, "For some reason, bit allocator says there are no bits allocated, but there is a bit set implying some sort of mismatch, something went wrong internally...");
  allocator->allocated_count--;

  *qword &= bit;
}
