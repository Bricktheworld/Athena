#pragma once
#include "Core/Foundation/types.h"

// Use `start` to chain multiple crc32 hashes together...
FOUNDATION_API u32 crc32(const void* src, size_t size, u32 start = 0);
