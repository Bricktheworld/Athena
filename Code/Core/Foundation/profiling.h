#pragma once
#include "Core/Foundation/types.h"
#include "Core/Vendor/superluminal/PerformanceAPI_capi.h"
#include "Core/Vendor/superluminal/PerformanceAPI_loader.h"

namespace profiler
{
  FOUNDATION_API void init();
  FOUNDATION_API void register_fiber(u64 fiber_id);
  FOUNDATION_API void unregister_fiber(u64 fiber_id);
  FOUNDATION_API void begin_switch_to_fiber(u64 current_fiber, u64 other_fiber);
  FOUNDATION_API void end_switch_to_fiber(u64 current_fiber);

}

FOUNDATION_API u64 begin_cpu_profiler_timestamp(void);
FOUNDATION_API f64 end_cpu_profiler_timestamp(u64 start_time);

