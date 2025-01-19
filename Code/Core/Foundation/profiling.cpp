#include "Core/Foundation/profiling.h"
#include "Core/Foundation/Containers/option.h"


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

u64
begin_cpu_profiler_timestamp(void)
{
  LARGE_INTEGER li;
  QueryPerformanceCounter(&li);
  return (u64)li.QuadPart;
}

f64
end_cpu_profiler_timestamp(u64 start_time)
{
  LARGE_INTEGER li;

  ASSERT_MSG_FATAL(QueryPerformanceFrequency(&li), "Failed to query performance counter!");

  static f64 s_PCFrequencyMs = (f64)li.QuadPart / 1000.0;

  QueryPerformanceCounter(&li);

  return (f64)((u64)li.QuadPart - start_time) / s_PCFrequencyMs;
}
