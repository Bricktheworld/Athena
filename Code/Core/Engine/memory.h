#pragma once
#include "Core/Foundation/types.h"
#include "Core/Foundation/memory.h"
#include "Core/Foundation/context.h"

void init_engine_memory();
void destroy_engine_memory();

// Used for anything that will last the lifetime of the engine.
extern AllocHeap       g_InitHeap;
// Lifetime of a frame.
extern AllocHeap       g_FrameHeap;
// Used to allocate overflow backing memory for linear allocators.
extern FreeHeap        g_OverflowHeap;

extern ReallocFreeHeap g_ResourceHeap;

extern AllocHeap       g_DebugHeap;

enum
{
  kOverflowPageSize = KiB(4),
};

enum HeapSize : u64
{
  kInitHeapSize     = GiB(1),
  kDebugHeapSize    = MiB(16),
  kFrameHeapSize    = MiB(512),
  kOverflowHeapSize = MiB(1),
  kResourceHeapSize = MiB(1),

  kTotalHeapSize    = kInitHeapSize + kDebugHeapSize + kFrameHeapSize + kOverflowHeapSize + kResourceHeapSize,
};

void reset_frame_heap();

