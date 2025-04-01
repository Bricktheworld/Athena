#include "Core/Foundation/context.h"
#include "Core/Foundation/colors.h"

#include "Core/Engine/memory.h"
#include "Core/Engine/job_system.h"
#include "Core/Engine/Render/graphics.h"
#include "Core/Engine/Render/frame_time.h"

#include "Core/Vendor/D3D12/d3dx12.h"

#include "Core/Engine/Vendor/imgui/imgui.h"
#include "Core/Engine/Vendor/imgui/imgui_impl_win32.h"
#include "Core/Engine/Vendor/imgui/imgui_impl_dx12.h"

#include <d3dcompiler.h>

#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "d3dcompiler.lib")

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 614;}
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
init_d3d12_device(HWND window, IDXGIFactory7* factory, IDXGIAdapter1** out_adapter, ID3D12Device6** out_device, wchar_t out_name[128])
{
  *out_adapter = nullptr;
  *out_device  = nullptr;
  size_t max_dedicated_vram = 0;
  bool   hdr_support        = false;
  IDXGIAdapter1* current_adapter = nullptr;
  for (u32 i = 0; factory->EnumAdapters1(i, &current_adapter) != DXGI_ERROR_NOT_FOUND; i++)
  {
    DXGI_ADAPTER_DESC1 dxgi_adapter_desc = {0};
    ID3D12Device6* current_device = nullptr;
    HASSERT(current_adapter->GetDesc1(&dxgi_adapter_desc));

    if ((dxgi_adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 || 
      FAILED(D3D12CreateDevice(current_adapter, D3D_FEATURE_LEVEL_12_0, __uuidof(ID3D12Device6), (void**)&current_device)) ||
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
  ret.lists = init_ring_queue<ID3D12GraphicsCommandList4*>(heap, pool_size);

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
    ID3D12GraphicsCommandList4* list = nullptr;
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
    ID3D12GraphicsCommandList4* list = nullptr;
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

GpuLinearAllocator
init_gpu_linear_allocator(u32 size, GpuHeapLocation location)
{
  GpuLinearAllocator ret = {0};
  ret.pos = 0;
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
destroy_gpu_linear_allocator(GpuLinearAllocator* allocator)
{
  COM_RELEASE(allocator->d3d12_heap);
  zero_memory(allocator, sizeof(GpuLinearAllocator));
}

GpuAllocation
gpu_linear_alloc(void* allocator, u32 size, u32 alignment)
{
  GpuLinearAllocator* self    = (GpuLinearAllocator*)allocator;
  u32                 offset  = ALIGN_POW2(self->pos, alignment);
  u32                 new_pos = offset + size;

  ASSERT_MSG_FATAL(new_pos <= self->size, "GPU linear allocator ran out of memory! Attempted to allocate 0x%x bytes, 0x%x bytes available", size, self->size - self->pos);

  self->pos = new_pos;


  GpuAllocation ret = {0};
  ret.size          = size;
  ret.offset        = offset;
  ret.d3d12_heap    = self->d3d12_heap;
  ret.metadata      = 0;
  ret.location      = self->location;

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
  GpuBufferDesc readback_desc = {0};
  readback_desc.size          = sizeof(u64) * desc.Count * kBackBufferCount;

  profiler->timestamp_readback = alloc_gpu_buffer_no_heap(g_GpuDevice, readback_desc, kGpuHeapSysRAMGpuToCpu, "Timestamp readback");
  profiler->name_to_timestamp  = init_hash_table<const char*, u32>(g_InitHeap, kMaxGpuTimestamps);
  zero_memory(profiler->timestamps, sizeof(profiler->timestamps));


  profiler->next_free_idx      = 0;
  HASSERT(g_GpuDevice->graphics_queue.d3d12_queue->GetTimestampFrequency(&profiler->gpu_frequency));
}

void
begin_gpu_profiler_timestamp(const CmdList& cmd_buffer, const char* name)
{
  GpuProfiler* profiler = &g_GpuDevice->profiler;

  u32 idx = 0;
  {
    u32* existing = hash_table_find(&profiler->name_to_timestamp, name);
    if (existing == nullptr)
    {
      ASSERT_MSG_FATAL(
        profiler->next_free_idx < kMaxGpuTimestamps,
        "Attempting to start a GPU timestamp name %s but no slots left! You are permitted up to %u GPU timestamps. If you need more, try incrementing kMaxGpuTimestamps", name, kMaxGpuTimestamps
      );
      u32* dst = hash_table_insert(&profiler->name_to_timestamp, name);
      *dst = profiler->next_free_idx++;
      existing = dst;
    }

    idx = *existing;
  }

  GpuTimestamp* timestamp = profiler->timestamps + idx;

  ASSERT_MSG_FATAL(!timestamp->in_flight, "GPU profiler timestamp name '%s' wasn't closed properly!", name);

  u32 start_idx = idx * 2;
  cmd_buffer.d3d12_list->EndQuery(profiler->d3d12_timestamp_heap, D3D12_QUERY_TYPE_TIMESTAMP, start_idx);

  timestamp->in_flight = true;
}

void
end_gpu_profiler_timestamp(const CmdList& cmd_buffer, const char* name)
{
  GpuProfiler* profiler = &g_GpuDevice->profiler;

  u32* ptr = hash_table_find(&profiler->name_to_timestamp, name);

  ASSERT_MSG_FATAL(ptr != nullptr, "GPU profiler timestamp name '%s' is invalid! Did you forget to call begin_gpu_profiler_timestamp?", name);

  u32 idx       = *ptr;
  u32 start_idx = idx * 2;
  u32 end_idx   = start_idx + 1;

  GpuTimestamp* timestamp = profiler->timestamps + idx;

  cmd_buffer.d3d12_list->EndQuery(profiler->d3d12_timestamp_heap, D3D12_QUERY_TYPE_TIMESTAMP, end_idx);

  u64 dst_offset = sizeof(u64) * ((g_FrameId % kBackBufferCount) * kMaxGpuTimestamps * 2 + start_idx);
  cmd_buffer.d3d12_list->ResolveQueryData(
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

f64
query_gpu_profiler_timestamp(const char* name)
{
  if (g_FrameId < 1)
  {
    return 0.0;
  }

  GpuProfiler* profiler = &g_GpuDevice->profiler;

  // TODO(bshihabi): Currently these time samples have a 1 frame delay.
  // I designed it like this because the render graph won't actually have the query
  // data because command lists are submitted all at once. This needs to be fixed first.

  u32* ptr = hash_table_find(&profiler->name_to_timestamp, name);

  ASSERT_MSG_FATAL(ptr != nullptr, "GPU profiler timestamp name '%s' is invalid! Did you forget to call begin_gpu_profiler_timestamp?", name);

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

static void
init_d3d12_indirect(GpuDevice* device)
{
  {
    D3D12_INDIRECT_ARGUMENT_DESC arg_desc = {};
    arg_desc.Type         = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW;

    D3D12_COMMAND_SIGNATURE_DESC desc = {};
    desc.ByteStride       = sizeof(MultiDrawIndirectArgs);
    desc.NumArgumentDescs = 1;
    desc.pArgumentDescs   = &arg_desc;
    desc.NodeMask         = 0;
    static_assert(sizeof(MultiDrawIndirectArgs) >= sizeof(D3D12_DRAW_ARGUMENTS));

    HASSERT(
      g_GpuDevice->d3d12->CreateCommandSignature(
        &desc,
        nullptr,
        IID_PPV_ARGS(&device->d3d12_multi_draw_indirect_signature)
      )
    );
  }

  {
    D3D12_INDIRECT_ARGUMENT_DESC arg_desc = {};
    arg_desc.Type         = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;

    D3D12_COMMAND_SIGNATURE_DESC desc = {};
    desc.ByteStride       = sizeof(MultiDrawIndirectIndexedArgs);
    desc.NumArgumentDescs = 1;
    desc.pArgumentDescs   = &arg_desc;
    desc.NodeMask         = 0;
    static_assert(sizeof(MultiDrawIndirectIndexedArgs) >= sizeof(D3D12_DRAW_INDEXED_ARGUMENTS));

    HASSERT(
      g_GpuDevice->d3d12->CreateCommandSignature(
        &desc,
        nullptr,
        IID_PPV_ARGS(&device->d3d12_multi_draw_indirect_indexed_signature)
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
        IID_PPV_ARGS(&device->d3d12_dispatch_indirect_signature)
      )
    );
  }
}

void
init_gpu_device(HWND window)
{
  if (g_GpuDevice == nullptr)
  {
    g_GpuDevice = HEAP_ALLOC(GpuDevice, g_InitHeap, 1);
  }

#ifdef DEBUG_LAYER
  ID3D12Debug* debug_interface = nullptr;
  HASSERT(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_interface)));
  debug_interface->EnableDebugLayer();
  defer { COM_RELEASE(debug_interface); };
#endif

  IDXGIFactory7* factory = init_factory();
  defer { COM_RELEASE(factory); };

  IDXGIAdapter1* adapter; 
  init_d3d12_device(window, factory, &adapter, &g_GpuDevice->d3d12, g_GpuDevice->gpu_name);
  defer { COM_RELEASE(adapter); };

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

  init_d3d12_indirect(g_GpuDevice);

  init_gpu_profiler();

#ifdef DEBUG_LAYER
  HASSERT(DXGIGetDebugInterface1(0, IID_PPV_ARGS(&g_GpuDevice->d3d12_debug)));
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
  g_GpuDevice->d3d12_debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
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

GpuTexture
alloc_gpu_texture_no_heap(const GpuDevice* device, GpuTextureDesc desc, const char* name)
{
  GpuTexture ret = {0};
  ret.desc = desc;

  D3D12_HEAP_PROPERTIES heap_props = CD3DX12_HEAP_PROPERTIES(get_d3d12_heap_location(kGpuHeapGpuOnly));
  D3D12_RESOURCE_DESC resource_desc;
  resource_desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  resource_desc.Format             = gpu_format_to_d3d12(desc.format);
  resource_desc.Alignment          = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  resource_desc.Width              = desc.width;
  resource_desc.Height             = desc.height;
  resource_desc.DepthOrArraySize   = MAX(desc.array_size, 1);
  resource_desc.MipLevels          = 1;
  resource_desc.SampleDesc.Count   = 1;
  resource_desc.SampleDesc.Quality = 0;
  resource_desc.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  resource_desc.Flags              = desc.flags;

  D3D12_CLEAR_VALUE  clear_value;
  D3D12_CLEAR_VALUE* p_clear_value = nullptr;
  clear_value.Format = gpu_format_to_d3d12(desc.format);
  if (is_depth_format(desc.format))
  {
    resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    clear_value.DepthStencil.Depth   = desc.depth_clear_value;
    clear_value.DepthStencil.Stencil = desc.stencil_clear_value;
    p_clear_value = &clear_value;
  }
  else if (desc.flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)
  {
    clear_value.Color[0] = desc.color_clear_value.x;
    clear_value.Color[1] = desc.color_clear_value.y;
    clear_value.Color[2] = desc.color_clear_value.z;
    clear_value.Color[3] = desc.color_clear_value.w;
    p_clear_value = &clear_value;
  }

  HASSERT(
    device->d3d12->CreateCommittedResource(
      &heap_props,
      D3D12_HEAP_FLAG_NONE,
      &resource_desc,
      desc.initial_state,
      p_clear_value,
      IID_PPV_ARGS(&ret.d3d12_texture)
    )
  );

  wchar_t wname[1024];
  mbstowcs_s(nullptr, wname, name, 1024);
  ret.d3d12_texture->SetName(wname);

  return ret;
}

void
free_gpu_texture(GpuTexture* texture)
{
  COM_RELEASE(texture->d3d12_texture);
  zero_memory(texture, sizeof(GpuTexture));
}

static D3D12_RESOURCE_DESC
d3d12_resource_desc(GpuTextureDesc desc)
{
  D3D12_RESOURCE_DESC resource_desc;
  resource_desc.Dimension          = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  resource_desc.Format             = gpu_format_to_d3d12(desc.format);
  resource_desc.Alignment          = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
  resource_desc.Width              = desc.width;
  resource_desc.Height             = desc.height;
  resource_desc.DepthOrArraySize   = desc.array_size;
  resource_desc.MipLevels          = 1;
  resource_desc.SampleDesc.Count   = 1;
  resource_desc.SampleDesc.Quality = 0;
  resource_desc.Layout             = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  resource_desc.Flags              = desc.flags;

  return resource_desc;
}


GpuTexture
alloc_gpu_texture(
  const GpuDevice* device,
  GpuAllocHeap heap,
  GpuTextureDesc desc,
  const char* name
) {
  GpuTexture ret = {0};
  ret.desc     = desc;

  D3D12_RESOURCE_DESC resource_desc   = d3d12_resource_desc(desc);

  D3D12_RESOURCE_ALLOCATION_INFO info = device->d3d12->GetResourceAllocationInfo(0, 1, &resource_desc);

  GpuAllocation allocation = GPU_HEAP_ALLOC(heap, (u32)info.SizeInBytes, (u32)info.Alignment);

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
    device->d3d12->CreatePlacedResource(
      allocation.d3d12_heap,
      allocation.offset,
      &resource_desc,
      desc.initial_state,
      p_clear_value,
      IID_PPV_ARGS(&ret.d3d12_texture)
    )
  );

  wchar_t wname[1024];
  mbstowcs_s(nullptr, wname, name, 1024);
  ret.d3d12_texture->SetName(wname);

  return ret;
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
  auto resource_desc = CD3DX12_RESOURCE_DESC::Buffer(desc.size, desc.flags);

  HASSERT(
    device->d3d12->CreateCommittedResource(
      &heap_props,
      D3D12_HEAP_FLAG_NONE,
      &resource_desc,
      desc.initial_state,
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

GpuBuffer
alloc_gpu_buffer(
  const GpuDevice* device,
  GpuAllocHeap heap,
  GpuBufferDesc desc,
  const char* name
) {
  // NOTE(Brandon): For simplicity's sake of CBVs, I just align all the sizes to 256.
  desc.size = ALIGN_POW2(desc.size, 256);

  GpuAllocation allocation = GPU_HEAP_ALLOC(heap, desc.size, (u32)KiB(64));

  GpuBuffer ret = {0};
  ret.desc = desc;

  auto resource_desc = CD3DX12_RESOURCE_DESC::Buffer(desc.size, desc.flags);

  HASSERT(
    device->d3d12->CreatePlacedResource(
      allocation.d3d12_heap,
      allocation.offset,
      &resource_desc,
      desc.initial_state,
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
init_descriptor_pool(AllocHeap heap, const GpuDevice* device, u32 size, DescriptorHeapType type, u32 table_reserved)
{
  ASSERT_MSG_FATAL(table_reserved < size, "The number of reserved entries for the descriptor table is more than the capacity for the descriptor pool.");

  DescriptorPool ret = {0};
  ret.num_descriptors = size;
  ret.type = type;
  ret.free_descriptors = init_ring_queue<u32>(heap, size);
  ret.d3d12_heap = init_d3d12_descriptor_heap(device, size, type);

  ret.descriptor_size = device->d3d12->GetDescriptorHandleIncrementSize(get_d3d12_descriptor_type(type));
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
  ret.type           = pool->type;

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
  ret.type           = pool->type;

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

  ret.type = allocator->type;

  return ret;
}


void
init_rtv(GpuDescriptor* descriptor, const GpuTexture* texture)
{
  ASSERT(descriptor->type == kDescriptorHeapTypeRtv);

  g_GpuDevice->d3d12->CreateRenderTargetView(
    texture->d3d12_texture,
    nullptr,
    descriptor->cpu_handle
  );

}

void
init_dsv(GpuDescriptor* descriptor, const GpuTexture* texture)
{
  ASSERT(is_depth_format(texture->desc.format));
  ASSERT(descriptor->type == kDescriptorHeapTypeDsv);

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
}

void
init_buffer_srv(
  GpuDescriptor* descriptor,
  const GpuBuffer* buffer,
  const GpuBufferSrvDesc& desc
) {
  ASSERT(descriptor->type == kDescriptorHeapTypeCbvSrvUav);
  D3D12_SHADER_RESOURCE_VIEW_DESC srv = {0};
  srv.Buffer.FirstElement        = desc.first_element;
  srv.Buffer.NumElements         = desc.num_elements;
  srv.Buffer.StructureByteStride = desc.stride;
  srv.Buffer.Flags               = desc.is_raw ? D3D12_BUFFER_SRV_FLAG_RAW : D3D12_BUFFER_SRV_FLAG_NONE;
  srv.Format                     = desc.is_raw ? DXGI_FORMAT_R32_TYPELESS : gpu_format_to_d3d12(desc.format);
  srv.ViewDimension              = D3D12_SRV_DIMENSION_BUFFER;
  srv.Shader4ComponentMapping    = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

  g_GpuDevice->d3d12->CreateShaderResourceView(buffer->d3d12_buffer, &srv, descriptor->cpu_handle);
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
  ASSERT(descriptor->type == kDescriptorHeapTypeCbvSrvUav);
  ASSERT(buffer_is_aligned(buffer, 256, desc.buffer_offset));
  ASSERT(desc.size <= U32_MAX);

  D3D12_CONSTANT_BUFFER_VIEW_DESC cbv = {0};
  cbv.BufferLocation = buffer->gpu_addr + desc.buffer_offset;
  cbv.SizeInBytes    = (u32)ALIGN_POW2(desc.size, 256);
  g_GpuDevice->d3d12->CreateConstantBufferView(&cbv, descriptor->cpu_handle);
}

void
init_buffer_uav(
  GpuDescriptor* descriptor,
  const GpuBuffer* buffer,
  const GpuBufferUavDesc& desc
) {
  ASSERT(descriptor->type == kDescriptorHeapTypeCbvSrvUav);

  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {0};
  uav.Buffer.FirstElement         = desc.first_element;
  uav.Buffer.NumElements          = desc.num_elements;
  uav.Buffer.StructureByteStride  = desc.stride;
  uav.Buffer.CounterOffsetInBytes = 0;
  uav.Buffer.Flags                = desc.is_raw ? D3D12_BUFFER_UAV_FLAG_RAW : D3D12_BUFFER_UAV_FLAG_NONE;
  uav.Format                      = desc.is_raw ? DXGI_FORMAT_R32_TYPELESS : gpu_format_to_d3d12(desc.format);
  uav.ViewDimension               = D3D12_UAV_DIMENSION_BUFFER;
  g_GpuDevice->d3d12->CreateUnorderedAccessView(buffer->d3d12_buffer, nullptr, &uav, descriptor->cpu_handle);
}

void
init_texture_srv(GpuDescriptor* descriptor, const GpuTexture* texture, const GpuTextureSrvDesc& desc)
{
  ASSERT(descriptor->type == kDescriptorHeapTypeCbvSrvUav);
  ASSERT(texture->desc.array_size >= 1);

  D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
  srv.Texture2D.MipLevels             = desc.mip_levels;
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
    srv.Texture2DArray.MostDetailedMip = 0;
    srv.Texture2DArray.MipLevels       = desc.mip_levels;
    srv.Texture2DArray.FirstArraySlice = 0;
    srv.Texture2DArray.ArraySize       = desc.array_size;
  }

  g_GpuDevice->d3d12->CreateShaderResourceView(texture->d3d12_texture, &srv, descriptor->cpu_handle);   
}

void
init_texture_uav(GpuDescriptor* descriptor, const GpuTexture* texture, const GpuTextureUavDesc& desc)
{
  ASSERT(descriptor->type == kDescriptorHeapTypeCbvSrvUav);

  D3D12_UNORDERED_ACCESS_VIEW_DESC uav = {0};
  uav.Format        = gpu_format_to_d3d12(desc.format);
  uav.ViewDimension = desc.array_size > 1 ? D3D12_UAV_DIMENSION_TEXTURE2DARRAY : D3D12_UAV_DIMENSION_TEXTURE2D;
  if (texture->desc.array_size > 1)
  {
    uav.Texture2DArray.MipSlice        = 0;
    uav.Texture2DArray.FirstArraySlice = 0;
    uav.Texture2DArray.ArraySize       = desc.array_size;
    uav.Texture2DArray.PlaneSlice      = 0;
  }
  g_GpuDevice->d3d12->CreateUnorderedAccessView(texture->d3d12_texture, nullptr, &uav, descriptor->cpu_handle);
}

void
init_bvh_srv(GpuDescriptor* descriptor, const GpuBvh* bvh)
{
  ASSERT(descriptor->type == kDescriptorHeapTypeCbvSrvUav);

  D3D12_SHADER_RESOURCE_VIEW_DESC desc = {0};
  desc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
  desc.RaytracingAccelerationStructure.Location = bvh->top_bvh.gpu_addr;
  desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

  g_GpuDevice->d3d12->CreateShaderResourceView(nullptr, &desc, descriptor->cpu_handle);
}

static ID3D12RootSignature*    g_RootSignature = nullptr;

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
  }
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
  return ret;
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

  return ret;
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
  ComputePSO ret;
  D3D12_COMPUTE_PIPELINE_STATE_DESC desc;
  desc.pRootSignature = g_RootSignature;
  desc.CS = CD3DX12_SHADER_BYTECODE(compute_shader->d3d12_shader);
  desc.NodeMask = 0;
  desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
  desc.CachedPSO.pCachedBlob = nullptr;
  desc.CachedPSO.CachedBlobSizeInBytes = 0;
  HASSERT(device->d3d12->CreateComputePipelineState(&desc, IID_PPV_ARGS(&ret.d3d12_pso)));

  wchar_t wname[1024];
  mbstowcs_s(nullptr, wname, name, 1024);
  ret.d3d12_pso->SetName(wname);

  return ret;
}

void
destroy_compute_pipeline(ComputePSO* pipeline)
{
  COM_RELEASE(pipeline->d3d12_pso);
  zero_memory(pipeline, sizeof(ComputePSO));
}

GpuBvh
init_gpu_bvh(
  GpuDevice* device,
  const GpuBuffer& vertex_uber_buffer,
  u32 vertex_count,
  u32 vertex_stride,
  const GpuBuffer& index_uber_buffer,
  u32 index_count,
  const char* name
) {
  // TODO(bshihabi): Give the acceleration structure a name!
  UNREFERENCED_PARAMETER(name);
  GpuBvh ret = {0};

  D3D12_RAYTRACING_GEOMETRY_DESC geometry_desc = {};
  geometry_desc.Type                                 = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
  geometry_desc.Flags                                = D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;
  geometry_desc.Triangles.IndexBuffer                = index_uber_buffer.gpu_addr;
  geometry_desc.Triangles.IndexCount                 = index_count;
  geometry_desc.Triangles.IndexFormat                = DXGI_FORMAT_R32_UINT;
  geometry_desc.Triangles.Transform3x4               = 0;
  geometry_desc.Triangles.VertexFormat               = DXGI_FORMAT_R32G32B32_FLOAT;
  geometry_desc.Triangles.VertexCount                = vertex_count;
  geometry_desc.Triangles.VertexBuffer.StartAddress  = vertex_uber_buffer.gpu_addr;
  geometry_desc.Triangles.VertexBuffer.StrideInBytes = vertex_stride;
  
  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS bottom_level_inputs = {};
  bottom_level_inputs.DescsLayout    = D3D12_ELEMENTS_LAYOUT_ARRAY;
  bottom_level_inputs.Flags          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
  bottom_level_inputs.NumDescs       = 1;
  bottom_level_inputs.Type           = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
  bottom_level_inputs.pGeometryDescs = &geometry_desc;

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO bottom_level_prebuild_info;
  device->d3d12->GetRaytracingAccelerationStructurePrebuildInfo(&bottom_level_inputs, &bottom_level_prebuild_info);
  bottom_level_prebuild_info.ScratchDataSizeInBytes   = ALIGN_POW2(bottom_level_prebuild_info.ScratchDataSizeInBytes,   D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
  bottom_level_prebuild_info.ResultDataMaxSizeInBytes = ALIGN_POW2(bottom_level_prebuild_info.ResultDataMaxSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
  ASSERT(bottom_level_prebuild_info.ResultDataMaxSizeInBytes > 0);

  ret.bottom_bvh = alloc_gpu_buffer_no_heap(
    device,
    {
      .size = (u32)bottom_level_prebuild_info.ResultDataMaxSizeInBytes,
      .flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      .initial_state = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE
    },
    kGpuHeapGpuOnly,
    "Bottom BVH Buffer"
  );

  D3D12_RAYTRACING_INSTANCE_DESC instance_desc = {};
  instance_desc.Transform[0][0] = 1;
  instance_desc.Transform[1][1] = 1;
  instance_desc.Transform[2][2] = 1;
  instance_desc.InstanceID      = 0;
  instance_desc.InstanceMask    = 0xFF;
  instance_desc.InstanceContributionToHitGroupIndex = 0;
  instance_desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
  instance_desc.AccelerationStructure = ret.bottom_bvh.gpu_addr;
  ret.instance_desc_buffer = alloc_gpu_buffer_no_heap(
    device,
    {.size = ALIGN_POW2(sizeof(instance_desc), D3D12_RAYTRACING_INSTANCE_DESCS_BYTE_ALIGNMENT)},
    kGpuHeapSysRAMCpuToGpu,
    "Instance Desc"
  );
  memcpy(unwrap(ret.instance_desc_buffer.mapped), &instance_desc, sizeof(instance_desc));

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS top_level_inputs = {};
  top_level_inputs.DescsLayout    = D3D12_ELEMENTS_LAYOUT_ARRAY;
  top_level_inputs.Flags          = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
  top_level_inputs.NumDescs       = 1;
  top_level_inputs.Type           = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
  top_level_inputs.pGeometryDescs = nullptr;
  top_level_inputs.InstanceDescs  = ret.instance_desc_buffer.gpu_addr;

  D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO top_level_prebuild_info;
  device->d3d12->GetRaytracingAccelerationStructurePrebuildInfo(&top_level_inputs, &top_level_prebuild_info);
  top_level_prebuild_info.ScratchDataSizeInBytes = ALIGN_POW2(top_level_prebuild_info.ScratchDataSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
  top_level_prebuild_info.ResultDataMaxSizeInBytes = ALIGN_POW2(top_level_prebuild_info.ResultDataMaxSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
  ASSERT(top_level_prebuild_info.ResultDataMaxSizeInBytes > 0);

  ret.top_bvh = alloc_gpu_buffer_no_heap(
    device,
    {
      .size = (u32)top_level_prebuild_info.ResultDataMaxSizeInBytes,
      .flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      .initial_state = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE
    },
    kGpuHeapGpuOnly,
    "Top BVH Buffer"
  );

  GpuBuffer top_level_scratch = alloc_gpu_buffer_no_heap(
    device,
    {
      .size = (u32)top_level_prebuild_info.ScratchDataSizeInBytes,
      .flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
    },
    kGpuHeapGpuOnly,
    "BVH Scratch Buffer"
  );

  GpuBuffer bottom_level_scratch = alloc_gpu_buffer_no_heap(
    device,
    {
      .size = (u32)bottom_level_prebuild_info.ScratchDataSizeInBytes,
      .flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
    },
    kGpuHeapGpuOnly,
    "BVH Scratch Buffer"
  );

  defer
  {
    free_gpu_buffer(&top_level_scratch); 
    free_gpu_buffer(&bottom_level_scratch);
  };

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC bottom_level_build_desc = {};
  bottom_level_build_desc.Inputs                           = bottom_level_inputs;
  bottom_level_build_desc.ScratchAccelerationStructureData = bottom_level_scratch.gpu_addr;
  bottom_level_build_desc.DestAccelerationStructureData    = ret.bottom_bvh.gpu_addr;

  D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC top_level_build_desc = {};
  top_level_build_desc.Inputs                           = top_level_inputs;
  top_level_build_desc.ScratchAccelerationStructureData = top_level_scratch.gpu_addr;
  top_level_build_desc.DestAccelerationStructureData    = ret.top_bvh.gpu_addr;


  CmdList cmd_list = alloc_cmd_list(&device->graphics_cmd_allocator);
  D3D12_RESOURCE_BARRIER uav_barrier = {};
  uav_barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
  uav_barrier.UAV.pResource = nullptr;
  cmd_list.d3d12_list->BuildRaytracingAccelerationStructure(&bottom_level_build_desc, 0, nullptr);
  cmd_list.d3d12_list->ResourceBarrier(1, &uav_barrier);
  cmd_list.d3d12_list->BuildRaytracingAccelerationStructure(&top_level_build_desc, 0, nullptr);
  cmd_list.d3d12_list->ResourceBarrier(1, &uav_barrier);
  GpuFence fence = init_gpu_fence();
  defer { destroy_gpu_fence(&fence); };

  FenceValue fence_value = submit_cmd_lists(&device->graphics_cmd_allocator, {cmd_list}, &fence);

  block_gpu_fence(&fence, fence_value);

  return ret;
}

void
destroy_acceleration_structure(GpuBvh* bvh)
{
  free_gpu_buffer(&bvh->instance_desc_buffer);
  free_gpu_buffer(&bvh->bottom_bvh);
  free_gpu_buffer(&bvh->top_bvh);
  zero_memory(bvh, sizeof(GpuBvh));
}

RayTracingPSO
init_ray_tracing_pipeline(const GpuDevice* device, const GpuShader* ray_tracing_library, const char* name)
{
  RayTracingPSO ret;

  CD3DX12_STATE_OBJECT_DESC desc = {D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE};

  auto* library = desc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
  auto shader_byte_code = CD3DX12_SHADER_BYTECODE(ray_tracing_library->d3d12_shader);
  library->SetDXILLibrary(&shader_byte_code);
  HASSERT(device->d3d12->CreateStateObject(desc, IID_PPV_ARGS(&ret.d3d12_pso)));

  HASSERT(ret.d3d12_pso->QueryInterface(IID_PPV_ARGS(&ret.d3d12_properties)));

  wchar_t wname[1024];
  mbstowcs_s(nullptr, wname, name, 1024);
  ret.d3d12_pso->SetName(wname);

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
  desc.width = swap_chain->width;
  desc.height = swap_chain->height;
  desc.format = swap_chain->format;
  desc.initial_state = D3D12_RESOURCE_STATE_PRESENT;
  for (u32 i = 0; i < num_back_buffers; i++)
  {
    HASSERT(swap_chain->d3d12_swap_chain->GetBuffer(i, IID_PPV_ARGS(&back_buffers[i]->d3d12_texture)));
    back_buffers[i]->desc = desc;
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


const GpuTexture*
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

void
swap_chain_submit(SwapChain* swap_chain, const GpuDevice* device, const GpuTexture* rtv)
{
  u32 index = swap_chain->back_buffer_index;
  ASSERT(swap_chain->back_buffers[index] == rtv);

  FenceValue value = cmd_queue_signal(&device->graphics_queue, &swap_chain->fence);
  swap_chain->frame_fence_values[index] = value;

  u32 sync_interval = swap_chain->vsync ? 1 : 0;
  u32 present_flags = swap_chain->tearing_supported && !swap_chain->vsync ? DXGI_PRESENT_ALLOW_TEARING : 0;
  HASSERT(swap_chain->d3d12_swap_chain->Present(sync_interval, present_flags));

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
init_imgui_ctx(
  const GpuDevice* device,
  GpuFormat rtv_format,
  HWND window,
  DescriptorLinearAllocator* cbv_srv_uav_heap
) {
  ASSERT(cbv_srv_uav_heap->type == kDescriptorHeapTypeCbvSrvUav);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
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
