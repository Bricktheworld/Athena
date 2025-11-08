#include "Core/Foundation/bit_allocator.h"

BitAllocator
init_bit_allocator(AllocHeap heap, u32 max_val)
{
  BitAllocator ret;

  u32 qwords  = ALIGN_POW2(max_val, 64) / 64;
  ret.bits    = HEAP_ALLOC(u64, heap, qwords);
  zero_memory(ret.bits, qwords * sizeof(u64));
  ret.max_val = max_val;

  return ret;
}

Option<u32>
bit_alloc(BitAllocator* allocator)
{
  u32 max_qword = ALIGN_POW2(allocator->max_val, 64) / 64;
  for (u32 i = 0; i < max_qword; i++)
  {
    u64* qword = allocator->bits + i;
    // Means completely allocated
    if (*qword == U64_MAX)
    {
      continue;
    }

    u32 idx = (u32)count_trailing_zeroes(~(*qword));
    u32 bit = 1ULL << idx;
    *qword |= bit;
    return i * 64 + idx;
  }

  ASSERT_MSG_FATAL(false, "Bit allocator with max value of %u ran out of bits!", allocator->max_val);
  return None;
}

bool
bit_is_allocated(const BitAllocator& allocator, u32 val)
{
  u64* qword = allocator.bits + (val / 64);
  u32  bit   = 1ULL << val;
  return (*qword) & bit;
}

void
bit_free(BitAllocator* allocator, u32 val)
{
  u64* qword = allocator->bits + (val / 64);
  u32  bit   = 1ULL << val;
  ASSERT_MSG_FATAL(((*qword) & bit), "Freed bit %u was not allocated! Double bit_free detected", val);

  *qword &= bit;
}
