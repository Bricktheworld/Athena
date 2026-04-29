#include "Core/Foundation/sort.h"
#include "Core/Foundation/context.h"

void
radix_sort(void* data, u32 count, u32 stride, u32 key_offset)
{
  if (count <= 1)
  {
    return;
  }

  static constexpr u32 kBits    = 8;
  static constexpr u32 kBuckets = 1u << kBits;
  static constexpr u32 kMask    = kBuckets - 1;
  static constexpr u32 kPasses  = sizeof(u32) * 8 / kBits;

  u8* src = (u8*)data;
  ScratchAllocator scratch_arena = alloc_scratch_arena();
  defer { free_scratch_arena(&scratch_arena); };

  u8* dst = HEAP_ALLOC(u8, scratch_arena, count * stride);

  for (u32 pass = 0; pass < kPasses; pass++)
  {
    u32 shift = pass * kBits;

    u32 counts[kBuckets] = {};
    for (u32 i = 0; i < count; i++)
    {
      u32 key = *(u32*)(src + i * stride + key_offset);
      counts[(key >> shift) & kMask]++;
    }

    u32 offsets[kBuckets];
    offsets[0] = 0;
    for (u32 i = 1; i < kBuckets; i++)
    {
      offsets[i] = offsets[i - 1] + counts[i - 1];
    }

    for (u32 i = 0; i < count; i++)
    {
      u32 key    = *(u32*)(src + i * stride + key_offset);
      u32 bucket = (key >> shift) & kMask;
      memcpy(dst + offsets[bucket]++ * stride, src + i * stride, stride);
    }

    u8* tmp = src;
    src     = dst;
    dst     = tmp;
  }
}
