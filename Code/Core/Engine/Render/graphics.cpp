#include "Core/Foundation/context.h"
#include "Core/Foundation/colors.h"
#include "Core/Foundation/filesystem.h"
#include "Core/Foundation/assets.h"

#include "Core/Engine/memory.h"
#include "Core/Engine/job_system.h"
#include "Core/Engine/Render/graphics.h"
#include "Core/Engine/Render/frame_time.h"

#include "Core/Engine/Shaders/root_signature.hlsli"

#include "Core/Vendor/D3D12/d3dx12.h"

#include "Core/Engine/Vendor/imgui/imgui.h"
#include "Core/Engine/Vendor/imgui/imgui_impl_win32.h"
#include "Core/Engine/Vendor/imgui/imgui_impl_dx12.h"

#include "Core/Engine/Vendor/imgui/implot.h"

#include "Core/Engine/Vendor/NVAftermath/GFSDK_Aftermath.h"
#include "Core/Engine/Vendor/NVAftermath/GFSDK_Aftermath_GpuCrashDump.h"
#include "Core/Engine/Vendor/NVAftermath/GFSDK_Aftermath_GpuCrashDumpDecoding.h"

#include "Core/Engine/Vendor/NVAPI/nvapi.h"

#include <d3dcompiler.h>
#include <shellapi.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "Shell32.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "d3dcompiler.lib")

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 619;}
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = "."; }

#if defined(DEBUG)
#define DEBUG_LAYER
#endif

GpuDevice* g_GpuDevice   = nullptr;
u32        g_FrameId     = 0;
u32        g_DeltaTimeMs = 0;

static DXGI_FORMAT gpu_format_to_d3d12(GpuFormat format)
{
  return (DXGI_FORMAT)format;
}

static IDXGIFactory7*
init_factory()
{
  IDXGIFactory7* factory = nullptr;
  u32 create_factory_flags = 0;
#ifdef DEBUG_LAYER
  create_factory_flags = DXGI_CREATE_FACTORY_DEBUG;
#endif

  HASSERT(CreateDXGIFactory2(create_factory_flags, IID_PPV_ARGS(&factory)));
  ASSERT(factory != nullptr);

  return factory;
}

static s32
compute_desktop_intersect_aabb(RECT a, RECT b)
{
  return MAX(0, MIN(a.right, b.right) - MAX(a.left, b.left)) * MAX(0, MIN(a.bottom, b.bottom) - MAX(a.top, b.top));
}

static bool
check_hdr_support(IDXGIAdapter1* adapter, HWND window)
{
  IDXGIOutput* current_output = nullptr;
  IDXGIOutput* best_output    = nullptr;

  s32 best_area = -1;

  for (u32 i = 0; adapter->EnumOutputs(i, &current_output) != DXGI_ERROR_NOT_FOUND; i++)
  {
    RECT client_rect;
    GetClientRect(window, &client_rect);

    DXGI_OUTPUT_DESC desc;
    HASSERT(current_output->GetDesc(&desc));

    s32 current_area = compute_desktop_intersect_aabb(desc.DesktopCoordinates, client_rect);

    if (current_area > best_area)
    {
      COM_RELEASE(best_output);
      best_output = current_output;
      best_area   = current_area;
    }
    else
    {
      COM_RELEASE(current_output);
    }

    current_output = nullptr;
  }

  if (best_output == nullptr)
  {
    return false;
  }

  IDXGIOutput6* best_output6 = nullptr;
  best_output->QueryInterface(IID_PPV_ARGS(&best_output6));

  DXGI_OUTPUT_DESC1 desc;
  HASSERT(best_output6->GetDesc1(&desc));

  COM_RELEASE(best_output6);

  return desc.ColorSpace == DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020;
}

static void
init_d3d12_device(HWND window, IDXGIFactory7* factory, IDXGIAdapter1** out_adapter, ID3D12Device15** out_device, wchar_t out_name[128])
{
  *out_adapter = nullptr;
  *out_device  = nullptr;
  size_t max_dedicated_vram = 0;
  bool   hdr_support        = false;
  IDXGIAdapter1* current_adapter = nullptr;
  for (u32 i = 0; factory->EnumAdapters1(i, &current_adapter) != DXGI_ERROR_NOT_FOUND; i++)
  {
    DXGI_ADAPTER_DESC1 dxgi_adapter_desc = {0};
    ID3D12Device15* current_device = nullptr;
    HASSERT(current_adapter->GetDesc1(&dxgi_adapter_desc));

    if ((dxgi_adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 || 
      FAILED(D3D12CreateDevice(current_adapter, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device15), (void**)&current_device)) ||
      dxgi_adapter_desc.DedicatedVideoMemory <= max_dedicated_vram)
    {
      COM_RELEASE(current_device);
      COM_RELEASE(current_adapter);
      continue;
    }

    max_dedicated_vram = dxgi_adapter_desc.DedicatedVideoMemory;
    hdr_support       |= check_hdr_support(current_adapter, window);
    COM_RELEASE((*out_adapter));
    COM_RELEASE((*out_device));
    *out_adapter = current_adapter;
    *out_device  = current_device;

    memcpy(out_name, dxgi_adapter_desc.Description, 128 * sizeof(wchar_t));

    current_adapter = nullptr;
  }

  ASSERT(*out_adapter != nullptr && *out_device != nullptr);
}

static bool
check_tearing_support(IDXGIFactory7* factory)
{
  BOOL allow_tearing = FALSE;

  if (FAILED(factory->QueryInterface(IID_PPV_ARGS(&factory))))
    return false;

  if (FAILED(factory->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow_tearing, sizeof(allow_tearing))))
    return false;

  COM_RELEASE(factory);

  return allow_tearing == TRUE;
}

static D3D12_COMMAND_LIST_TYPE
get_d3d12_cmd_list_type(CmdQueueType type)
{
  switch(type)
  {
    case kCmdQueueTypeGraphics: return D3D12_COMMAND_LIST_TYPE_DIRECT;
    case kCmdQueueTypeCompute:  return D3D12_COMMAND_LIST_TYPE_COMPUTE;
    case kCmdQueueTypeCopy:     return D3D12_COMMAND_LIST_TYPE_COPY;
    default: UNREACHABLE;
  }
}

u64
get_gpu_memory_usage()
{
  DXGI_QUERY_VIDEO_MEMORY_INFO info;
  g_GpuDevice->dxgi_adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info);

  return info.CurrentUsage;
}

u64
get_gpu_memory_budget()
{
  DXGI_QUERY_VIDEO_MEMORY_INFO info;
  g_GpuDevice->dxgi_adapter->QueryVideoMemoryInfo(0, DXGI_MEMORY_SEGMENT_GROUP_LOCAL, &info);

  return info.Budget;
}

static ID3D12Fence* 
init_gpu_fence(ID3D12Device2* d3d12_dev)
{
  ID3D12Fence* fence = nullptr;
  HASSERT(d3d12_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
  ASSERT(fence != nullptr);

  return fence;
}

GpuFence
init_gpu_fence()
{
  GpuFence ret = {0};
  HASSERT(g_GpuDevice->d3d12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&ret.d3d12_fence)));
  ASSERT(ret.d3d12_fence != nullptr);

  ret.cpu_event = CreateEventW(NULL, FALSE, FALSE, NULL);
  ret.value = 0;
  ret.last_completed_value = 0;
  ret.last_completed_value = 0;
  ret.already_waiting = false;

  return ret;
}

void
destroy_gpu_fence(GpuFence* fence)
{
  CloseHandle(fence->cpu_event);
  COM_RELEASE(fence->d3d12_fence);
  zero_memory(fence, sizeof(GpuFence));
}

static FenceValue
inc_fence(GpuFence* fence)
{
  return ++fence->value;
}

FenceValue
poll_gpu_fence_value(GpuFence* fence)
{
  fence->last_completed_value = MAX(fence->last_completed_value, fence->d3d12_fence->GetCompletedValue());
  return fence->last_completed_value;
}

bool
is_gpu_fence_complete(GpuFence* fence, FenceValue value)
{
  if (value > fence->last_completed_value)
  {
    poll_gpu_fence_value(fence);
  }

  return value <= fence->last_completed_value;
}

void
block_gpu_fence(GpuFence* fence, FenceValue value)
{
  // If you hit this assertion, it's because only a single thread can wait on a fence at a time
  ASSERT(!fence->already_waiting);
  if (is_gpu_fence_complete(fence, value))
    return;

  HASSERT(fence->d3d12_fence->SetEventOnCompletion(value, fence->cpu_event));
  fence->already_waiting = true;

  WaitForSingleObject(fence->cpu_event, (DWORD)-1);

  if (g_GpuDevice->flags & kGpuFlagsEnableRtValidation)
  {
    ID3D12Device5* device5 = nullptr;
    defer { COM_RELEASE(device5); };
    g_GpuDevice->d3d12->QueryInterface(IID_PPV_ARGS(&device5));
    NvAPI_D3D12_FlushRaytracingValidationMessages(device5);
  }

  poll_gpu_fence_value(fence);
  fence->already_waiting = false;
}

CmdQueue
init_cmd_queue(const GpuDevice* device, CmdQueueType type)
{
  CmdQueue ret = {0};

  D3D12_COMMAND_QUEUE_DESC desc = { };
  desc.Type = get_d3d12_cmd_list_type(type);
  desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
  desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  desc.NodeMask = 0;

  HASSERT(device->d3d12->CreateCommandQueue(&desc, IID_PPV_ARGS(&ret.d3d12_queue)));
  ASSERT(ret.d3d12_queue != nullptr);
  ret.type = type;

  return ret;
}

void
destroy_cmd_queue(CmdQueue* queue)
{
  COM_RELEASE(queue->d3d12_queue);

  zero_memory(queue, sizeof(CmdQueue));
}

void
cmd_queue_gpu_wait_for_fence(const CmdQueue* queue, GpuFence* fence, FenceValue value)
{
  HASSERT(queue->d3d12_queue->Wait(fence->d3d12_fence, value));
}

FenceValue
cmd_queue_signal(const CmdQueue* queue, GpuFence* fence)
{
  FenceValue value = inc_fence(fence);
  HASSERT(queue->d3d12_queue->Signal(fence->d3d12_fence, value));
  return value;
}


CmdListAllocator
init_cmd_list_allocator(
  AllocHeap heap,
  const GpuDevice* device,
  const CmdQueue* queue,
  u16 pool_size
) {
  ASSERT(pool_size > 0);
  CmdListAllocator ret = {0};
  ret.d3d12_queue = queue->d3d12_queue;
  ret.fence = init_gpu_fence();
  ret.allocators = init_ring_queue<CmdAllocator>(heap, pool_size);
  ret.lists = init_ring_queue<ID3D12GraphicsCommandList7*>(heap, pool_size);

  CmdAllocator allocator = {0};
  for (u16 i = 0; i < pool_size; i++)
  {
    HASSERT(
      device->d3d12->CreateCommandAllocator(
        get_d3d12_cmd_list_type(queue->type),
        IID_PPV_ARGS(&allocator.d3d12_allocator)
      )
    );
    allocator.fence_value = 0;
    ring_queue_push(&ret.allocators, allocator);
  }

  for (u16 i = 0; i < pool_size; i++)
  {
    ID3D12GraphicsCommandList7* list = nullptr;
    HASSERT(
      device->d3d12->CreateCommandList(
        0,
        get_d3d12_cmd_list_type(queue->type),
        allocator.d3d12_allocator,
        nullptr,
        IID_PPV_ARGS(&list)
      )
    );
    list->Close();
    ring_queue_push(&ret.lists, list);
  }


  return ret;
}

void
destroy_cmd_list_allocator(CmdListAllocator* allocator)
{
  destroy_gpu_fence(&allocator->fence);

  while (!ring_queue_is_empty(allocator->lists))
  {
    ID3D12GraphicsCommandList7* list = nullptr;
    ring_queue_pop(&allocator->lists, &list);
    COM_RELEASE(list);
  }

  while (!ring_queue_is_empty(allocator->allocators))
  {
    CmdAllocator cmd_allocator = {0};
    ring_queue_pop(&allocator->allocators, &cmd_allocator);
    COM_RELEASE(cmd_allocator.d3d12_allocator);
  }
}

CmdList
alloc_cmd_list(CmdListAllocator* allocator)
{
  CmdList ret = {0};
  CmdAllocator cmd_allocator = {0};
  ring_queue_pop(&allocator->allocators, &cmd_allocator);

  block_gpu_fence(&allocator->fence, cmd_allocator.fence_value);

  ring_queue_pop(&allocator->lists, &ret.d3d12_list);

  ret.d3d12_allocator = cmd_allocator.d3d12_allocator;

  ret.d3d12_allocator->Reset();
  ret.d3d12_list->Reset(ret.d3d12_allocator, nullptr);

  return ret;
}

FenceValue
submit_cmd_lists(CmdListAllocator* allocator, Span<CmdList> lists, Option<GpuFence*> fence)
{
  FenceValue ret = 0;

  ScratchAllocator scratch_arena = alloc_scratch_arena();
  defer { free_scratch_arena(&scratch_arena); };
  auto d3d12_cmd_lists = init_array<ID3D12CommandList*>(scratch_arena, lists.size);

  for (CmdList list : lists)
  {
    list.d3d12_list->Close();
    *array_add(&d3d12_cmd_lists) = list.d3d12_list;
  }

  allocator->d3d12_queue->ExecuteCommandLists(static_cast<u32>(d3d12_cmd_lists.size), d3d12_cmd_lists.memory);

  FenceValue value = inc_fence(&allocator->fence);
  HASSERT(allocator->d3d12_queue->Signal(allocator->fence.d3d12_fence, value));
  if (fence)
  {
    ret = inc_fence(unwrap(fence));
    HASSERT(allocator->d3d12_queue->Signal(unwrap(fence)->d3d12_fence, ret));
  }

  for (CmdList list : lists)
  {
    CmdAllocator cmd_allocator = {0};
    cmd_allocator.d3d12_allocator = list.d3d12_allocator;
    cmd_allocator.fence_value = value;
    ring_queue_push(&allocator->allocators, cmd_allocator);
    ring_queue_push(&allocator->lists, list.d3d12_list);
  }

  return ret;
}


static D3D12_HEAP_TYPE
get_d3d12_heap_location(GpuHeapLocation type)
{
  switch(type)
  {
    case kGpuHeapGpuOnly:        return D3D12_HEAP_TYPE_DEFAULT;
    case kGpuHeapSysRAMCpuToGpu: return D3D12_HEAP_TYPE_UPLOAD;
    case kGpuHeapVRAMCpuToGpu:   return D3D12_HEAP_TYPE_GPU_UPLOAD;
    case kGpuHeapSysRAMGpuToCpu: return D3D12_HEAP_TYPE_READBACK;
    default: UNREACHABLE;
  }
}

GpuPhysicalMemory
alloc_gpu_physical_memory(u64 size, GpuHeapLocation location)
{
  GpuPhysicalMemory ret = {0};

  D3D12_HEAP_DESC desc = {0};
  desc.SizeInBytes = size;
  desc.Properties  = CD3DX12_HEAP_PROPERTIES(get_d3d12_heap_location(location));

  // TODO(Brandon): If we ever do MSAA textures then this needs to change.
  desc.Alignment = KiB(64);
  desc.Flags     = D3D12_HEAP_FLAG_NONE;

  ret.size       = size;
  ret.location   = location;

  HASSERT(g_GpuDevice->d3d12->CreateHeap(&desc, IID_PPV_ARGS(&ret.d3d12_heap)));
  return ret;
}

void
free_gpu_physical_memory(GpuPhysicalMemory* memory)
{
  COM_RELEASE(memory->d3d12_heap);
  zero_memory(memory, sizeof(GpuPhysicalMemory));
}

void
fill_gpu_physical_allocation_struct(GpuAllocation* dst, const GpuPhysicalMemory& memory, u64 offset, u64 size, u32 metadata)
{
  dst->size       = size;
  dst->offset     = offset;
  dst->d3d12_heap = memory.d3d12_heap;
  dst->metadata   = metadata;
  dst->location   = memory.location;
}

GpuLinearAllocator
init_gpu_linear_allocator(u32 size, GpuHeapLocation location)
{
  GpuLinearAllocator ret = {0};
  ret.pos                = 0;
  ret.physical_memory    = alloc_gpu_physical_memory(size, location);

  return ret;
}

void
destroy_gpu_linear_allocator(GpuLinearAllocator* allocator)
{
  free_gpu_physical_memory(&allocator->physical_memory);
  zero_memory(allocator, sizeof(GpuLinearAllocator));
}

GpuAllocation
gpu_linear_alloc(void* allocator, u32 size, u32 alignment)
{
  GpuLinearAllocator* self    = (GpuLinearAllocator*)allocator;
  u32                 offset  = ALIGN_POW2(self->pos, alignment);
  u32                 new_pos = offset + size;

  ASSERT_MSG_FATAL(new_pos <= self->physical_memory.size, "GPU linear allocator ran out of memory! Attempted to allocate 0x%x bytes, 0x%x bytes available", size, self->physical_memory.size - self->pos);

  self->pos = new_pos;



  GpuAllocation ret = {0};
  fill_gpu_physical_allocation_struct(&ret, self->physical_memory, offset, size);

  return ret;
}

static void
init_gpu_profiler(void)
{
  GpuProfiler* profiler = &g_GpuDevice->profiler;

  D3D12_QUERY_HEAP_DESC desc = {};
  desc.Type     = D3D12_QUERY_HEAP_TYPE_TIMESTAMP;
  desc.Count    = kMaxGpuTimestamps * 2;
  desc.NodeMask = 0;

  g_GpuDevice->d3d12->CreateQueryHeap(&desc, IID_PPV_ARGS(&profiler->d3d12_timestamp_heap));
  GpuBufferDesc readback_desc = {};
  readback_desc.size          = sizeof(u64) * desc.Count * kBackBufferCount;

  profiler->timestamp_readback = alloc_gpu_buffer_no_heap(g_GpuDevice, readback_desc, kGpuHeapSysRAMGpuToCpu, "Timestamp readback");
  profiler->name_to_timestamp  = init_hash_table<const char*, u32>(g_InitHeap, kMaxGpuTimestamps);
  zero_memory(profiler->timestamps, sizeof(profiler->timestamps));


  profiler->next_free_idx      = 0;
  HASSERT(g_GpuDevice->graphics_queue.d3d12_queue->GetTimestampFrequency(&profiler->gpu_frequency));
}

void
begin_gpu_profiler_timestamp(CmdList* cmd_buffer, const char* name, u32 tag)
{
  GpuProfiler* profiler = &g_GpuDevice->profiler;

  u32 idx = 0;
  {
    u32* existing = hash_table_find(&profiler->name_to_timestamp, name + tag);
    if (existing == nullptr)
    {
      ASSERT_MSG_FATAL(
        profiler->next_free_idx < kMaxGpuTimestamps,
        "Attempting to start a GPU timestamp name %s but no slots left! You are permitted up to %u GPU timestamps. If you need more, try incrementing kMaxGpuTimestamps", name, kMaxGpuTimestamps
      );
      u32* dst = hash_table_insert(&profiler->name_to_timestamp, name + tag);
      *dst = profiler->next_free_idx++;
      existing = dst;
    }

    idx = *existing;
  }

  GpuTimestamp* timestamp = profiler->timestamps + idx;

  ASSERT_MSG_FATAL(!timestamp->in_flight, "GPU profiler timestamp name '%s[%u]' wasn't closed properly!", name, tag);

  u32 start_idx = idx * 2;
  cmd_buffer->d3d12_list->EndQuery(profiler->d3d12_timestamp_heap, D3D12_QUERY_TYPE_TIMESTAMP, start_idx);

  timestamp->in_flight = true;
}

void
end_gpu_profiler_timestamp(CmdList* cmd_buffer, const char* name, u32 tag)
{
  GpuProfiler* profiler = &g_GpuDevice->profiler;

  u32* ptr = hash_table_find(&profiler->name_to_timestamp, name + tag);

  ASSERT_MSG_FATAL(ptr != nullptr, "GPU profiler timestamp name '%s[%u]' is invalid! Did you forget to call begin_gpu_profiler_timestamp?", name, tag);

  u32 idx       = *ptr;
  u32 start_idx = idx * 2;
  u32 end_idx   = start_idx + 1;

  GpuTimestamp* timestamp = profiler->timestamps + idx;

  cmd_buffer->d3d12_list->EndQuery(profiler->d3d12_timestamp_heap, D3D12_QUERY_TYPE_TIMESTAMP, end_idx);

  u64 dst_offset = sizeof(u64) * ((g_FrameId % kBackBufferCount) * kMaxGpuTimestamps * 2 + start_idx);

  gpu_memory_barrier(cmd_buffer);
  cmd_buffer->d3d12_list->ResolveQueryData(
    profiler->d3d12_timestamp_heap,
    D3D12_QUERY_TYPE_TIMESTAMP,
    start_idx,
    2,
    profiler->timestamp_readback.d3d12_buffer,
    dst_offset
  );

  ASSERT_MSG_FATAL(timestamp->in_flight, "GPU profiler timestamp name '%s' was never started! Did you forget to call begin_gpu_profiler_timestamp?", name);
  timestamp->in_flight = false;
}

bool
has_gpu_profiler_timestamp(const char* name, u32 tag)
{
  GpuProfiler* profiler = &g_GpuDevice->profiler;
  u32* ptr = hash_table_find(&profiler->name_to_timestamp, name + tag);
  return ptr != nullptr;
}

f64
query_gpu_profiler_timestamp(const char* name, u32 tag)
{
  if (g_FrameId < 1)
  {
    return 0.0;
  }

  GpuProfiler* profiler = &g_GpuDevice->profiler;

  // TODO(bshihabi): Currently these time samples have a 1 frame delay.
  // I designed it like this because the render graph won't actually have the query
  // data because command lists are submitted all at once. This needs to be fixed first.

  u32* ptr = hash_table_find(&profiler->name_to_timestamp, name + tag);

  ASSERT_MSG_FATAL(ptr != nullptr, "GPU profiler timestamp name '%s[%u]' is invalid! Did you forget to call begin_gpu_profiler_timestamp?", name + tag);

  u32 idx       = *ptr;
  u32 start_idx = idx * 2;
  u32 end_idx   = start_idx + 1;

  u64 src_offset        = sizeof(u64) * (((g_FrameId - 1) % kBackBufferCount) * kMaxGpuTimestamps * 2);
  const u64* query_data = (const u64*)((const u8*)unwrap(profiler->timestamp_readback.mapped) + src_offset);

  u64 start_time        = query_data[start_idx];
  u64 end_time          = query_data[end_idx];

  u64 delta_time        = end_time - start_time;
  return ((f64)delta_time / (f64)profiler->gpu_frequency) * 1000.0;
  
}

static ID3D12CommandSignature* g_MultiDrawIndirectSignature = nullptr;
static ID3D12CommandSignature* g_DispatchIndirectSignature  = nullptr;
static ID3D12RootSignature*    g_RootSignature              = nullptr;

static void
init_d3d12_indirect()
{
  {
    D3D12_INDIRECT_ARGUMENT_DESC arg_descs[2]                 = {};
    arg_descs[0].Type                                         = D3D12_INDIRECT_ARGUMENT_TYPE_INCREMENTING_CONSTANT;
    arg_descs[0].IncrementingConstant.RootParameterIndex      = kIndirectCommandIndexSlot;
    arg_descs[0].IncrementingConstant.DestOffsetIn32BitValues = 0;

    arg_descs[1].Type                                         = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

    D3D12_COMMAND_SIGNATURE_DESC desc = {};
    desc.ByteStride       = sizeof(MultiDrawIndirectArgs);
    desc.NumArgumentDescs = ARRAY_LENGTH(arg_descs);
    desc.pArgumentDescs   = arg_descs;
    desc.NodeMask         = 0;
    static_assert(sizeof(MultiDrawIndirectArgs) >= sizeof(D3D12_DRAW_ARGUMENTS));

    HASSERT(
      g_GpuDevice->d3d12->CreateCommandSignature(
        &desc,
        g_RootSignature,
        IID_PPV_ARGS(&g_GpuDevice->d3d12_multi_draw_indirect_signature)
      )
    );
  }

  {
    D3D12_INDIRECT_ARGUMENT_DESC arg_descs[2]                 = {};
    arg_descs[0].Type                                         = D3D12_INDIRECT_ARGUMENT_TYPE_INCREMENTING_CONSTANT;
    arg_descs[0].IncrementingConstant.RootParameterIndex      = kIndirectCommandIndexSlot;
    arg_descs[0].IncrementingConstant.DestOffsetIn32BitValues = 0;

    arg_descs[1].Type                                         = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

    D3D12_COMMAND_SIGNATURE_DESC desc = {};
    desc.ByteStride       = sizeof(MultiDrawIndirectIndexedArgs);
    desc.NumArgumentDescs = ARRAY_LENGTH(arg_descs);
    desc.pArgumentDescs   = arg_descs;
    desc.NodeMask         = 0;
    static_assert(sizeof(MultiDrawIndirectIndexedArgs) >= sizeof(D3D12_DRAW_INDEXED_ARGUMENTS));

    HASSERT(
      g_GpuDevice->d3d12->CreateCommandSignature(
        &desc,
        g_RootSignature,
        IID_PPV_ARGS(&g_GpuDevice->d3d12_multi_draw_indirect_indexed_signature)
      )
    );
  }

  {
    D3D12_INDIRECT_ARGUMENT_DESC arg_desc = {};
    arg_desc.Type         = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH;

    D3D12_COMMAND_SIGNATURE_DESC desc = {};
    desc.ByteStride       = sizeof(DispatchIndirectArgs);
    desc.NumArgumentDescs = 1;
    desc.pArgumentDescs   = &arg_desc;
    desc.NodeMask         = 0;
    static_assert(sizeof(DispatchIndirectArgs) >= sizeof(D3D12_DISPATCH_ARGUMENTS));

    HASSERT(
      g_GpuDevice->d3d12->CreateCommandSignature(
        &desc,
        nullptr,
        IID_PPV_ARGS(&g_GpuDevice->d3d12_dispatch_indirect_signature)
      )
    );
  }
}

static void
init_root_signature(const GpuDevice* device, ID3DBlob* blob)
{
  if (g_RootSignature == nullptr)
  {
    ID3DBlob* root_signature_blob = nullptr;
    defer { COM_RELEASE(root_signature_blob); };

    HASSERT(
      D3DGetBlobPart(
        blob->GetBufferPointer(),
        blob->GetBufferSize(),
        D3D_BLOB_ROOT_SIGNATURE,
        0,
        &root_signature_blob
      )
    );
    device->d3d12->CreateRootSignature(
      0,
      root_signature_blob->GetBufferPointer(),
      root_signature_blob->GetBufferSize(),
      IID_PPV_ARGS(&g_RootSignature)
    );

    init_d3d12_indirect();
  }
}


struct AftermathManager
{
  HashTable<u64, D3D12_SHADER_BYTECODE> shader_database;
  LinearAllocator                                                    allocator;

  char   crash_dump_path[kMaxPathLength];
  void*  crash_dump;
  u32    crash_dump_size;

};

static AftermathManager* g_AftermathManager = nullptr;

static bool
register_aftermath_shader(const u8* src, size_t size)
{
  AftermathManager* aftermath = g_AftermathManager;
  // Can't register more than some maximum number of shaders
  if (aftermath->shader_database.used == aftermath->shader_database.capacity)
  {
    dbgln("AFTERMATH: Shader Database maximum entries allowed! %llu", aftermath->shader_database.capacity);
    return false;
  }

  // Can't allocate the buffer, not catastrophic but just no crash info
  if (size >= available_memory(&aftermath->allocator))
  {
    dbgln("AFTERMATH: Attempted to allocate %llu bytes from aftermath allocator but only %llu available.", size, available_memory(&aftermath->allocator));
    return false;
  }

  u8* bytecode_buffer = HEAP_ALLOC_ALIGNED((AllocHeap)aftermath->allocator, size, alignof(u32));
  memcpy(bytecode_buffer, src, size);

  D3D12_SHADER_BYTECODE bytecode;
  bytecode.pShaderBytecode = bytecode_buffer;
  bytecode.BytecodeLength  = size;

  GFSDK_Aftermath_ShaderBinaryHash aftermath_hash;
  GFSDK_Aftermath_GetShaderHash(GFSDK_Aftermath_Version_API, &bytecode, &aftermath_hash);

  *hash_table_insert(&aftermath->shader_database, aftermath_hash.hash) = bytecode;

  return true;
}

// This should be called before any D3D12 devices are initialized
static void
enable_aftermath_gpu_crash_dumps()
{
  g_AftermathManager = HEAP_ALLOC(AftermathManager, g_InitHeap, 1);
  zero_memory(g_AftermathManager->crash_dump_path, sizeof(g_AftermathManager->crash_dump_path));
  g_AftermathManager->crash_dump      = nullptr;
  g_AftermathManager->crash_dump_size = 0;

  static u32 kAftermathHeapSize = MiB(4);

  g_AftermathManager->allocator       = init_linear_allocator(HEAP_ALLOC_ALIGNED(g_InitHeap, kAftermathHeapSize, 16), kAftermathHeapSize);
  // The shader database should be aware of 
  g_AftermathManager->shader_database = init_hash_table<u64, D3D12_SHADER_BYTECODE>(g_AftermathManager->allocator, 1024);

  auto gpu_crash_dump_callback = [](const void* gpu_crash_dump, const u32 gpu_crash_dump_size, void* user)
  {
    AftermathManager* manager = (AftermathManager*)user;

    for (auto [hash, bytecode] : manager->shader_database)
    {
      char path[kMaxPathLength];
      snprintf(path, sizeof(path), "GpuDumps/Shaders/shader-%llu.cso", hash);

      Result<FileStream, FileError> res = create_file(path, kCreateTruncateExisting);
      if (!res)
      {
        dbgln("AFTERMATH ERROR: Failed to create shader bytecode file %s: %s", path, file_error_to_str(res.error()));
        continue;
      }

      FileStream file = res.value();
      defer { close_file(&file); };

      write_file(file, bytecode.pShaderBytecode, bytecode.BytecodeLength);
    }

    // TODO(bshihabi): make this platform independent
    SYSTEMTIME st;
    GetLocalTime(&st);

    snprintf(
      manager->crash_dump_path,
      sizeof(manager->crash_dump_path),
      "GpuDumps\\%04d-%02d-%02d_%02d-%02d-%02d.nv-gpudmp",
      st.wYear,
      st.wMonth,
      st.wDay,
      st.wHour,
      st.wMinute,
      st.wSecond
    );

    manager->crash_dump_size = gpu_crash_dump_size;
    manager->crash_dump      = reserve_commit_pages(gpu_crash_dump_size);
    if (manager->crash_dump)
    {
      memcpy(manager->crash_dump, gpu_crash_dump, manager->crash_dump_size);
    }
    else
    {
      dbgln("Failed to allocate pages for GPU crash dump of size %u in memory!", manager->crash_dump_size);
    }

    Result<FileStream, FileError> res = create_file(manager->crash_dump_path, kFileCreateFlagsNone);
    if (!res)
    {
      ASSERT_MSG_FATAL(false, "GPU Crash Dump failed to create file! %s", file_error_to_str(res.error()));
      return;
    }

    FileStream file = res.value();
    defer { close_file(&file); };

    write_file(file, gpu_crash_dump, gpu_crash_dump_size);
  };

  auto shader_debug_info_callback = [](const void* shader_debug_info, const u32 shader_debug_info_size, void*)
  {
    GFSDK_Aftermath_ShaderDebugInfoIdentifier identifier;
    GFSDK_Aftermath_Result result = GFSDK_Aftermath_GetShaderDebugInfoIdentifier(GFSDK_Aftermath_Version_API, shader_debug_info, shader_debug_info_size, &identifier);
    if (!GFSDK_Aftermath_SUCCEED(result))
    {
      dbgln("AFTERMATH ERROR: Failed to get shader debug info identifier!");
    }
    else
    {
      char path[kMaxPathLength];
      snprintf(path, sizeof(path), "GpuDumps/Shaders/shader-%llu-%llu.nvdbg", identifier.id[0], identifier.id[1]);

      Result<FileStream, FileError> res = create_file(path, kCreateTruncateExisting);
      if (!res)
      {
        dbgln("AFTERMATH ERROR: Failed to create shader debug file %s: %s", path, file_error_to_str(res.error()));
        return;
      }

      FileStream file = res.value();
      defer { close_file(&file); };

      write_file(file, shader_debug_info, shader_debug_info_size);
    }
  };


  GFSDK_Aftermath_Result res = GFSDK_Aftermath_EnableGpuCrashDumps(
    GFSDK_Aftermath_Version_API,
    GFSDK_Aftermath_GpuCrashDumpWatchedApiFlags_DX,
    GFSDK_Aftermath_GpuCrashDumpFeatureFlags_Default,   // Default behavior.
    gpu_crash_dump_callback,                            // Register callback for GPU crash dumps.
    shader_debug_info_callback,                         // Register callback for shader debug information.
    nullptr,                                            // Register callback for GPU crash dump description.
    nullptr,                                            // Register callback for marker resolution (R495 or later NVIDIA graphics driver).
    g_AftermathManager
  );

  if (res == GFSDK_Aftermath_Result_FAIL_InvalidAdapter)
  {
    dbgln("AFTERMATH ERROR: Only NVIDIA GPUs are supported. Failed to enable.");
    g_AftermathManager = nullptr;
    return;
  }

  ASSERT_MSG_FATAL(GFSDK_Aftermath_SUCCEED(res), "AFTERMATH ERROR: Failed to enable GPU crash dumps!");
}

static void
init_aftermath(GpuDevice* device)
{
  u32 flags = GFSDK_Aftermath_FeatureFlags_EnableMarkers           |
              GFSDK_Aftermath_FeatureFlags_CallStackCapturing      |
              GFSDK_Aftermath_FeatureFlags_EnableResourceTracking  |
              GFSDK_Aftermath_FeatureFlags_GenerateShaderDebugInfo |
              GFSDK_Aftermath_FeatureFlags_EnableShaderErrorReporting;
  GFSDK_Aftermath_Result res = GFSDK_Aftermath_DX12_Initialize(
    GFSDK_Aftermath_Version_API,
    flags,
    device->d3d12
  );
  ASSERT_MSG_FATAL(GFSDK_Aftermath_SUCCEED(res), "Aftermath failed to initialize DX12");
}

static void __stdcall
nvapi_validation_callback(void*, NVAPI_D3D12_RAYTRACING_VALIDATION_MESSAGE_SEVERITY severity, const char* message_code, const char* message, const char* message_details)
{
  const char* severity_string = "unknown";
  switch (severity)
  {
    case NVAPI_D3D12_RAYTRACING_VALIDATION_MESSAGE_SEVERITY_ERROR:   
    {
      severity_string = "error";
      ASSERT_MSG_FATAL(false, "Ray Tracing Validation message: %s: [%s] %s\n%s", severity_string, message_code, message, message_details);
    } break;
    case NVAPI_D3D12_RAYTRACING_VALIDATION_MESSAGE_SEVERITY_WARNING: severity_string = "warning"; break;
  }
  dbgln("Ray Tracing Validation message: %s: [%s] %s\n%s", severity_string, message_code, message, message_details);
}

static void
init_nvapi()
{
  // Load NVAPI
  NvAPI_Status res = NvAPI_Initialize();
  if (res == NVAPI_NVIDIA_DEVICE_NOT_FOUND)
  {
    // We don't care if it's not NVIDIA
    return;
  }
  else if (res != NVAPI_OK)
  {
    dbgln("Failed to initialize NvAPI: %d", res);
  }

  if (g_GpuDevice->flags & kGpuFlagsEnableRtValidation)
  {
    ID3D12Device5* device5 = nullptr;
    defer { COM_RELEASE(device5); };
    g_GpuDevice->d3d12->QueryInterface(IID_PPV_ARGS(&device5));
    res = NvAPI_D3D12_EnableRaytracingValidation(device5, NVAPI_D3D12_RAYTRACING_VALIDATION_FLAG_NONE);
    ASSERT_MSG_FATAL(res == NVAPI_OK, "Failed to enable raytracing validation flags! Error: %d", res);
    void* handle = nullptr;
    res = NvAPI_D3D12_RegisterRaytracingValidationMessageCallback(device5, &nvapi_validation_callback, nullptr, &handle);
    ASSERT_MSG_FATAL(res == NVAPI_OK, "Failed to register raytracing message callback! Error: %d", res);
  }
}

static void
check_feature_support()
{
  D3D12_FEATURE_DATA_D3D12_OPTIONS21 features;
  g_GpuDevice->d3d12->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS21, &features, sizeof(features));
  ASSERT_MSG_FATAL(features.ExecuteIndirectTier == D3D12_EXECUTE_INDIRECT_TIER_1_1, "DrawID not supported on GPU!");
}

void
init_gpu_device(HWND window, u32 flags)
{
  if (g_GpuDevice == nullptr)
  {
    g_GpuDevice = HEAP_ALLOC(GpuDevice, g_InitHeap, 1);
  }

  g_GpuDevice->flags = flags;

#if !defined(DEBUG_LAYER)
  ASSERT_MSG_FATAL((flags & kGpuFlagsEnableValidationLayers) == 0, "Debug layers not supported! Make sure that you are compiling in debug mode with DEBUG_LAYERS macro enabled in graphics.cpp");
#endif

  enable_aftermath_gpu_crash_dumps();

#if defined(DEBUG_LAYER)
  if (flags & kGpuFlagsEnableValidationLayers)
  {
    ID3D12Debug*  debug_interface  = nullptr;
    HASSERT(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_interface)));
    debug_interface->EnableDebugLayer();
    defer { COM_RELEASE(debug_interface); };

    // Enable DRED
    ID3D12DeviceRemovedExtendedDataSettings1* dred_settings = nullptr;
    HASSERT(D3D12GetDebugInterface(IID_PPV_ARGS(&dred_settings)));

    dred_settings->SetAutoBreadcrumbsEnablement  (D3D12_DRED_ENABLEMENT_FORCED_ON);
    dred_settings->SetBreadcrumbContextEnablement(D3D12_DRED_ENABLEMENT_FORCED_ON);
    dred_settings->SetPageFaultEnablement        (D3D12_DRED_ENABLEMENT_FORCED_ON);

    if (flags & kGpuFlagsEnableGpuValidation)
    {
      ID3D12Debug1* debug_interface1 = nullptr;
      HASSERT(debug_interface->QueryInterface(IID_PPV_ARGS(&debug_interface1)));
      debug_interface1->SetEnableGPUBasedValidation(true);
    }

    PIXLoadLatestWinPixGpuCapturerLibrary();
    PIXSetTargetWindow(window);

  }
#endif

  IDXGIFactory7* factory = init_factory();
  defer { COM_RELEASE(factory); };

  IDXGIAdapter1* adapter; 
  init_d3d12_device(window, factory, &adapter, &g_GpuDevice->d3d12, g_GpuDevice->gpu_name);
  defer { COM_RELEASE(adapter); };

  check_feature_support();

  init_nvapi();

  // FOR DEVELOPMENT ONLY: Forces a stable power usage that way we can do profiling correctly
  if (flags & kGpuFlagsEnableDevelopmentStablePower)
  {
    g_GpuDevice->d3d12->SetStablePowerState(true);
  }

  HASSERT(adapter->QueryInterface(IID_PPV_ARGS(&g_GpuDevice->dxgi_adapter)));

  g_GpuDevice->graphics_queue = init_cmd_queue(g_GpuDevice, kCmdQueueTypeGraphics);

  g_GpuDevice->graphics_cmd_allocator = init_cmd_list_allocator(
    g_InitHeap,
    g_GpuDevice,
    &g_GpuDevice->graphics_queue,
    kBackBufferCount * 16
  );
  g_GpuDevice->compute_queue = init_cmd_queue(g_GpuDevice, kCmdQueueTypeCompute);
  g_GpuDevice->compute_cmd_allocator = init_cmd_list_allocator(
    g_InitHeap,
    g_GpuDevice,
    &g_GpuDevice->compute_queue,
    kBackBufferCount * 8
  );
  g_GpuDevice->copy_queue = init_cmd_queue(g_GpuDevice, kCmdQueueTypeCopy);
  g_GpuDevice->copy_cmd_allocator = init_cmd_list_allocator(
    g_InitHeap,
    g_GpuDevice,
    &g_GpuDevice->copy_queue,
    kBackBufferCount * 8
  );

  init_gpu_profiler();

#ifdef DEBUG_LAYER
  if (flags & kGpuFlagsEnableValidationLayers)
  {
    HASSERT(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&g_GpuDevice->dxgi_debug)));
    HASSERT(g_GpuDevice->d3d12->QueryInterface(IID_PPV_ARGS(&g_GpuDevice->d3d12_info_queue)));
    HASSERT(g_GpuDevice->d3d12_info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true));
    HASSERT(g_GpuDevice->d3d12_info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR,      true));
    HASSERT(g_GpuDevice->d3d12_info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING,    true));
    HASSERT(g_GpuDevice->d3d12_info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_INFO,       true));

    HASSERT(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&g_GpuDevice->dxgi_info_queue)));

    // We don't want these to break because it makes it confusing. We just want the crash to be handled by aftermath and dred and caught during that one spot in present
    HASSERT(g_GpuDevice->d3d12_info_queue->SetBreakOnID(D3D12_MESSAGE_ID_DEVICE_REMOVAL_PROCESS_AT_FAULT,          false));
    HASSERT(g_GpuDevice->d3d12_info_queue->SetBreakOnID(D3D12_MESSAGE_ID_DEVICE_REMOVAL_PROCESS_POSSIBLY_AT_FAULT, false));
    HASSERT(g_GpuDevice->d3d12_info_queue->SetBreakOnID(D3D12_MESSAGE_ID_DEVICE_REMOVAL_PROCESS_NOT_AT_FAULT,      false));

    // There's some sort of bug with the validation layers in ResolveQueryData. I'm not sure of a better way to get rid of it so I'm disabling it entirely
    D3D12_MESSAGE_ID denied_ids[] = { D3D12_MESSAGE_ID_CREATERESOURCE_STATE_IGNORED };

    D3D12_INFO_QUEUE_FILTER filter = {};
    filter.DenyList.NumIDs  = ARRAY_LENGTH(denied_ids);
    filter.DenyList.pIDList = denied_ids;

    g_GpuDevice->d3d12_info_queue->AddStorageFilterEntries(&filter);

  }
  else
  {
    // If there are debug layers enabled, then we can't enable full aftermath support...
    init_aftermath(g_GpuDevice);
  }
#else
  init_aftermath(g_GpuDevice);
#endif
}

void
wait_for_gpu_device_idle(GpuDevice* device)
{
  FenceValue value = cmd_queue_signal(&device->graphics_queue, &device->graphics_cmd_allocator.fence);
  block_gpu_fence(&device->graphics_cmd_allocator.fence, value);

  value = cmd_queue_signal(&device->compute_queue, &device->compute_cmd_allocator.fence);
  block_gpu_fence(&device->compute_cmd_allocator.fence, value);

  value = cmd_queue_signal(&device->copy_queue, &device->copy_cmd_allocator.fence);
  block_gpu_fence(&device->copy_cmd_allocator.fence, value);
}

void
destroy_gpu_device()
{
  destroy_cmd_list_allocator(&g_GpuDevice->graphics_cmd_allocator);
  destroy_cmd_list_allocator(&g_GpuDevice->compute_cmd_allocator);
  destroy_cmd_list_allocator(&g_GpuDevice->copy_cmd_allocator);

  destroy_cmd_queue(&g_GpuDevice->graphics_queue);
  destroy_cmd_queue(&g_GpuDevice->compute_queue);
  destroy_cmd_queue(&g_GpuDevice->copy_queue);

#ifdef DEBUG_LAYER
  // g_GpuDevice->d3d12_debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
#endif

  COM_RELEASE(g_GpuDevice->d3d12);
  zero_memory(g_GpuDevice, sizeof(GpuDevice));
}

bool
is_depth_format(GpuFormat format)
{
  return format == kGpuFormatD32Float      ||
        format == kGpuFormatD16Unorm       ||
        format == kGpuFormatD24UnormS8Uint ||
        format == kGpuFormatD32FloatS8x24Uint;
}

static GpuFormat
get_typeless_depth_format(GpuFormat format)
{
  switch (format)
  {
    case kGpuFormatD32Float: return kGpuFormatR32Typeless;
    case kGpuFormatD16Unorm: return kGpuFormatR16Typeless;
    default: UNREACHABLE; // TODO(Brandon): Implement
  }
}

static D3D12_BARRIER_LAYOUT
gpu_texture_layout_to_d3d12(GpuTextureLayout layout)
{
  switch (layout)
  {
    case kGpuTextureLayoutGeneral:         return D3D12_BARRIER_LAYOUT_COMMON;
    case kGpuTextureLayoutShaderResource:  return D3D12_BARRIER_LAYOUT_SHADER_RESOURCE;
    case kGpuTextureLayoutUnorderedAccess: return D3D12_BARRIER_LAYOUT_UNORDERED_ACCESS;
    case kGpuTextureLayoutRenderTarget:    return D3D12_BARRIER_LAYOUT_RENDER_TARGET;
    case kGpuTextureLayoutDepthStencil:    return D3D12_BARRIER_LAYOUT_DEPTH_STENCIL_WRITE;
    case kGpuTextureLayoutDiscard:         return D3D12_BARRIER_LAYOUT_UNDEFINED;
    default: UNREACHABLE;
  }
}

static D3D12_RESOURCE_DESC1
d3d12_resource_desc(const GpuTextureDesc& desc)
{
  ASSERT_MSG_FATAL(desc.mip_levels > 0, "Must have at least 1 mip (the base mip)");
  D3D12_RESOURCE_DESC1 resource_desc;
  resource_desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  resource_desc.Format             = gpu_format_to_d3d12(desc.format);
  resource_desc.Alignment          = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  resource_desc.Width              = desc.width;
  resource_desc.Height             = desc.height;
  resource_desc.DepthOrArraySize   = MAX(desc.array_size, 1);
  resource_desc.MipLevels          = desc.mip_levels;
  resource_desc.SampleDesc.Count   = 1;
  resource_desc.SampleDesc.Quality = 0;
  resource_desc.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  resource_desc.Flags              = desc.flags;

  resource_desc.SamplerFeedbackMipRegion.Width  = 0;
  resource_desc.SamplerFeedbackMipRegion.Height = 0;
  resource_desc.SamplerFeedbackMipRegion.Depth  = 0;

  return resource_desc;
}

void
free_gpu_texture(GpuTexture* texture)
{
  COM_RELEASE(texture->d3d12_texture);
  zero_memory(texture, sizeof(GpuTexture));
}


u64
query_gpu_texture_size(GpuTextureDesc desc, u64* out_alignment)
{
  D3D12_RESOURCE_DESC1 resource_desc  = d3d12_resource_desc(desc);

  D3D12_RESOURCE_ALLOCATION_INFO info = g_GpuDevice->d3d12->GetResourceAllocationInfo2(0, 1, &resource_desc, nullptr);

  if (out_alignment != nullptr)
  {
    *out_alignment = info.Alignment;
  }

  return info.SizeInBytes;
}

GpuTexture
init_gpu_texture(GpuAllocation allocation, GpuTextureDesc desc, const char* name)
{
  GpuTexture ret = {0};
  ret.desc       = desc;
  ret.layout     = kGpuTextureLayoutGeneral;

  u64 alignment = 1;
  u64 size      = query_gpu_texture_size(desc, &alignment);
  ASSERT_MSG_FATAL(size <= allocation.size, "GpuAllocation passed to init_gpu_texture is not large enough!");

  D3D12_RESOURCE_DESC1 resource_desc = d3d12_resource_desc(desc);

  D3D12_CLEAR_VALUE clear_value;
  D3D12_CLEAR_VALUE* p_clear_value   = nullptr;
  clear_value.Format                 = gpu_format_to_d3d12(desc.format);
  if (is_depth_format(desc.format))
  {
    resource_desc.Flags             |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    clear_value.DepthStencil.Depth   = desc.depth_clear_value;
    clear_value.DepthStencil.Stencil = desc.stencil_clear_value;
    p_clear_value                    = &clear_value;
  }
  else if (desc.flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
  {
    clear_value.Color[0]             = desc.color_clear_value.x;
    clear_value.Color[1]             = desc.color_clear_value.y;
    clear_value.Color[2]             = desc.color_clear_value.z;
    clear_value.Color[3]             = desc.color_clear_value.w;
    p_clear_value                    = &clear_value;
  }

  HASSERT(
    g_GpuDevice->d3d12->CreatePlacedResource2(
      allocation.d3d12_heap,
      allocation.offset,
      &resource_desc,
      gpu_texture_layout_to_d3d12(ret.layout),
      p_clear_value,
      0,
      nullptr,
      IID_PPV_ARGS(&ret.d3d12_texture)
    )
  );

  wchar_t wname[1024];
  mbstowcs_s(nullptr, wname, name, 1024);
  ret.d3d12_texture->SetName(wname);

  return ret;
}

GpuTexture
alloc_gpu_texture(
  GpuAllocHeap heap,
  GpuTextureDesc desc,
  const char* name
) {
  u64 alignment = 1;
  u64 size      = query_gpu_texture_size(desc, &alignment);
  GpuAllocation allocation = GPU_HEAP_ALLOC(heap, (u32)size, (u32)alignment);
  return init_gpu_texture(allocation, desc, name);
}

GpuBuffer
alloc_gpu_buffer_no_heap(
  const GpuDevice* device,
  GpuBufferDesc desc,
  GpuHeapLocation location,
  const char* name
) {
  GpuBuffer ret = {0};
  ret.desc = desc;

  D3D12_HEAP_PROPERTIES heap_props = CD3DX12_HEAP_PROPERTIES(get_d3d12_heap_location(location));
  D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
  if (location != kGpuHeapSysRAMCpuToGpu && location != kGpuHeapSysRAMGpuToCpu)
  {
    flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  }

  if (desc.is_rt_bvh)
  {
    flags |= D3D12_RESOURCE_FLAG_RAYTRACING_ACCELERATION_STRUCTURE;
  }
  auto resource_desc = CD3DX12_RESOURCE_DESC1::Buffer(desc.size, flags);

  HASSERT(
    device->d3d12->CreateCommittedResource3(
      &heap_props,
      D3D12_HEAP_FLAG_NONE,
      &resource_desc,
      D3D12_BARRIER_LAYOUT_UNDEFINED,
      nullptr,
      nullptr,
      0,
      nullptr,
      IID_PPV_ARGS(&ret.d3d12_buffer)
    )
  );

  wchar_t wname[1024];
  mbstowcs_s(nullptr, wname, name, 1024);
  ret.d3d12_buffer->SetName(wname);
  ret.gpu_addr = ret.d3d12_buffer->GetGPUVirtualAddress();
  if (location == kGpuHeapSysRAMCpuToGpu || location == kGpuHeapVRAMCpuToGpu || location == kGpuHeapSysRAMGpuToCpu)
  {
    void* mapped = nullptr;
    ret.d3d12_buffer->Map(0, nullptr, &mapped);
    ret.mapped = mapped;
    ASSERT_MSG_FATAL(mapped != nullptr, "Failed to map GPU buffer!");
  }

  return ret;
}

void
free_gpu_buffer(GpuBuffer* buffer)
{
  COM_RELEASE(buffer->d3d12_buffer);
  zero_memory(buffer, sizeof(GpuBuffer));
}

GpuBuffer init_gpu_buffer(
  GpuAllocation allocation,
  u32 size,
  const char* name,
  bool is_rt_bvh
) {
  GpuBuffer ret      = {0};
  ret.desc.size      = size;
  ret.desc.is_rt_bvh = is_rt_bvh;

  D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
  if (allocation.location != kGpuHeapSysRAMCpuToGpu && allocation.location != kGpuHeapSysRAMGpuToCpu)
  {
    flags |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  }

  if (ret.desc.is_rt_bvh)
  {
    flags |= D3D12_RESOURCE_FLAG_RAYTRACING_ACCELERATION_STRUCTURE;
  }

  ret.desc.flags     = flags;
  auto resource_desc = CD3DX12_RESOURCE_DESC1::Buffer(ret.desc.size, flags);

  HASSERT(
    g_GpuDevice->d3d12->CreatePlacedResource2(
      allocation.d3d12_heap,
      allocation.offset,
      &resource_desc,
      D3D12_BARRIER_LAYOUT_UNDEFINED,
      nullptr,
      0,
      nullptr,
      IID_PPV_ARGS(&ret.d3d12_buffer)
    )
  );

  wchar_t wname[1024];
  mbstowcs_s(nullptr, wname, name, 1024);
  ret.d3d12_buffer->SetName(wname);

  ret.gpu_addr = ret.d3d12_buffer->GetGPUVirtualAddress();
  if (allocation.location == kGpuHeapSysRAMCpuToGpu || allocation.location == kGpuHeapVRAMCpuToGpu || allocation.location == kGpuHeapSysRAMGpuToCpu)
  {
    void* mapped = nullptr;
    ret.d3d12_buffer->Map(0, nullptr, &mapped);
    ret.mapped = mapped;
    ASSERT_MSG_FATAL(mapped != nullptr, "Failed to map GPU buffer!");
  }

  return ret;
}

GpuBuffer
alloc_gpu_buffer(
  const GpuDevice* device,
  GpuAllocHeap heap,
  GpuBufferDesc desc,
  const char* name
) {
  // TODO(bshihabi): Just delete this
  UNREFERENCED_PARAMETER(device);
  // NOTE(Brandon): For simplicity's sake of CBVs, I just align all the sizes to 256.
  desc.size = ALIGN_POW2(desc.size, 256);

  GpuAllocation allocation = GPU_HEAP_ALLOC(heap, desc.size, (u32)KiB(64));
  return init_gpu_buffer(allocation, desc.size, name, desc.is_rt_bvh);
}

GpuRingBuffer
alloc_gpu_ring_buffer_no_heap(AllocHeap heap, GpuBufferDesc desc, GpuHeapLocation location, const char* name)
{
  static constexpr u32 kMinimumAllocationSize = 256;

  GpuRingBuffer ret = {};
  ret.buffer        = alloc_gpu_buffer_no_heap(g_GpuDevice, desc, location, name);
  ret.fence         = init_gpu_fence();
  ret.queued_fences = init_ring_queue<GpuRingBuffer::GpuAllocationFence>(heap, desc.size / kMinimumAllocationSize);
  ret.write         = 0;
  ret.read          = 0;
  ret.used          = 0;

  return ret;
}

GpuRingBuffer
alloc_gpu_ring_buffer(AllocHeap cpu_heap, GpuAllocHeap gpu_heap, u32 size, const char* name)
{
  static constexpr u32 kMinimumAllocationSize = 256;

  GpuRingBuffer ret = {};
  ret.buffer        = alloc_gpu_buffer(gpu_heap, size, name);
  ret.fence         = init_gpu_fence();
  ret.queued_fences = init_ring_queue<GpuRingBuffer::GpuAllocationFence>(cpu_heap, size / kMinimumAllocationSize);
  ret.write         = 0;
  ret.read          = 0;
  ret.used          = 0;

  return ret;
}

static void
gpu_ring_buffer_consume_finished(GpuRingBuffer* buffer)
{
  FenceValue current_fence_value = poll_gpu_fence_value(&buffer->fence);
  while (!ring_queue_is_empty(buffer->queued_fences))
  {
    GpuRingBuffer::GpuAllocationFence allocation_fence;
    ring_queue_peak_front(buffer->queued_fences, &allocation_fence);
    FenceValue wait_for_fence_value = allocation_fence.value;
    // If the value we're supposed to wait for hasn't been reached yet, nothing left to look for in the queue (since GpuFences increment monotonically)
    if (current_fence_value < wait_for_fence_value)
    {
      break;
    }

    ring_queue_pop(&buffer->queued_fences);
    buffer->read  = allocation_fence.offset;
    ASSERT_MSG_FATAL(allocation_fence.size <= buffer->used, "GpuRingBuffer::GpuAllocationFence size is bigger than the tracked usage in GpuRingBuffer. This is a bug in the GpuRingBuffer.");
    buffer->used -= allocation_fence.size;

    if (buffer->used == 0)
    {
      buffer->read = buffer->write = 0;
    }
  }
}

void
gpu_ring_buffer_wait(GpuRingBuffer* buffer, u32 size)
{
  u32 capacity = buffer->buffer.desc.size;
  ASSERT_MSG_FATAL(size <= capacity, "Attempting to allocate 0x%llx bytes from GPU ring buffer of capacity 0x%llx. You will wait for ever. Increase the size of this GPU ring buffer to fix.", size, capacity);
  while (true)
  {
    gpu_ring_buffer_consume_finished(buffer);
    if (buffer->used + size > capacity)
    {
      ASSERT_MSG_FATAL(!ring_queue_is_empty(buffer->queued_fences), "For some reason fence queue for ring buffer is empty, but the read/write tails say there is no room available...");

      // Block until the next even is completed and then consume more
      block_gpu_fence(&buffer->fence, buffer->fence.last_completed_value + 1);
      continue;
    }
    else
    {
      break;
    }
  }
}

// Either returns the offset or the fence value to wait for
Result<u64, FenceValue>
gpu_ring_buffer_alloc(GpuRingBuffer* buffer, u32 size, u32 alignment)
{
  gpu_ring_buffer_consume_finished(buffer);

  u32 capacity = buffer->buffer.desc.size;
  ASSERT_MSG_FATAL(size < capacity, "Attempted to allocate %u bytes from GPU ring buffer with capacity %u", size, capacity);
  if (buffer->write >= buffer->read)
  {
    u32 aligned_write = ALIGN_UP(buffer->write, alignment);
    u32 padding       = aligned_write - buffer->write;
    u32 padded_size   = size + padding;
    //                     read             write
    //                     |                |        |
    //  [                  xxxxxxxxxxxxxxxxx         ]

    if (buffer->write + padded_size <= capacity)
    {
      // There's enough room at the end!
      //                     read             write
      //                     |                |        |
      //  [                  xxxxxxxxxxxxxxxxx         ]
      u32 dst = buffer->write + padding;

      buffer->write += padded_size;
      buffer->used  += padded_size;

      GpuRingBuffer::GpuAllocationFence allocation_fence;
      allocation_fence.value  = inc_fence(&buffer->fence);
      allocation_fence.size   = padded_size;
      allocation_fence.offset = buffer->write;
      ring_queue_push(&buffer->queued_fences, allocation_fence);

      return Ok((u64)dst);
    }
    else if (size <= buffer->read)
    {
      // There's not enough room at the end, start from the beginning
      //   write             read
      //   |                 |                         |
      //  [                  xxxxxxxxxxxxxxxxx---------]
      u32 dst        = 0;
      u32 null_space = (capacity - buffer->write);
      buffer->used  += null_space + size;
      buffer->write  = size;

      GpuRingBuffer::GpuAllocationFence allocation_fence;
      allocation_fence.value  = inc_fence(&buffer->fence);
      allocation_fence.size   = size;
      allocation_fence.offset = buffer->write;
      ring_queue_push(&buffer->queued_fences, allocation_fence);

      return Ok((u64)dst);
    }

  }
  else if (buffer->write + size <= buffer->read)
  {
    //
    //       Tail          Head             
    //       |             |             
    //  [xxxx              xxxxxxxxxxxxxxxxxxxxxxxxxx]
    //
    u32 dst = buffer->write;

    buffer->write += size;
    buffer->used  += size;

    GpuRingBuffer::GpuAllocationFence allocation_fence;
    // Peak the next fence value, it's expected that the user is going to signal the queue with the next fence value using our fence
    allocation_fence.value  = inc_fence(&buffer->fence);
    allocation_fence.size   = size;
    allocation_fence.offset = buffer->write;
    ring_queue_push(&buffer->queued_fences, allocation_fence);

    return Ok((u64)dst);
  }

  ASSERT_MSG_FATAL(!ring_queue_is_empty(buffer->queued_fences), "For some reason fence queue for ring buffer is empty, but the read/write tails say there is no room available...");

  GpuRingBuffer::GpuAllocationFence allocation_fence;
  ring_queue_peak_front(buffer->queued_fences, &allocation_fence);

  return Err(allocation_fence.value);
}

void
gpu_ring_buffer_commit(const GpuRingBuffer* buffer, CmdListAllocator* cmd_buffer_allocator)
{
  HASSERT(cmd_buffer_allocator->d3d12_queue->Signal(buffer->fence.d3d12_fence, buffer->fence.value));
}

void
gpu_ring_buffer_commit(const GpuRingBuffer* buffer, CmdQueue* queue)
{
  HASSERT(queue->d3d12_queue->Signal(buffer->fence.d3d12_fence, buffer->fence.value));
}

void
free_gpu_ring_buffer(GpuRingBuffer* buffer)
{
  free_gpu_buffer(&buffer->buffer);
  destroy_gpu_fence(&buffer->fence);
}

static D3D12_DESCRIPTOR_HEAP_TYPE
get_d3d12_descriptor_type(DescriptorHeapType type)
{
  switch(type)
  {
    case kDescriptorHeapTypeCbvSrvUav: return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    case kDescriptorHeapTypeSampler:   return D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
    case kDescriptorHeapTypeRtv:       return D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    case kDescriptorHeapTypeDsv:       return D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    default: UNREACHABLE;
  }
}

static bool
descriptor_type_is_shader_visible(DescriptorHeapType type)
{
  return type == kDescriptorHeapTypeCbvSrvUav || type == kDescriptorHeapTypeSampler;
}

static ID3D12DescriptorHeap*
init_d3d12_descriptor_heap(const GpuDevice* device, u32 size, DescriptorHeapType type)
{
  D3D12_DESCRIPTOR_HEAP_DESC desc;
  desc.Type              = get_d3d12_descriptor_type(type);
  desc.NumDescriptors    = size;
  desc.NodeMask          = 1;
  desc.Flags             = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

  bool is_shader_visible = descriptor_type_is_shader_visible(type);

  if (is_shader_visible)
  {
    desc.Flags |= D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ASSERT(size <= 2048);
  }

  ID3D12DescriptorHeap* ret = nullptr;
  HASSERT(device->d3d12->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&ret)));
  return ret;
}

DescriptorPool
init_descriptor_pool(AllocHeap heap, u32 size, DescriptorHeapType type, u32 table_reserved)
{
  ASSERT_MSG_FATAL(table_reserved < size, "The number of reserved entries for the descriptor table is more than the capacity for the descriptor pool.");

  DescriptorPool ret = {0};
  ret.num_descriptors = size;
  ret.type = type;
  ret.free_descriptors = init_ring_queue<u32>(heap, size);
  ret.d3d12_heap = init_d3d12_descriptor_heap(g_GpuDevice, size, type);

  ret.descriptor_size = g_GpuDevice->d3d12->GetDescriptorHandleIncrementSize(get_d3d12_descriptor_type(type));
  ret.cpu_start = ret.d3d12_heap->GetCPUDescriptorHandleForHeapStart();
  if (descriptor_type_is_shader_visible(type))
  {
    ret.gpu_start = ret.d3d12_heap->GetGPUDescriptorHandleForHeapStart();
  }

  for (u32 i = table_reserved; i < size; i++)
  {
    ring_queue_push(&ret.free_descriptors, i);
  }

  return ret;
}

void
destroy_descriptor_pool(DescriptorPool* pool)
{
  COM_RELEASE(pool->d3d12_heap);
  zero_memory(pool, sizeof(DescriptorPool));
}

DescriptorLinearAllocator
init_descriptor_linear_allocator(
  const GpuDevice* device,
  u32 size,
  DescriptorHeapType type
) {
  ASSERT(size > 0);

  DescriptorLinearAllocator ret = {0};
  ret.pos = 0;
  ret.num_descriptors = size;
  ret.type = type;
  ret.d3d12_heap = init_d3d12_descriptor_heap(device, size, type);

  ret.descriptor_size = device->d3d12->GetDescriptorHandleIncrementSize(get_d3d12_descriptor_type(type));
  ret.cpu_start       = ret.d3d12_heap->GetCPUDescriptorHandleForHeapStart();
  if (descriptor_type_is_shader_visible(type))
  {
    ret.gpu_start = ret.d3d12_heap->GetGPUDescriptorHandleForHeapStart();
  }

  return ret;
}

void
reset_descriptor_linear_allocator(DescriptorLinearAllocator* allocator)
{
  allocator->pos = 0;
}

void
destroy_descriptor_linear_allocator(DescriptorLinearAllocator* allocator)
{
  COM_RELEASE(allocator->d3d12_heap);
  zero_memory(allocator, sizeof(DescriptorLinearAllocator));
}

GpuDescriptor
alloc_descriptor(DescriptorPool* pool)
{
  u32 index = 0;
  ring_queue_pop(&pool->free_descriptors, &index);
  u64 offset = index * pool->descriptor_size;

  GpuDescriptor ret  = {0};
  ret.cpu_handle.ptr = pool->cpu_start.ptr + offset;
  ret.gpu_handle     = None;
  ret.index          = index;
  ret.heap_type           = pool->type;

  if (pool->gpu_start)
  {
    ret.gpu_handle = D3D12_GPU_DESCRIPTOR_HANDLE{unwrap(pool->gpu_start).ptr + offset};
  }

  return ret;
}

GpuDescriptor
alloc_table_descriptor(DescriptorPool* pool, u32 idx)
{
  u64 offset = idx * pool->descriptor_size;

  GpuDescriptor ret  = {0};
  ret.cpu_handle.ptr = pool->cpu_start.ptr + offset;
  ret.gpu_handle     = None;
  ret.index          = idx;
  ret.heap_type           = pool->type;

  if (pool->gpu_start)
  {
    ret.gpu_handle = D3D12_GPU_DESCRIPTOR_HANDLE{unwrap(pool->gpu_start).ptr + offset};
  }

  return ret;
}

void
free_descriptor(DescriptorPool* pool, GpuDescriptor* descriptor)
{
  ASSERT(descriptor->cpu_handle.ptr >= pool->cpu_start.ptr);
  ASSERT(descriptor->index < pool->num_descriptors);
  ring_queue_push(&pool->free_descriptors, descriptor->index);
  zero_memory(descriptor, sizeof(GpuDescriptor));
}

GpuDescriptor
alloc_descriptor(DescriptorLinearAllocator* allocator)
{
  GpuDescriptor ret = {0};
  u32 index = allocator->pos++;
  u64 offset = index * allocator->descriptor_size;

  ret.cpu_handle.ptr = allocator->cpu_start.ptr + offset;
  ret.gpu_handle = None;
  ret.index = index;

  if (allocator->gpu_start)
  {
    ret.gpu_handle = D3D12_GPU_DESCRIPTOR_HANDLE{unwrap(allocator->gpu_start).ptr + offset};
  }

  ret.heap_type = allocator->type;
  ret.use_type  = kDescriptorTypeNull;

  return ret;
}


void
init_rtv(GpuDescriptor* descriptor, const GpuTexture* texture)
{
  ASSERT(descriptor->heap_type == kDescriptorHeapTypeRtv);

  g_GpuDevice->d3d12->CreateRenderTargetView(
    texture->d3d12_texture,
    nullptr,
    descriptor->cpu_handle
  );

  descriptor->use_type = kDescriptorTypeRtv;

}

void
init_dsv(GpuDescriptor* descriptor, const GpuTexture* texture)
{
  ASSERT(is_depth_format(texture->desc.format));
  ASSERT(descriptor->heap_type == kDescriptorHeapTypeDsv);

  D3D12_DEPTH_STENCIL_VIEW_DESC desc;
  desc.Texture2D.MipSlice = 0;
  desc.ViewDimension      = D3D12_DSV_DIMENSION_TEXTURE2D;
  desc.Flags              = D3D12_DSV_FLAG_NONE;
  desc.Format             = gpu_format_to_d3d12(texture->desc.format);
  g_GpuDevice->d3d12->CreateDepthStencilView(
    texture->d3d12_texture,
    &desc,
    descriptor->cpu_handle
  );

  descriptor->use_type = kDescriptorTypeDsv;
}

void
init_buffer_srv(
  GpuDescriptor* descriptor,
  const GpuBuffer* buffer,
  const GpuBufferSrvDesc& desc
) {
  ASSERT(descriptor->heap_type == kDescriptorHeapTypeCbvSrvUav);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {0};
  srv.Buffer.FirstElement        = desc.first_element;
  srv.Buffer.NumElements         = desc.num_elements;
  srv.Buffer.StructureByteStride = desc.stride;
  srv.Buffer.Flags               = desc.is_raw ? D3D12_BUFFER_SRV_FLAG_RAW : D3D12_BUFFER_SRV_FLAG_NONE;
  srv.Format                     = desc.is_raw ? DXGI_FORMAT_R32_TYPELESS : gpu_format_to_d3d12(desc.format);
  srv.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

  g_GpuDevice->d3d12->CreateShaderResourceView(buffer->d3d12_buffer, &srv, descriptor->cpu_handle);

  descriptor->use_type = kDescriptorTypeSrv;
}

static bool
buffer_is_aligned(const GpuBuffer* buffer, u64 alignment, u64 offset = 0)
{
  return (buffer->gpu_addr + offset) % alignment == 0;
}

void
init_buffer_cbv(
  GpuDescriptor* descriptor,
  const GpuBuffer* buffer,
  const GpuBufferCbvDesc& desc
) {
  ASSERT(descriptor->heap_type == kDescriptorHeapTypeCbvSrvUav);
  ASSERT(buffer_is_aligned(buffer, 256, desc.buffer_offset));
  ASSERT(desc.size <= U32_MAX);

  D3D12_CONSTANT_BUFFER_VIEW_DESC cbv = {0};
  cbv.BufferLocation = buffer->gpu_addr + desc.buffer_offset;
  cbv.SizeInBytes    = (u32)ALIGN_POW2(desc.size, 256);
  g_GpuDevice->d3d12->CreateConstantBufferView(&cbv, descriptor->cpu_handle);

  descriptor->use_type = kDescriptorTypeCbv;
}

void
init_buffer_uav(
  GpuDescriptor* descriptor,
  const GpuBuffer* buffer,
  const GpuBufferUavDesc& desc
) {
  ASSERT(descriptor->heap_type == kDescriptorHeapTypeCbvSrvUav);
  ASSERT_MSG_FATAL(desc.counter_offset == 0, "Counter is non zero! Did you mean to use init_buffer_counted_uav", D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT);

  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {0};
  uav.Buffer.FirstElement         = desc.first_element;
  uav.Buffer.NumElements          = desc.num_elements;
  uav.Buffer.StructureByteStride  = desc.stride;
  uav.Buffer.CounterOffsetInBytes = 0;
  uav.Buffer.Flags                = desc.is_raw ? D3D12_BUFFER_UAV_FLAG_RAW : D3D12_BUFFER_UAV_FLAG_NONE;
  uav.Format                      = desc.is_raw ? DXGI_FORMAT_R32_TYPELESS : gpu_format_to_d3d12(desc.format);
  uav.ViewDimension               = D3D12_UAV_DIMENSION_BUFFER;
  g_GpuDevice->d3d12->CreateUnorderedAccessView(buffer->d3d12_buffer, nullptr, &uav, descriptor->cpu_handle);

  descriptor->use_type = kDescriptorTypeUav;
}

void
init_buffer_counted_uav(
  GpuDescriptor*          descriptor,
  const GpuBuffer*        buffer,
  const GpuBuffer*        counter,
  const GpuBufferUavDesc& desc
) {
  ASSERT(descriptor->heap_type == kDescriptorHeapTypeCbvSrvUav);
  ASSERT_MSG_FATAL((desc.counter_offset % D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT) == 0, "Counter offset must be aligned to %u bytes", D3D12_UAV_COUNTER_PLACEMENT_ALIGNMENT);
  ASSERT_MSG_FATAL(desc.format == kGpuFormatUnknown, "Counter UAV buffers must have an unknown format!");
  ASSERT_MSG_FATAL(desc.stride > 0,                  "Counter UAVs must have a stride in the buffer!");
  ASSERT_MSG_FATAL(!desc.is_raw,                     "Counter UAVs cannot be created on raw resources!");

  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {0};
  uav.Buffer.FirstElement         = desc.first_element;
  uav.Buffer.NumElements          = desc.num_elements;
  uav.Buffer.StructureByteStride  = desc.stride;
  uav.Buffer.CounterOffsetInBytes = desc.counter_offset;
  uav.Buffer.Flags                = D3D12_BUFFER_UAV_FLAG_NONE;
  uav.Format                      = DXGI_FORMAT_UNKNOWN;
  uav.ViewDimension               = D3D12_UAV_DIMENSION_BUFFER;
  g_GpuDevice->d3d12->CreateUnorderedAccessView(buffer->d3d12_buffer, counter->d3d12_buffer, &uav, descriptor->cpu_handle);

  descriptor->use_type = kDescriptorTypeUav;
}

void
init_texture_srv(GpuDescriptor* descriptor, const GpuTexture* texture, GpuTextureSrvDesc desc)
{
  ASSERT(descriptor->heap_type == kDescriptorHeapTypeCbvSrvUav);
  ASSERT(texture->desc.array_size >= 1);

  // Reasonable default to just follow what the texture says
  if (desc.mip_levels == 0)
  {
    desc.mip_levels        = texture->desc.mip_levels;
    desc.most_detailed_mip = 0;
  }

  desc.most_detailed_mip = MIN(desc.most_detailed_mip, desc.mip_levels - 1);

  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
  srv.Texture2D.MipLevels = desc.mip_levels;
  if (is_depth_format(texture->desc.format))
  {
    srv.Format = (DXGI_FORMAT)((u32)desc.format + 1);
  }
  else
  {
    srv.Format = gpu_format_to_d3d12(desc.format);
  }
  srv.ViewDimension            = desc.array_size > 1 ? D3D12_SRV_DIMENSION_TEXTURE2DARRAY : D3D12_SRV_DIMENSION_TEXTURE2D;
  srv.Shader4ComponentMapping  = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srv.Texture2DArray.ArraySize = 1;
  if (desc.array_size > 1)
  {
    srv.Texture2DArray.MostDetailedMip = desc.most_detailed_mip;
    srv.Texture2DArray.MipLevels       = desc.mip_levels;
    srv.Texture2DArray.FirstArraySlice = 0;
    srv.Texture2DArray.ArraySize       = desc.array_size;
  }

  g_GpuDevice->d3d12->CreateShaderResourceView(texture->d3d12_texture, &srv, descriptor->cpu_handle);   

  descriptor->use_type = kDescriptorTypeSrv;
}

void
init_texture_uav(GpuDescriptor* descriptor, const GpuTexture* texture, const GpuTextureUavDesc& desc)
{
  ASSERT(descriptor->heap_type == kDescriptorHeapTypeCbvSrvUav);
  ASSERT_MSG_FATAL(desc.mip_slice < texture->desc.mip_levels, "Mip slice %u out of bounds for texture 0x%llx with %u mips!", desc.mip_slice, texture->d3d12_texture, texture->desc.mip_levels);

  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {0};
  uav.Format        = gpu_format_to_d3d12(desc.format);
  uav.ViewDimension = desc.array_size > 1 ? D3D12_UAV_DIMENSION_TEXTURE2DARRAY : D3D12_UAV_DIMENSION_TEXTURE2D;
  if (texture->desc.array_size > 1)
  {
    uav.Texture2DArray.MipSlice        = desc.mip_slice;
    uav.Texture2DArray.FirstArraySlice = 0;
    uav.Texture2DArray.ArraySize       = desc.array_size;
    uav.Texture2DArray.PlaneSlice      = 0;
  }
  else
  {
    uav.Texture2D.MipSlice             = desc.mip_slice;
    uav.Texture2D.PlaneSlice           = 0;
  }
  g_GpuDevice->d3d12->CreateUnorderedAccessView(texture->d3d12_texture, nullptr, &uav, descriptor->cpu_handle);

  descriptor->use_type = kDescriptorTypeUav;
}

void
init_bvh_srv(GpuDescriptor* descriptor, const GpuRtTlas* tlas)
{
  ASSERT(descriptor->heap_type == kDescriptorHeapTypeCbvSrvUav);

  D3D12_SHADER_RESOURCE_VIEW_DESC desc = {0};
  desc.ViewDimension                            = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
  desc.RaytracingAccelerationStructure.Location = tlas->buffer.gpu_addr;
  desc.Shader4ComponentMapping                  = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

  g_GpuDevice->d3d12->CreateShaderResourceView(nullptr, &desc, descriptor->cpu_handle);

  descriptor->use_type = kDescriptorTypeSrv;
}


GpuShader
load_shader_from_file(const GpuDevice* device, const wchar_t* path)
{
  GpuShader ret = {0};
  HASSERT(D3DReadFileToBlob(path, &ret.d3d12_shader));
  init_root_signature(device, ret.d3d12_shader);
  return ret;
}

GpuShader
load_shader_from_memory(const GpuDevice* device, const u8* src, size_t size)
{
  GpuShader ret = {0};

  HASSERT(D3DCreateBlob(size, &ret.d3d12_shader));
  ASSERT(ret.d3d12_shader->GetBufferSize() == size);

  memcpy(ret.d3d12_shader->GetBufferPointer(), src, size);

  init_root_signature(device, ret.d3d12_shader);

  register_aftermath_shader(src, size);

  return ret;
}

void
reload_shader_from_memory(GpuShader* shader, const u8* src, size_t size)
{
  destroy_shader(shader);
  GpuShader new_shader = load_shader_from_memory(g_GpuDevice, src, size);
  shader->d3d12_shader = new_shader.d3d12_shader;
  shader->generation++;
}

void
destroy_shader(GpuShader* shader)
{
  COM_RELEASE(shader->d3d12_shader);
}


static D3D12_PRIMITIVE_TOPOLOGY_TYPE
get_d3d12_primitive_topology(PrimitiveTopologyType type)
{
  switch(type)
  {
    case kPrimitiveTopologyUndefined: return D3D12_PRIMITIVE_TOPOLOGY_TYPE_UNDEFINED;
    case kPrimitiveTopologyPoint:     return D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
    case kPrimitiveTopologyLine:      return D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
    case kPrimitiveTopologyTriangle:  return D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    case kPrimitiveTopologyPatch:     return D3D12_PRIMITIVE_TOPOLOGY_TYPE_PATCH;
    default: UNREACHABLE;
  }
}

static D3D12_COMPARISON_FUNC
get_d3d12_compare_func(DepthFunc func)
{
  switch (func)
  {
    case kDepthFuncNone:         return D3D12_COMPARISON_FUNC_NONE;
    case kDepthFuncNever:        return D3D12_COMPARISON_FUNC_NEVER;
    case kDepthFuncLess:         return D3D12_COMPARISON_FUNC_LESS;
    case kDepthFuncEqual:        return D3D12_COMPARISON_FUNC_EQUAL;
    case kDepthFuncLessEqual:    return D3D12_COMPARISON_FUNC_LESS_EQUAL;
    case kDepthFuncGreater:      return D3D12_COMPARISON_FUNC_GREATER;
    case kDepthFuncNotEqual:     return D3D12_COMPARISON_FUNC_NOT_EQUAL;
    case kDepthFuncGreaterEqual: return D3D12_COMPARISON_FUNC_GREATER_EQUAL;
    case kDepthFuncAlways:       return D3D12_COMPARISON_FUNC_ALWAYS;
    default: UNREACHABLE;
  }
}


GraphicsPSO
init_graphics_pipeline(
  const GpuDevice* device,
  const GraphicsPipelineDesc& desc,
  const char* name
) {
  GraphicsPSO ret = {0};

  D3D12_RENDER_TARGET_BLEND_DESC render_target_blend_desc;
  render_target_blend_desc.BlendEnable = desc.blend_enable;
  render_target_blend_desc.LogicOpEnable = FALSE;
  render_target_blend_desc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
  render_target_blend_desc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
  render_target_blend_desc.BlendOp = D3D12_BLEND_OP_ADD;
  render_target_blend_desc.SrcBlendAlpha = D3D12_BLEND_ONE;
  render_target_blend_desc.DestBlendAlpha = D3D12_BLEND_ZERO;
  render_target_blend_desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
  render_target_blend_desc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

  D3D12_BLEND_DESC blend_desc;
  blend_desc.AlphaToCoverageEnable = FALSE;
  blend_desc.IndependentBlendEnable = FALSE;

  for(u32 i = 0; i < desc.rtv_formats.size; i++)
  {
    blend_desc.RenderTarget[i] = render_target_blend_desc;
  }

  D3D12_DEPTH_STENCIL_DESC depth_stencil_desc;
  depth_stencil_desc.DepthEnable = desc.dsv_format != DXGI_FORMAT_UNKNOWN;
  depth_stencil_desc.DepthWriteMask = desc.depth_read_only ? D3D12_DEPTH_WRITE_MASK_ZERO : D3D12_DEPTH_WRITE_MASK_ALL;
  depth_stencil_desc.DepthFunc = get_d3d12_compare_func(desc.depth_func);
  depth_stencil_desc.StencilEnable = desc.stencil_enable;
  depth_stencil_desc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
  depth_stencil_desc.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;


  D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {0};
  pso_desc.VS = CD3DX12_SHADER_BYTECODE(desc.vertex_shader->d3d12_shader);
  pso_desc.PS = CD3DX12_SHADER_BYTECODE(desc.pixel_shader->d3d12_shader);
  pso_desc.BlendState = blend_desc;
  pso_desc.SampleMask = UINT32_MAX;
  pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
  pso_desc.DepthStencilState = depth_stencil_desc;
  PrimitiveTopologyType topology = desc.topology == kPrimitiveTopologyUndefined ? kPrimitiveTopologyTriangle : desc.topology;
  pso_desc.PrimitiveTopologyType = get_d3d12_primitive_topology(topology);
  pso_desc.NumRenderTargets = static_cast<u32>(desc.rtv_formats.size);

  pso_desc.DSVFormat = gpu_format_to_d3d12(desc.dsv_format);

  pso_desc.SampleDesc.Count = 1;
  pso_desc.SampleDesc.Quality = 0;
  pso_desc.NodeMask = 0;

  pso_desc.RasterizerState.FrontCounterClockwise = false;
  pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;

  for (u32 i = 0; i < desc.rtv_formats.size; i++)
  {
    pso_desc.RTVFormats[i] = gpu_format_to_d3d12(desc.rtv_formats[i]);
  }

  HASSERT(device->d3d12->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&ret.d3d12_pso)));

  wchar_t wname[1024];
  mbstowcs_s(nullptr, wname, name, 1024);
  ret.d3d12_pso->SetName(wname);

  ret.name                     = name;
  ret.vertex_shader_generation = desc.vertex_shader->generation;
  ret.pixel_shader_generation  = desc.pixel_shader->generation;
  ret.desc                     = desc;

  return ret;
}
void
reload_graphics_pipeline(GraphicsPSO* pipeline)
{
  COM_RELEASE(pipeline->d3d12_pso);
  *pipeline = init_graphics_pipeline(g_GpuDevice, pipeline->desc, pipeline->name);
}

void
destroy_graphics_pipeline(GraphicsPSO* pipeline)
{
  COM_RELEASE(pipeline->d3d12_pso);
  zero_memory(pipeline, sizeof(GraphicsPSO));
}

ComputePSO
init_compute_pipeline(const GpuDevice* device, const GpuShader* compute_shader, const char* name)
{
  ComputePSO ret = {0};
  D3D12_COMPUTE_PIPELINE_STATE_DESC desc;
  desc.pRootSignature                  = g_RootSignature;
  desc.CS                              = CD3DX12_SHADER_BYTECODE(compute_shader->d3d12_shader);
  desc.NodeMask                        = 0;
  desc.Flags                           = D3D12_PIPELINE_STATE_FLAG_NONE;
  desc.CachedPSO.pCachedBlob           = nullptr;
  desc.CachedPSO.CachedBlobSizeInBytes = 0;
  HASSERT(device->d3d12->CreateComputePipelineState(&desc, IID_PPV_ARGS(&ret.d3d12_pso)));

  wchar_t wname[1024];
  mbstowcs_s(nullptr, wname, name, 1024);
  ret.d3d12_pso->SetName(wname);

  ret.name                             = name;
  ret.compute_shader                   = compute_shader;
  ret.compute_shader_generation        = compute_shader->generation;

  return ret;
}

void
reload_compute_pipeline(ComputePSO* pipeline)
{
  COM_RELEASE(pipeline->d3d12_pso);
  *pipeline = init_compute_pipeline(g_GpuDevice, pipeline->compute_shader, pipeline->name);
}

void
destroy_compute_pipeline(ComputePSO* pipeline)
{
  COM_RELEASE(pipeline->d3d12_pso);
  zero_memory(pipeline, sizeof(ComputePSO));
}

static D3D12_RAYTRACING_GEOMETRY_DESC
get_d3d12_blas_desc(
  const GpuRtBlasDesc& desc,

  const GpuBuffer& vertex_buffer,
  const GpuBuffer& index_buffer
) {
  GpuFormat index_format = desc.index_stride == sizeof(u16) ? kGpuFormatR16Uint : kGpuFormatR32Uint;

  D3D12_RAYTRACING_GEOMETRY_DESC ret = {};
  ret.Type                                 = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
  ret.Flags                                = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
  ret.Triangles.IndexBuffer                = index_buffer.gpu_addr + desc.index_start * desc.index_stride;
  ret.Triangles.IndexCount                 = desc.index_count;
  ret.Triangles.IndexFormat                = gpu_format_to_d3d12(index_format);
  ret.Triangles.Transform3x4               = 0;
  ret.Triangles.VertexFormat               = gpu_format_to_d3d12(desc.vertex_format);
  ret.Triangles.VertexCount                = desc.vertex_count;
  ret.Triangles.VertexBuffer.StartAddress  = vertex_buffer.gpu_addr + desc.vertex_start * desc.vertex_stride;
  ret.Triangles.VertexBuffer.StrideInBytes = desc.vertex_stride;

  return ret;
}

GpuRtBlas
alloc_gpu_rt_blas(
  GpuAllocHeap heap,
  const GpuBuffer& vertex_buffer,
  const GpuBuffer& index_buffer,

  GpuRtBlasDesc    desc,

  const char*      name
) {
  D3D12_RAYTRACING_GEOMETRY_DESC geometry_desc       = get_d3d12_blas_desc(desc, vertex_buffer, index_buffer);

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
  inputs.DescsLayout                                 = D3D12_ELEMENTS_LAYOUT_ARRAY;
  if (desc.allow_updates)
  {
    // I by default enable prefer fast build if you allow updates because they go hand in hand
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
  }
  else
  {
    inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
  }

  if (desc.minimize_memory)
  {
    inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_MINIMIZE_MEMORY;
  }

  inputs.NumDescs                                    = 1;
  inputs.Type                                        = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
  inputs.pGeometryDescs                              = &geometry_desc;

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild_info;
  g_GpuDevice->d3d12->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuild_info);
  prebuild_info.ScratchDataSizeInBytes               = ALIGN_POW2(prebuild_info.ScratchDataSizeInBytes,   D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
  prebuild_info.ResultDataMaxSizeInBytes             = ALIGN_POW2(prebuild_info.ResultDataMaxSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);

  GpuBufferDesc buffer_desc = {};
  buffer_desc.size          = (u32)prebuild_info.ResultDataMaxSizeInBytes;
  buffer_desc.is_rt_bvh     = true;

  GpuRtBlas ret;
  ret.desc         = desc;
  ret.scratch_size = (u32)prebuild_info.ScratchDataSizeInBytes;
  ret.buffer       = alloc_gpu_buffer(g_GpuDevice, heap, buffer_desc, name);
  return ret;
}

GpuRtTlasSizeInfo
query_gpu_rt_tlas_size_info(u32 num_instances)
{
  GpuRtTlasSizeInfo ret;

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
  inputs.DescsLayout    = D3D12_ELEMENTS_LAYOUT_ARRAY;
  // I expect that the TLAS wil be updated frequently, so I always have update/fast build enabled.
  inputs.Flags          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;
  inputs.NumDescs       = num_instances;
  inputs.Type           = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
  inputs.pGeometryDescs = nullptr;
  // Basically just need some non-null GPU virtual address
  inputs.InstanceDescs  = 0xC1C1C1C1;

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuild_info;
  g_GpuDevice->d3d12->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuild_info);
  prebuild_info.ScratchDataSizeInBytes   = ALIGN_POW2(prebuild_info.ScratchDataSizeInBytes,   D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
  prebuild_info.ResultDataMaxSizeInBytes = ALIGN_POW2(prebuild_info.ResultDataMaxSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
  ASSERT_MSG_FATAL(prebuild_info.ResultDataMaxSizeInBytes > 0, "Size of TLAS is 0 for some reason...");

  ret.max_size     = (u32)prebuild_info.ResultDataMaxSizeInBytes;
  ret.scratch_size = (u32)prebuild_info.ScratchDataSizeInBytes;

  return ret;
}

GpuRtTlas
alloc_gpu_rt_tlas(
  GpuAllocHeap     heap,
  u32              num_instances,
  const char*      name
) {
  GpuRtTlasSizeInfo size_info = query_gpu_rt_tlas_size_info(num_instances);

  GpuBufferDesc buffer_desc = {};
  buffer_desc.size          = size_info.max_size;
  buffer_desc.flags         = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  buffer_desc.is_rt_bvh     = true;
  
  GpuRtTlas ret;
  ret.max_instances = num_instances;
  ret.scratch_size  = size_info.scratch_size;
  ret.buffer        = alloc_gpu_buffer(g_GpuDevice, heap, buffer_desc, name);
  return ret;
}

GpuRtTlas
alloc_gpu_rt_tlas_no_heap(
  u32              num_instances,
  const char*      name
) {
  GpuRtTlasSizeInfo size_info = query_gpu_rt_tlas_size_info(num_instances);

  GpuBufferDesc buffer_desc = {};
  buffer_desc.size          = size_info.max_size;
  buffer_desc.flags         = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  buffer_desc.is_rt_bvh     = true;
  
  GpuRtTlas ret;
  ret.max_instances = num_instances;
  ret.scratch_size  = size_info.scratch_size;
  ret.buffer        = alloc_gpu_buffer_no_heap(g_GpuDevice, buffer_desc, kGpuHeapGpuOnly, name);
  return ret;
}

RayTracingPSO
init_ray_tracing_pipeline(const GpuDevice* device, const GpuShader* ray_tracing_library, const char* name)
{
  RayTracingPSO ret = {0};

  CD3DX12_STATE_OBJECT_DESC desc = {D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE};

  auto* library = desc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
  auto shader_byte_code = CD3DX12_SHADER_BYTECODE(ray_tracing_library->d3d12_shader);
  library->SetDXILLibrary(&shader_byte_code);
  HASSERT(device->d3d12->CreateStateObject(desc, IID_PPV_ARGS(&ret.d3d12_pso)));

  HASSERT(ret.d3d12_pso->QueryInterface(IID_PPV_ARGS(&ret.d3d12_properties)));

  wchar_t wname[1024];
  mbstowcs_s(nullptr, wname, name, 1024);
  ret.d3d12_pso->SetName(wname);
  ret.name                           = name;
  ret.ray_tracing_library            = ray_tracing_library;
  ret.ray_tracing_library_generation = ray_tracing_library->generation;

  return ret;
}

void
destroy_ray_tracing_pipeline(RayTracingPSO* pipeline)
{
  COM_RELEASE(pipeline->d3d12_pso);
  zero_memory(pipeline, sizeof(RayTracingPSO));
}

ShaderTable
init_shader_table(const GpuDevice* device, RayTracingPSO pipeline, const char* name)
{
  ShaderTable ret = {0};

  const u32 shader_id_size = D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT;
  ret.record_size          = ALIGN_POW2(shader_id_size + 8, D3D12_RAYTRACING_SHADER_RECORD_BYTE_ALIGNMENT);

  // Shader Table:
  // 0: RayGen Shader
  // 1: Miss   Shader
  // 2: Hit    Shader
  u32 buffer_size = ret.record_size * 3;
  buffer_size     = ALIGN_POW2(buffer_size, D3D12_RAYTRACING_SHADER_TABLE_BYTE_ALIGNMENT);

  GpuBufferDesc desc = {0};
  desc.size = buffer_size;
  ret.buffer = alloc_gpu_buffer_no_heap(device, desc, kGpuHeapSysRAMCpuToGpu, name);

  u8* dst = (u8*)unwrap(ret.buffer.mapped);
  // TODO(Brandon): Don't hard-code these names
  memcpy(dst, pipeline.d3d12_properties->GetShaderIdentifier(L"ray_gen"), shader_id_size);
  dst += ret.record_size;
  memcpy(dst, pipeline.d3d12_properties->GetShaderIdentifier(L"miss"), shader_id_size);
  dst += ret.record_size;
  memcpy(dst, pipeline.d3d12_properties->GetShaderIdentifier(L"kHitGroup"), shader_id_size);

  ret.ray_gen_addr = ret.buffer.gpu_addr + 0;
  ret.miss_addr    = ret.ray_gen_addr    + ret.record_size;
  ret.hit_addr     = ret.miss_addr       + ret.record_size;

  ret.ray_gen_size = ret.record_size;
  ret.miss_size    = ret.record_size;
  ret.hit_size     = ret.record_size;

  return ret;
}

void
destroy_shader_table(ShaderTable* shader_table)
{
  free_gpu_buffer(&shader_table->buffer);
  zero_memory(shader_table, sizeof(ShaderTable));
}

static void
alloc_back_buffers_from_swap_chain(
  const SwapChain* swap_chain,
  GpuTexture** back_buffers,
  u32 num_back_buffers
) {
  GpuTextureDesc desc = {0};
  desc.width  = swap_chain->width;
  desc.height = swap_chain->height;
  desc.format = swap_chain->format;
  for (u32 i = 0; i < num_back_buffers; i++)
  {
    HASSERT(swap_chain->d3d12_swap_chain->GetBuffer(i, IID_PPV_ARGS(&back_buffers[i]->d3d12_texture)));
    back_buffers[i]->desc   = desc;
    back_buffers[i]->layout = kGpuTextureLayoutGeneral;
    back_buffers[i]->d3d12_texture->SetName(L"Back Buffer");
  }
}

static void
set_hdr_metadata(IDXGISwapChain4* swap_chain1, f32 min_output_nits, f32 max_output_nits, ColorSpaceName color_space)
{
  DXGI_HDR_METADATA_HDR10 metadata;
  metadata.RedPrimary  [0]           = (u16)kColorSpaces[color_space].red_x;
  metadata.RedPrimary  [1]           = (u16)kColorSpaces[color_space].red_y;
  metadata.GreenPrimary[0]           = (u16)kColorSpaces[color_space].green_x;
  metadata.GreenPrimary[1]           = (u16)kColorSpaces[color_space].green_y;
  metadata.BluePrimary [0]           = (u16)kColorSpaces[color_space].blue_x;
  metadata.BluePrimary [1]           = (u16)kColorSpaces[color_space].blue_y;
  metadata.WhitePoint  [0]           = (u16)kColorSpaces[color_space].white_x;
  metadata.WhitePoint  [1]           = (u16)kColorSpaces[color_space].white_y;
  metadata.MinMasteringLuminance     = (u32)(min_output_nits * 10000.0f);
  metadata.MaxMasteringLuminance     = (u32)(max_output_nits * 10000.0f);
  metadata.MaxContentLightLevel      = (u16)(2000.0f);
  metadata.MaxFrameAverageLightLevel = (u16)(500.0f);
  HASSERT(swap_chain1->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(metadata), &metadata));
}

SwapChain
init_swap_chain(HWND window, const GpuDevice* device)
{
  auto* factory = init_factory();
  defer { COM_RELEASE(factory); };

  SwapChain ret = {0};

  RECT client_rect;
  GetClientRect(window, &client_rect);
  ret.width             = client_rect.right - client_rect.left;
  ret.height            = client_rect.bottom - client_rect.top;
  ret.format            = kGpuFormatRGB10A2Unorm;
  ret.tearing_supported = check_tearing_support(factory);
  ret.vsync             = true;
  ret.flags             = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;


  DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = { 0 };
  swap_chain_desc.Width       = ret.width;
  swap_chain_desc.Height      = ret.height;
  swap_chain_desc.Format      = gpu_format_to_d3d12(ret.format);
  swap_chain_desc.Stereo      = FALSE;
  swap_chain_desc.SampleDesc  = { 1, 0 };
  swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swap_chain_desc.BufferCount = ARRAY_LENGTH(ret.back_buffers);
  swap_chain_desc.Scaling     = DXGI_SCALING_NONE;
  swap_chain_desc.SwapEffect  = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
  swap_chain_desc.AlphaMode   = DXGI_ALPHA_MODE_UNSPECIFIED;
  swap_chain_desc.Flags       = ret.flags;

  IDXGISwapChain1* swap_chain1 = nullptr;
  HASSERT(
    factory->CreateSwapChainForHwnd(
      device->graphics_queue.d3d12_queue,
      window,
      &swap_chain_desc,
      nullptr,
      nullptr,
      &swap_chain1
    )
  );

  HASSERT(factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER));
  HASSERT(swap_chain1->QueryInterface(IID_PPV_ARGS(&ret.d3d12_swap_chain)));
  COM_RELEASE(swap_chain1);

  set_hdr_metadata(ret.d3d12_swap_chain, 0.001f, 1000.0f, kRec2020);
  ret.d3d12_swap_chain->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);
  ret.d3d12_swap_chain->SetMaximumFrameLatency(kFramesInFlight);
  ret.d3d12_latency_waitable = ret.d3d12_swap_chain->GetFrameLatencyWaitableObject();

  ret.fence = init_gpu_fence();
  zero_memory(ret.frame_fence_values, sizeof(ret.frame_fence_values));

  for (u32 i = 0; i < ARRAY_LENGTH(ret.back_buffers); i++)
  {
    ret.back_buffers[i] = HEAP_ALLOC(GpuTexture, g_InitHeap, 1);
  }

  alloc_back_buffers_from_swap_chain(
    &ret,
    ret.back_buffers,
    ARRAY_LENGTH(ret.back_buffers)
  );

  ret.back_buffer_index = 0;

  return ret;
}

void
destroy_swap_chain(SwapChain* swap_chain)
{
  for (GpuTexture* texture : swap_chain->back_buffers)
  {
    free_gpu_texture(texture);
  }
  destroy_gpu_fence(&swap_chain->fence);
  COM_RELEASE(swap_chain->d3d12_swap_chain);
}


GpuTexture*
swap_chain_acquire(SwapChain* swap_chain)
{
  u32 index = swap_chain->back_buffer_index;
  block_gpu_fence(&swap_chain->fence, swap_chain->frame_fence_values[index]);

  return swap_chain->back_buffers[index];
}

void
swap_chain_wait_latency(SwapChain* swap_chain)
{
  WaitForSingleObjectEx(swap_chain->d3d12_latency_waitable, 1000, true);

  static DXGI_FRAME_STATISTICS prev_stats = {};
  DXGI_FRAME_STATISTICS stats = {};
  swap_chain->d3d12_swap_chain->GetFrameStatistics(&stats);

  u32 refresh_diff = stats.SyncRefreshCount - prev_stats.SyncRefreshCount;
  u32 present_diff = stats.PresentCount     - prev_stats.PresentCount;

  swap_chain->missed_vsync = present_diff < refresh_diff;

  prev_stats = stats;
}

static const char* kD3D12DredBreadcrumbOpToString[] =
{
  "D3D12_AUTO_BREADCRUMB_OP_SETMARKER",
  "D3D12_AUTO_BREADCRUMB_OP_BEGINEVENT",
  "D3D12_AUTO_BREADCRUMB_OP_ENDEVENT",
  "D3D12_AUTO_BREADCRUMB_OP_DRAWINSTANCED",
  "D3D12_AUTO_BREADCRUMB_OP_DRAWINDEXEDINSTANCED",
  "D3D12_AUTO_BREADCRUMB_OP_EXECUTEINDIRECT",
  "D3D12_AUTO_BREADCRUMB_OP_DISPATCH",
  "D3D12_AUTO_BREADCRUMB_OP_COPYBUFFERREGION",
  "D3D12_AUTO_BREADCRUMB_OP_COPYTEXTUREREGION",
  "D3D12_AUTO_BREADCRUMB_OP_COPYRESOURCE",
  "D3D12_AUTO_BREADCRUMB_OP_COPYTILES",
  "D3D12_AUTO_BREADCRUMB_OP_RESOLVESUBRESOURCE",
  "D3D12_AUTO_BREADCRUMB_OP_CLEARRENDERTARGETVIEW",
  "D3D12_AUTO_BREADCRUMB_OP_CLEARUNORDEREDACCESSVIEW",
  "D3D12_AUTO_BREADCRUMB_OP_CLEARDEPTHSTENCILVIEW",
  "D3D12_AUTO_BREADCRUMB_OP_RESOURCEBARRIER",
  "D3D12_AUTO_BREADCRUMB_OP_EXECUTEBUNDLE",
  "D3D12_AUTO_BREADCRUMB_OP_PRESENT",
  "D3D12_AUTO_BREADCRUMB_OP_RESOLVEQUERYDATA",
  "D3D12_AUTO_BREADCRUMB_OP_BEGINSUBMISSION",
  "D3D12_AUTO_BREADCRUMB_OP_ENDSUBMISSION",
  "D3D12_AUTO_BREADCRUMB_OP_DECODEFRAME",
  "D3D12_AUTO_BREADCRUMB_OP_PROCESSFRAMES",
  "D3D12_AUTO_BREADCRUMB_OP_ATOMICCOPYBUFFERUINT",
  "D3D12_AUTO_BREADCRUMB_OP_ATOMICCOPYBUFFERUINT64",
  "D3D12_AUTO_BREADCRUMB_OP_RESOLVESUBRESOURCEREGION",
  "D3D12_AUTO_BREADCRUMB_OP_WRITEBUFFERIMMEDIATE",
  "D3D12_AUTO_BREADCRUMB_OP_DECODEFRAME1",
  "D3D12_AUTO_BREADCRUMB_OP_SETPROTECTEDRESOURCESESSION",
  "D3D12_AUTO_BREADCRUMB_OP_DECODEFRAME2",
  "D3D12_AUTO_BREADCRUMB_OP_PROCESSFRAMES1",
  "D3D12_AUTO_BREADCRUMB_OP_BUILDRAYTRACINGACCELERATIONSTRUCTURE",
  "D3D12_AUTO_BREADCRUMB_OP_EMITRAYTRACINGACCELERATIONSTRUCTUREPOSTBUILDINFO",
  "D3D12_AUTO_BREADCRUMB_OP_COPYRAYTRACINGACCELERATIONSTRUCTURE",
  "D3D12_AUTO_BREADCRUMB_OP_DISPATCHRAYS",
  "D3D12_AUTO_BREADCRUMB_OP_INITIALIZEMETACOMMAND",
  "D3D12_AUTO_BREADCRUMB_OP_EXECUTEMETACOMMAND",
  "D3D12_AUTO_BREADCRUMB_OP_ESTIMATEMOTION",
  "D3D12_AUTO_BREADCRUMB_OP_RESOLVEMOTIONVECTORHEAP",
  "D3D12_AUTO_BREADCRUMB_OP_SETPIPELINESTATE1",
  "D3D12_AUTO_BREADCRUMB_OP_INITIALIZEEXTENSIONCOMMAND",
  "D3D12_AUTO_BREADCRUMB_OP_EXECUTEEXTENSIONCOMMAND",
  "D3D12_AUTO_BREADCRUMB_OP_DISPATCHMESH",
  "D3D12_AUTO_BREADCRUMB_OP_ENCODEFRAME",
  "D3D12_AUTO_BREADCRUMB_OP_RESOLVEENCODEROUTPUTMETADATA",
  "D3D12_AUTO_BREADCRUMB_OP_BARRIER",
  "D3D12_AUTO_BREADCRUMB_OP_BEGIN_COMMAND_LIST",
  "D3D12_AUTO_BREADCRUMB_OP_DISPATCHGRAPH",
  "D3D12_AUTO_BREADCRUMB_OP_SETPROGRAM"
};

void
swap_chain_submit(SwapChain* swap_chain, const GpuDevice* device, const GpuTexture* rtv)
{
  u32 index = swap_chain->back_buffer_index;
  ASSERT(swap_chain->back_buffers[index] == rtv);

  FenceValue value = cmd_queue_signal(&device->graphics_queue, &swap_chain->fence);
  swap_chain->frame_fence_values[index] = value;

  u32 sync_interval = swap_chain->vsync ? 1 : 0;
  u32 present_flags = swap_chain->tearing_supported && !swap_chain->vsync ? DXGI_PRESENT_ALLOW_TEARING : 0;
  HRESULT res = swap_chain->d3d12_swap_chain->Present(sync_interval, present_flags);
  if (res == DXGI_ERROR_DEVICE_REMOVED || res == DXGI_ERROR_DEVICE_RESET )
  {
    if (g_GpuDevice->flags & kGpuFlagsEnableRtValidation)
    {
      ID3D12Device5* device5 = nullptr;
      defer { COM_RELEASE(device5); };
      g_GpuDevice->d3d12->QueryInterface(IID_PPV_ARGS(&device5));
      NvAPI_D3D12_FlushRaytracingValidationMessages(device5);
    }

    GFSDK_Aftermath_CrashDump_Status status;
    GFSDK_Aftermath_GetCrashDumpStatus(&status);
    dbgln("GPU Device removed!! Waiting for Aftermath to generate crash dump...");
    // Wait for timeout
    while (status != GFSDK_Aftermath_CrashDump_Status_Finished)
    {
      _mm_pause();
      _mm_pause();
      _mm_pause();
      _mm_pause();
    }


    dbgln("================ GPU Crash Dump: %s ================", g_AftermathManager->crash_dump_path);
    if (device->flags & kGpuFlagsEnableValidationLayers)
    {
      dbgln("  NOTE: Aftermath crash information information is limited due to incompatibility of NVIDIA Aftermath and D3D12 validation layers.\n  Please run without -d3ddebug commandline args to get more information. Using DRED information instead.");
      ID3D12DeviceRemovedExtendedData2* dred = nullptr;
      HASSERT(device->d3d12->QueryInterface(IID_PPV_ARGS(&dred)));

      D3D12_DRED_DEVICE_STATE state = dred->GetDeviceState();
      switch (state)
      {
        case D3D12_DRED_DEVICE_STATE_HUNG:      dbgln("  GPU Device State: Hung");       break;
        case D3D12_DRED_DEVICE_STATE_FAULT:     dbgln("  GPU Device State: Fault");      break;
        case D3D12_DRED_DEVICE_STATE_PAGEFAULT: dbgln("  GPU Device State: Page Fault"); break;
        case D3D12_DRED_DEVICE_STATE_UNKNOWN:   dbgln("  GPU Device State: Unknown");    break;
      }

      D3D12_DRED_AUTO_BREADCRUMBS_OUTPUT1 breadcrumbs;
      D3D12_DRED_PAGE_FAULT_OUTPUT1       page_fault;
      HASSERT(dred->GetAutoBreadcrumbsOutput1(&breadcrumbs));
      HASSERT(dred->GetPageFaultAllocationOutput1(&page_fault));

      dbgln("  GPU Page Fault at 0x%016llx", page_fault.PageFaultVA);
      dbgln("    Allocated Resources:");

      
      for (const D3D12_DRED_ALLOCATION_NODE1* node = page_fault.pHeadExistingAllocationNode; node != nullptr; node = node->pNext)
      {
        dbgln("      %s", node->ObjectNameA);
      }

      dbgln("    Recently Freed Resources:");
      for (const D3D12_DRED_ALLOCATION_NODE1* node = page_fault.pHeadRecentFreedAllocationNode; node != nullptr; node = node->pNext)
      {
        dbgln("      %s", node->ObjectNameA);
      }

      for (const D3D12_AUTO_BREADCRUMB_NODE1* node = breadcrumbs.pHeadAutoBreadcrumbNode; node != nullptr; node = node->pNext)
      {
        dbgln("  CmdList breadcrumbs: ");
        for (u32 ibreadcrumb = 0; ibreadcrumb < node->BreadcrumbCount; ibreadcrumb++)
        {
          if (*node->pLastBreadcrumbValue == 0)
          {
            continue;
          }

          D3D12_AUTO_BREADCRUMB_OP op = node->pCommandHistory[ibreadcrumb];
          if (ibreadcrumb == *node->pLastBreadcrumbValue)
          {
            dbgln("   -> %s", kD3D12DredBreadcrumbOpToString[op]);
          }
          else
          {
            dbgln("      %s", kD3D12DredBreadcrumbOpToString[op]);
          }
        }
      }
    }
    else
    {
      GFSDK_Aftermath_PageFaultInformation page_fault_info{0};
      GFSDK_Aftermath_GetPageFaultInformation(&page_fault_info);
      if (page_fault_info.bHasPageFaultOccured)
      {
        dbgln("  GPU Page Fault at 0x%016llx", page_fault_info.faultingGpuVA);
        dbgln("  Resource Size: %llu", page_fault_info.resourceDesc.size);
        dbgln("  Resource Was Destroyed: %u", page_fault_info.resourceDesc.bWasDestroyed);
      }

#if 0
      if (g_AftermathManager->crash_dump != nullptr && g_AftermathManager->crash_dump_size > 0)
      {
        GFSDK_Aftermath_GpuCrashDump_Decoder* decoder = nullptr;
        GFSDK_Aftermath_Result res = GFSDK_Aftermath_GpuCrashDump_CreateDecoder(GFSDK_Aftermath_Version_API, g_AftermathManager->crash_dump, g_AftermathManager->crash_dump_size, &decoder);
        if (GFSDK_Aftermath_SUCCEED(res))
        {
          u32 active_shader_count = 0;
          res = GFSDK_Aftermath_GpuCrashDump_GetActiveShadersInfoCount(decoder, &active_shader_count);
          if (GFSDK_Aftermath_SUCCEED(res))
          {
            static constexpr u32 kMaxActiveShaders = 1024;
            GFSDK_Aftermath_GpuCrashDump_ShaderInfo active_shaders[];
            GFSDK_Aftermath_GpuCrashDump_GetActiveShadersInfo(dcoder, );
          }
          else
          {
            dbgln("Failed to get active shaders info count! %u");
          }
        }
        else
        {
          dbgln("AFTERMATH: Failed to create decoder for crash dump! %u", res);
        }
      }
#endif
    }

    if (g_AftermathManager && g_AftermathManager->crash_dump_path[0] != '\0')
    {
      char msg[kMaxPathLength + 128];
      snprintf(msg, sizeof(msg), "GPU crash dump saved to:\n%s\n\nWould you like to open it?", g_AftermathManager->crash_dump_path);
      if (MessageBoxA(nullptr, msg, "GPU Crash", MB_YESNO | MB_ICONERROR) == IDYES)
      {
        SHELLEXECUTEINFOA info = {};
        info.cbSize            = sizeof(info);
        info.fMask             = SEE_MASK_NOASYNC;
        info.lpVerb            = "open";
        info.lpFile            = g_AftermathManager->crash_dump_path;
        info.nShow             = SW_SHOW;
        ShellExecuteExA(&info);

        char wait_msg[kMaxPathLength + 128];
        snprintf(wait_msg, sizeof(wait_msg), "Opening:\n%s\n\nClick OK once Nsight Graphics has opened.", g_AftermathManager->crash_dump_path);
        MessageBoxA(nullptr, wait_msg, "GPU Crash", MB_OK | MB_ICONINFORMATION);
      }
    }

    ASSERT_MSG_FATAL(false, "GPU Crash Occurred!");
    ExitProcess(1);
  }
  else
  {
    HASSERT(res);
  }


  swap_chain->back_buffer_index = swap_chain->d3d12_swap_chain->GetCurrentBackBufferIndex();
}

void
swap_chain_resize(SwapChain* swap_chain, HWND window, GpuDevice* device)
{
  wait_for_gpu_device_idle(device);

  RECT client_rect;
  GetClientRect(window, &client_rect);

  swap_chain->width  = client_rect.right - client_rect.left;
  swap_chain->height = client_rect.bottom - client_rect.top;

  swap_chain->back_buffer_index = 0;

  for (GpuTexture* texture : swap_chain->back_buffers)
  {
    free_gpu_texture(texture);
  }

  HASSERT(
    swap_chain->d3d12_swap_chain->ResizeBuffers(
      ARRAY_LENGTH(swap_chain->back_buffers),
      swap_chain->width,
      swap_chain->height,
      gpu_format_to_d3d12(swap_chain->format),
      swap_chain->flags
    )
  );

  alloc_back_buffers_from_swap_chain(
    swap_chain,
    swap_chain->back_buffers,
    ARRAY_LENGTH(swap_chain->back_buffers)
  );
}

void
set_descriptor_heaps(CmdList* cmd, const DescriptorPool* heaps, u32 num_heaps)
{
  ScratchAllocator scratch_arena = alloc_scratch_arena();
  defer { free_scratch_arena(&scratch_arena); };
  auto d3d12_heaps = init_array<ID3D12DescriptorHeap*>(scratch_arena, num_heaps);
  for (u32 i = 0; i < num_heaps; i++)
  {
    *array_add(&d3d12_heaps) = heaps[i].d3d12_heap;
  }

  cmd->d3d12_list->SetDescriptorHeaps(num_heaps, &d3d12_heaps[0]);
}

void
set_descriptor_heaps(CmdList* cmd, Span<const DescriptorPool*> heaps)
{
  ScratchAllocator scratch_arena = alloc_scratch_arena();
  defer { free_scratch_arena(&scratch_arena); };
  auto d3d12_heaps = init_array<ID3D12DescriptorHeap*>(scratch_arena, heaps.size);
  for (u32 i = 0; i < heaps.size; i++)
  {
    *array_add(&d3d12_heaps) = heaps[i]->d3d12_heap;
  }

  cmd->d3d12_list->SetDescriptorHeaps(static_cast<u32>(heaps.size), &d3d12_heaps[0]);
}

void
set_descriptor_table(CmdList* cmd, const DescriptorPool* heap, u32 start_idx, u32 bind_slot)
{
  auto gpu_handle = unwrap(heap->gpu_start);
  gpu_handle.ptr += heap->descriptor_size * start_idx;
  cmd->d3d12_list->SetGraphicsRootDescriptorTable(bind_slot, gpu_handle);
  cmd->d3d12_list->SetComputeRootDescriptorTable(bind_slot, gpu_handle);
}

void
set_graphics_root_signature(CmdList* cmd)
{
  ASSERT(g_RootSignature != nullptr);
  cmd->d3d12_list->SetGraphicsRootSignature(g_RootSignature);
}

void
set_compute_root_signature(CmdList* cmd)
{
  ASSERT(g_RootSignature != nullptr);
  cmd->d3d12_list->SetComputeRootSignature(g_RootSignature);
}

void
build_rt_blas(
  CmdList*         cmd,
  const GpuRtBlas& blas,
  const GpuBuffer& scratch,
  u32              scratch_offset,
  const GpuBuffer& index_buffer,
  const GpuBuffer& vertex_buffer,
  u32              flags
) {
  D3D12_RAYTRACING_GEOMETRY_DESC geometry_desc       = get_d3d12_blas_desc(blas.desc, vertex_buffer, index_buffer);

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
  inputs.DescsLayout                                 = D3D12_ELEMENTS_LAYOUT_ARRAY;
  inputs.Flags                                       = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;

  if (flags & kGpuRtasBuildIncremental)
  {
    ASSERT_MSG_FATAL(blas.desc.allow_updates, "Attempting to perform incremental RT BLAS update build on BLAS that was not created with allow_updates");
    if (blas.desc.allow_updates)
    {
      inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
    }
  }

  inputs.NumDescs                                    = 1;
  inputs.Type                                        = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
  inputs.pGeometryDescs                              = &geometry_desc;

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {};
  desc.Inputs                           = inputs;
  desc.SourceAccelerationStructureData  = 0;
  desc.DestAccelerationStructureData    = blas.buffer.gpu_addr;
  desc.ScratchAccelerationStructureData = scratch.gpu_addr + scratch_offset;

  gpu_memory_barrier(cmd);
  cmd->d3d12_list->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);
  gpu_memory_barrier(cmd);
}

void
build_rt_tlas(
  CmdList*         cmd,
  const GpuRtTlas& tlas,
  const GpuBuffer& instance_buffer,
  u32              instance_count,
  const GpuBuffer& scratch,
  u32              scratch_offset,
  u32              flags
) {
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
  inputs.DescsLayout    = D3D12_ELEMENTS_LAYOUT_ARRAY;
  inputs.Flags          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_ALLOW_UPDATE | D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_BUILD;

  if (flags & kGpuRtasBuildIncremental)
  {
    inputs.Flags |= D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PERFORM_UPDATE;
  }

  ASSERT_MSG_FATAL(instance_count <= tlas.max_instances, "TLAS has a maximum instance count of %u, but ");

  inputs.NumDescs       = instance_count;
  inputs.Type           = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
  inputs.pGeometryDescs = nullptr;
  inputs.InstanceDescs  = instance_buffer.gpu_addr;

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC desc = {};
  desc.Inputs                           = inputs;
  desc.SourceAccelerationStructureData  = (flags & kGpuRtasBuildIncremental) ? tlas.buffer.gpu_addr : 0;
  desc.DestAccelerationStructureData    = tlas.buffer.gpu_addr;
  desc.ScratchAccelerationStructureData = scratch.gpu_addr + scratch_offset;

  gpu_memory_barrier(cmd);
  cmd->d3d12_list->BuildRaytracingAccelerationStructure(&desc, 0, nullptr);
  gpu_memory_barrier(cmd);
}

void
gpu_copy_buffer(
  CmdList* cmd,
  const    GpuBuffer& dst,
  u64      dst_offset,
  const    GpuBuffer& src,
  u64      src_offset,
  u64      bytes
) {
  cmd->d3d12_list->CopyBufferRegion(dst.d3d12_buffer, dst_offset, src.d3d12_buffer, src_offset, bytes);
}

GpuTextureCopyableFootprint
gpu_get_texture_copyable_footprint(const GpuTextureDesc& desc)
{
  D3D12_RESOURCE_DESC1 d3d12_desc = d3d12_resource_desc(desc);

  D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint;
  u32 row_count;
  u64 row_byte_count;
  u64 total_size;
  g_GpuDevice->d3d12->GetCopyableFootprints1(&d3d12_desc, 0, 1, 0, &footprint, &row_count, &row_byte_count, &total_size);

  GpuTextureCopyableFootprint ret;
  ret.offset                = footprint.Offset;
  ret.row_count             = row_count;
  ret.row_byte_count        = row_byte_count;
  ret.row_padded_byte_count = footprint.Footprint.RowPitch;
  ret.total_size            = total_size;
  return ret;
}

void
gpu_copy_texture(
        CmdList*    cmd,
        GpuTexture* dst,
  const GpuBuffer&  src,
        u64         src_offset,
        u64         src_size
) {
  D3D12_RESOURCE_DESC1 desc = d3d12_resource_desc(dst->desc);

  D3D12_PLACED_SUBRESOURCE_FOOTPRINT src_footprint;
  u32 row_count;
  u64 row_byte_count;
  u64 total_size;
  g_GpuDevice->d3d12->GetCopyableFootprints1(&desc, 0, 1, 0, &src_footprint, &row_count, &row_byte_count, &total_size);
  src_footprint.Offset = src_offset;

  ASSERT_MSG_FATAL(total_size == src_size, "Size mismatch for copyable footprint of buffer! Are you copying to the correct format?");
  

  D3D12_TEXTURE_COPY_LOCATION src_location = {0};
  src_location.pResource        = src.d3d12_buffer;
  src_location.Type             = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
  src_location.PlacedFootprint  = src_footprint;

  D3D12_TEXTURE_COPY_LOCATION dst_location = {0};
  dst_location.pResource        = dst->d3d12_texture;
  dst_location.Type             = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  dst_location.SubresourceIndex = 0;

  cmd->d3d12_list->CopyTextureRegion(&dst_location, 0, 0, 0, &src_location, nullptr);
}

void
gpu_memory_barrier(CmdList* cmd)
{
  // TODO(bshihabi): We can add more fine-grained caches in the future, this is fine for now.
  D3D12_GLOBAL_BARRIER barrier;
  barrier.SyncBefore    = D3D12_BARRIER_SYNC_ALL;
  barrier.SyncAfter     = D3D12_BARRIER_SYNC_ALL;
  barrier.AccessBefore  = D3D12_BARRIER_ACCESS_COMMON;
  barrier.AccessAfter   = D3D12_BARRIER_ACCESS_COMMON;

  D3D12_BARRIER_GROUP group;
  group.Type            = D3D12_BARRIER_TYPE_GLOBAL;
  group.NumBarriers     = 1;
  group.pGlobalBarriers = &barrier;

  cmd->d3d12_list->Barrier(1, &group);
}

void
gpu_texture_layout_transition(CmdList* cmd, GpuTexture* texture, GpuTextureLayout layout, GpuTextureLoadOp load_op)
{
  D3D12_TEXTURE_BARRIER barrier;
  barrier.SyncBefore                        = D3D12_BARRIER_SYNC_ALL;
  barrier.SyncAfter                         = D3D12_BARRIER_SYNC_ALL;
  barrier.AccessBefore                      = D3D12_BARRIER_ACCESS_COMMON;
  switch (layout)
  {
    case kGpuTextureLayoutGeneral:         barrier.AccessAfter = D3D12_BARRIER_ACCESS_COMMON;               break;
    case kGpuTextureLayoutShaderResource:  barrier.AccessAfter = D3D12_BARRIER_ACCESS_COMMON;               break;
    case kGpuTextureLayoutUnorderedAccess: barrier.AccessAfter = D3D12_BARRIER_ACCESS_UNORDERED_ACCESS;     break;
    case kGpuTextureLayoutRenderTarget:    barrier.AccessAfter = D3D12_BARRIER_ACCESS_RENDER_TARGET;        break;
    case kGpuTextureLayoutDepthStencil:    barrier.AccessAfter = D3D12_BARRIER_ACCESS_DEPTH_STENCIL_WRITE;  break;
    case kGpuTextureLayoutDiscard:         barrier.AccessAfter = D3D12_BARRIER_ACCESS_NO_ACCESS;            break;
    default: UNREACHABLE;
  }
  barrier.LayoutBefore                      = gpu_texture_layout_to_d3d12(texture->layout);
  barrier.LayoutAfter                       = gpu_texture_layout_to_d3d12(layout);
  barrier.pResource                         = texture->d3d12_texture;
  barrier.Flags                             = D3D12_TEXTURE_BARRIER_FLAG_NONE;
  barrier.Subresources                      = CD3DX12_BARRIER_SUBRESOURCE_RANGE(0xffffffff);

  if (load_op == kGpuTextureLoadOpDiscard)
  {
    barrier.LayoutBefore = gpu_texture_layout_to_d3d12(kGpuTextureLayoutDiscard);
    barrier.AccessBefore = D3D12_BARRIER_ACCESS_NO_ACCESS;
    barrier.Flags       |= D3D12_TEXTURE_BARRIER_FLAG_DISCARD;
  }

  D3D12_BARRIER_GROUP group;
  group.Type             = D3D12_BARRIER_TYPE_TEXTURE;
  group.NumBarriers      = 1;
  group.pTextureBarriers = &barrier;

  cmd->d3d12_list->Barrier(1, &group);

  texture->layout = layout;
}

void
gpu_bind_root_srv(CmdList* cmd, u32 idx, const GpuBuffer&  buffer)
{
  gpu_memory_barrier(cmd);
  cmd->d3d12_list->SetGraphicsRootShaderResourceView(idx, buffer.gpu_addr);
  cmd->d3d12_list->SetComputeRootShaderResourceView (idx, buffer.gpu_addr);
}

void
gpu_bind_root_constants(CmdList* cmd, u32 idx, const u32* constants, u32 count)
{
  cmd->d3d12_list->SetGraphicsRoot32BitConstants(idx, count, constants, 0);
  cmd->d3d12_list->SetComputeRoot32BitConstants (idx, count, constants, 0);
}

void
gpu_bind_graphics_pso(CmdList* cmd, const GraphicsPSO& pso)
{
  // Hot reloads need to be handled carefully here...
  if (pso.desc.vertex_shader->generation != pso.vertex_shader_generation || pso.desc.pixel_shader->generation != pso.pixel_shader_generation)
  {
    // Not sure if there is a nicer way of handling this other than const_cast
    // In general it's only an issue for development builds so this entire bit of code
    // will just go away so I think it's okay.
    reload_graphics_pipeline(const_cast<GraphicsPSO*>(&pso));
  }
  cmd->d3d12_list->SetPipelineState(pso.d3d12_pso);
}

void
gpu_bind_compute_pso(CmdList* cmd, const ComputePSO& pso)
{
  // Hot reloads need to be handled carefully here...
  if (pso.compute_shader->generation != pso.compute_shader_generation)
  {
    // Not sure if there is a nicer way of handling this other than const_cast
    // In general it's only an issue for development builds so this entire bit of code
    // will just go away so I think it's okay.
    reload_compute_pipeline(const_cast<ComputePSO*>(&pso));
  }
  cmd->d3d12_list->SetPipelineState(pso.d3d12_pso);
}

void
gpu_dispatch(CmdList* cmd, u32 x, u32 y, u32 z)
{
  cmd->d3d12_list->Dispatch(x, y, z);
}

void
gpu_draw_instanced(CmdList* cmd, u32 vertex_count_per_instance, u32 instance_count, u32 start_vertex_location, u32 start_instance_location)
{
  cmd->d3d12_list->DrawInstanced(vertex_count_per_instance, instance_count, start_vertex_location, start_instance_location);
}

void
gpu_draw_indexed_instanced(CmdList* cmd, u32 index_count_per_instance, u32 instance_count, u32 start_index_location, s32 base_vertex_location, u32 start_instance_location)
{
  cmd->d3d12_list->DrawIndexedInstanced(
    index_count_per_instance,
    instance_count,
    start_index_location,
    base_vertex_location,
    start_instance_location
  );
}

void
gpu_clear_render_target(CmdList* cmd, const GpuDescriptor* rtv, const Vec4& clear_color)
{
  cmd->d3d12_list->ClearRenderTargetView(rtv->cpu_handle, (const f32*)&clear_color.r, 0, nullptr);
}

void
gpu_set_viewports(CmdList* cmd, f32 left, f32 top, f32 width, f32 height)
{
  auto viewport = CD3DX12_VIEWPORT(left, top, width, height);
  cmd->d3d12_list->RSSetViewports(1, &viewport);
  auto rect = CD3DX12_RECT(0, 0, S32_MAX, S32_MAX);
  cmd->d3d12_list->RSSetScissorRects(1, &rect);
}

void
gpu_set_scissor_rect(CmdList* cmd, s32 left, s32 top, s32 right, s32 bottom)
{
  auto rect = CD3DX12_RECT(left, top, right, bottom);
  cmd->d3d12_list->RSSetScissorRects(1, &rect);
}

void
gpu_clear_depth_stencil(CmdList* cmd, const GpuDescriptor* dsv, DepthStencilClearFlags flags, f32 depth, u8 stencil)
{
  D3D12_CLEAR_FLAGS d3d12_flags = (D3D12_CLEAR_FLAGS)0;
  if (flags & kClearDepth)   d3d12_flags |= D3D12_CLEAR_FLAG_DEPTH;
  if (flags & kClearStencil) d3d12_flags |= D3D12_CLEAR_FLAG_STENCIL;
  cmd->d3d12_list->ClearDepthStencilView(dsv->cpu_handle, d3d12_flags, depth, stencil, 0, nullptr);
}

void
gpu_ia_set_primitive_topology(CmdList* cmd, D3D12_PRIMITIVE_TOPOLOGY topology)
{
  cmd->d3d12_list->IASetPrimitiveTopology(topology);
}

void
gpu_ia_set_index_buffer(CmdList* cmd, const GpuBuffer* buffer, u32 stride, u32 size)
{
  D3D12_INDEX_BUFFER_VIEW view    = {};
  view.BufferLocation             = buffer->gpu_addr;
  view.SizeInBytes                = size ? size : buffer->desc.size;
  view.Format                     = stride == 2 ? DXGI_FORMAT_R16_UINT : DXGI_FORMAT_R32_UINT;
  cmd->d3d12_list->IASetIndexBuffer(&view);
}

void
gpu_multi_draw_indirect_indexed(
  CmdList*         cmd,
  const GpuBuffer* args_buffer,
  const GpuBuffer* count_buffer,
  u64              args_offset,
  u32              max_draw_count
) {
  cmd->d3d12_list->ExecuteIndirect(
    g_GpuDevice->d3d12_multi_draw_indirect_indexed_signature,
    max_draw_count,
    args_buffer->d3d12_buffer,
    args_offset,
    count_buffer ? count_buffer->d3d12_buffer : nullptr,
    0
  );
}

void
gpu_multi_draw_indirect(
  CmdList*         cmd,
  const GpuBuffer* args_buffer,
  const GpuBuffer* count_buffer,
  u64              args_offset,
  u32              max_draw_count
) {
  cmd->d3d12_list->ExecuteIndirect(
    g_GpuDevice->d3d12_multi_draw_indirect_signature,
    max_draw_count,
    args_buffer->d3d12_buffer,
    args_offset,
    count_buffer ? count_buffer->d3d12_buffer : nullptr,
    0
  );
}

void
gpu_bind_render_targets(CmdList* cmd, const GpuDescriptor* rtvs, u32 rtv_count, Option<GpuDescriptor> dsv)
{
  D3D12_CPU_DESCRIPTOR_HANDLE cpu_handles[kMaxRenderTargetCount];

  for (u32 irtv = 0; irtv < rtv_count; irtv++)
  {
    cpu_handles[irtv] = rtvs[irtv].cpu_handle;
  }

  if (dsv)
  {
    cmd->d3d12_list->OMSetRenderTargets(rtv_count, cpu_handles, FALSE, &unwrap(dsv).cpu_handle);
  }
  else
  {
    cmd->d3d12_list->OMSetRenderTargets(rtv_count, cpu_handles, FALSE, nullptr);
  }
}

void
gpu_bind_descriptor_heap(CmdList* cmd, const DescriptorLinearAllocator* heap)
{
  cmd->d3d12_list->SetDescriptorHeaps(1, &heap->d3d12_heap);
}

void
init_imgui_ctx(
  const GpuDevice* device,
  GpuFormat rtv_format,
  HWND window,
  DescriptorLinearAllocator* cbv_srv_uav_heap
) {
  ASSERT(cbv_srv_uav_heap->type == kDescriptorHeapTypeCbvSrvUav);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImPlot::CreateContext();
  ImGuiIO* io = &ImGui::GetIO();
  io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

  ImGui::StyleColorsDark();

  GpuDescriptor descriptor = alloc_descriptor(cbv_srv_uav_heap);

  ImGui_ImplWin32_Init(window);
  ImGui_ImplDX12_Init(
    device->d3d12,
    kBackBufferCount,
    gpu_format_to_d3d12(rtv_format),
    cbv_srv_uav_heap->d3d12_heap,
    descriptor.cpu_handle,
    unwrap(descriptor.gpu_handle)
  );
}

void
destroy_imgui_ctx()
{
  ImGui_ImplDX12_Shutdown();
  ImGui_ImplWin32_Shutdown();
}

void
imgui_begin_frame()
{
  ImGui_ImplDX12_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();
}

void
imgui_end_frame()
{
  ImGui::Render();
}

void
imgui_render(CmdList* cmd)
{
  ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmd->d3d12_list);
}
