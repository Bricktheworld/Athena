#include "Core/Engine/memory.h"

struct MemoryLayout
{
  u8* memory = nullptr;

  LinearAllocator init_allocator;
  LinearAllocator frame_allocator;
  PoolAllocator   overflow_allocator;
//  PoolAllocator   overflow_allocator;
};

static MemoryLayout g_MemoryLayout;

AllocHeap       g_InitHeap;
AllocHeap       g_FrameHeap;
FreeHeap        g_OverflowHeap;
ReallocFreeHeap g_ResourceHeap;

void
init_engine_memory()
{
  ASSERT(g_MemoryLayout.memory == nullptr);

  g_MemoryLayout.memory = (u8*)reserve_commit_pages(kTotalHeapSize);

  ASSERT(g_MemoryLayout.memory != nullptr);

  u8* memory = g_MemoryLayout.memory;
  g_MemoryLayout.init_allocator     = init_linear_allocator(memory, kInitHeapSize);
  memory += kInitHeapSize;

  g_MemoryLayout.frame_allocator    = init_linear_allocator(memory, kFrameHeapSize);
  memory += kFrameHeapSize;

  g_MemoryLayout.overflow_allocator = init_pool_allocator(memory, kOverflowHeapSize, kOverflowPageSize, 8); 
  memory += kOverflowHeapSize;

  g_InitHeap     = g_MemoryLayout.init_allocator;
  g_FrameHeap    = g_MemoryLayout.frame_allocator;
  g_OverflowHeap = g_MemoryLayout.overflow_allocator;
//  g_ResourceHeap = g_MemoryLayout.resour;
}

void
destroy_engine_memory()
{
  free_pages(g_MemoryLayout.memory);
  zero_memory(&g_MemoryLayout, sizeof(g_MemoryLayout));
}

void
reset_frame_heap()
{
  reset_linear_allocator(&g_MemoryLayout.frame_allocator);
}

