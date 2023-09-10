#pragma once
#include "types.h"
#include "vendor/superluminal/PerformanceAPI_capi.h"
#include "vendor/superluminal/PerformanceAPI_loader.h"

namespace profiler
{
  void init();
  void register_fiber(u64 fiber_id);
  void unregister_fiber(u64 fiber_id);
  void begin_switch_to_fiber(u64 current_fiber, u64 other_fiber);
  void end_switch_to_fiber(u64 current_fiber);
}
