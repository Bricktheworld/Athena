#pragma once
#include "Core/Foundation/types.h"

enum SortOp : u32
{
  kSortIncreasing,
  kSortDecreasing,
};
FOUNDATION_API void radix_sort(void* data, u32 count, u32 stride, u32 key_offset, SortOp op = kSortIncreasing);
