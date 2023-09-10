#include "profiling.h"
#include "option.h"


struct Profiler
{
  Option<PerformanceAPI_Functions> superluminal = None;
};

static Profiler g_profiler;

void
profiler::init()
{
  PerformanceAPI_Functions superluminal_funcs;
  auto ret = PerformanceAPI_LoadFrom(L"C:\\Program Files\\Superluminal\\Performance\\API\\dll\\x64\\PerformanceAPI.dll", &superluminal_funcs);
  if (ret)
  {
    g_profiler.superluminal = superluminal_funcs;
  }
  else
  {
    dbgln("Failed to load superluminal...");
  }
}

void
profiler::register_fiber(u64 fiber_id)
{
  if (!g_profiler.superluminal)
    return;

  PerformanceAPI_Functions& superluminal = unwrap(g_profiler.superluminal);
  superluminal.RegisterFiber(fiber_id);
}

void
profiler::unregister_fiber(u64 fiber_id)
{
  if (!g_profiler.superluminal)
    return;

  PerformanceAPI_Functions& superluminal = unwrap(g_profiler.superluminal);
  superluminal.UnregisterFiber(fiber_id);
}

void
profiler::begin_switch_to_fiber(u64 current_fiber, u64 other_fiber)
{
  if (!g_profiler.superluminal)
    return;

  PerformanceAPI_Functions& superluminal = unwrap(g_profiler.superluminal);
  superluminal.BeginFiberSwitch(current_fiber, other_fiber);
}

void
profiler::end_switch_to_fiber(u64 current_fiber)
{
  if (!g_profiler.superluminal)
    return;

  PerformanceAPI_Functions& superluminal = unwrap(g_profiler.superluminal);
  superluminal.EndFiberSwitch(current_fiber);
}
