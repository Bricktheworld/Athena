#pragma once

struct ClearStructuredBufferSrt
{
  u32 clear_value;
  u32 count;
  u32 offset;
  RWStructuredBufferPtr<u32> dst;
};