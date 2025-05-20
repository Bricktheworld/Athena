#include "Core/Foundation/types.h"
#include "Core/Foundation/signpost.h"


// I'm doing this instead of just using RingBuffer because this is ultra performance dependent
static constexpr u64 kSignpostBufferSize    = MiB(8) / sizeof(u64);
static u64*          g_SignpostBuffer       = nullptr;
static volatile u64  g_SignpostBufferOffset = 0;


void
init_signpost_buffer(AllocHeap heap)
{
  g_SignpostBuffer = HEAP_ALLOC(u64, heap, kSignpostBufferSize);
}

void
signpost_log_packet(const u64* packet, u32 size_in_bytes)
{
  ASSERT_MSG_FATAL(size_in_bytes % sizeof(u64) == 0, "Signpost packet must be aligned to 8 byte boundary!");

  u64 count_u64 = size_in_bytes / sizeof(u64);

  u64 idx = InterlockedAdd64((volatile s64*)&g_SignpostBufferOffset, count_u64);

  u64* dst = g_SignpostBuffer + idx;
  memcpy(dst, packet, count_u64);
}

