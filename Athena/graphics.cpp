#include "graphics.h"
#include "job_system.h"
#include "memory/memory.h"
#include <windows.h>
#include <d3dcompiler.h>
#include "pix3.h"
#include "vendor/d3dx12.h"
#include "vendor/imgui/imgui.h"
#include "vendor/imgui/imgui_impl_win32.h"
#include "vendor/imgui/imgui_impl_dx12.h"
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "d3dcompiler.lib")

#ifdef DEBUG
//#define DEBUG_LAYER
#endif

namespace gfx
{
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
  
  static void
  init_d3d12_device(IDXGIFactory7* factory, IDXGIAdapter1** out_adapter, ID3D12Device6** out_device)
  {
    *out_adapter = nullptr;
    *out_device = nullptr;
    size_t max_dedicated_vram = 0;
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
      COM_RELEASE((*out_adapter));
      COM_RELEASE((*out_device));
      *out_adapter = current_adapter;
      *out_device  = current_device;

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
      case kCmdQueueTypeGraphics:
        return D3D12_COMMAND_LIST_TYPE_DIRECT;
      case kCmdQueueTypeCompute:
        return D3D12_COMMAND_LIST_TYPE_COMPUTE;
      case kCmdQueueTypeCopy:
        return D3D12_COMMAND_LIST_TYPE_COPY;
      default:
        UNREACHABLE;
    }
    return D3D12_COMMAND_LIST_TYPE_NONE;
  }
  
  static ID3D12Fence* 
  init_fence(ID3D12Device2* d3d12_dev)
  {
    ID3D12Fence* fence = nullptr;
    HASSERT(d3d12_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
    ASSERT(fence != nullptr);
  
    return fence;
  }
  
  Fence
  init_fence(const GraphicsDevice* device)
  {
    Fence ret = {0};
    HASSERT(device->d3d12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&ret.d3d12_fence)));
    ASSERT(ret.d3d12_fence != nullptr);
  
    ret.cpu_event = CreateEventW(NULL, FALSE, FALSE, NULL);
    ret.value = 0;
    ret.last_completed_value = 0;
    ret.last_completed_value = 0;
    ret.already_waiting = false;
  
    return ret;
  }
  
  void
  destroy_fence(Fence* fence)
  {
    CloseHandle(fence->cpu_event);
    COM_RELEASE(fence->d3d12_fence);
    zero_memory(fence, sizeof(Fence));
  }
  
  static FenceValue
  inc_fence(Fence* fence)
  {
    return ++fence->value;
  }
  
  static FenceValue
  poll_fence_value(Fence* fence)
  {
    fence->last_completed_value = max(fence->last_completed_value, fence->d3d12_fence->GetCompletedValue());
    return fence->last_completed_value;
  }
  
  static bool
  is_fence_complete(Fence* fence, FenceValue value)
  {
    if (value > fence->last_completed_value)
    {
      poll_fence_value(fence);
    }
  
    return value <= fence->last_completed_value;
  }
  
  void
  yield_for_fence_value(Fence* fence, FenceValue value)
  {
    if (is_fence_complete(fence, value))
      return;
  
    yield_async([&]()
    {
      block_for_fence_value(fence, value);
    });
  }
  
  void
  block_for_fence_value(Fence* fence, FenceValue value)
  {
    ASSERT(!fence->already_waiting);
    if (is_fence_complete(fence, value))
      return;
  
    HASSERT(fence->d3d12_fence->SetEventOnCompletion(value, fence->cpu_event));
    fence->already_waiting = true;
  
    WaitForSingleObject(fence->cpu_event, -1);
    poll_fence_value(fence);
    fence->already_waiting = false;
  }
  
  CmdQueue
  init_cmd_queue(const GraphicsDevice* device, CmdQueueType type)
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
  cmd_queue_gpu_wait_for_fence(const CmdQueue* queue, Fence* fence, FenceValue value)
  {
    HASSERT(queue->d3d12_queue->Wait(fence->d3d12_fence, value));
  }
  
  FenceValue
  cmd_queue_signal(const CmdQueue* queue, Fence* fence)
  {
    FenceValue value = inc_fence(fence);
    HASSERT(queue->d3d12_queue->Signal(fence->d3d12_fence, value));
    return value;
  }
  
  
  CmdListAllocator
  init_cmd_list_allocator(MEMORY_ARENA_PARAM,
                          const GraphicsDevice* device,
                          const CmdQueue* queue,
                          u16 pool_size)
  {
    ASSERT(pool_size > 0);
    CmdListAllocator ret = {0};
    ret.d3d12_queue = queue->d3d12_queue;
    ret.fence = init_fence(device);
    ret.allocators = init_ring_queue<CmdAllocator>(MEMORY_ARENA_FWD, pool_size);
    ret.lists = init_ring_queue<ID3D12GraphicsCommandList4*>(MEMORY_ARENA_FWD, pool_size);
  
    CmdAllocator allocator = {0};
    for (u16 i = 0; i < pool_size; i++)
    {
      HASSERT(device->d3d12->CreateCommandAllocator(get_d3d12_cmd_list_type(queue->type),
                                                    IID_PPV_ARGS(&allocator.d3d12_allocator)));
      allocator.fence_value = 0;
      ring_queue_push(&ret.allocators, allocator);
    }
  
    for (u16 i = 0; i < pool_size; i++)
    {
      ID3D12GraphicsCommandList4* list = nullptr;
      HASSERT(device->d3d12->CreateCommandList(0,
                                              get_d3d12_cmd_list_type(queue->type),
                                              allocator.d3d12_allocator,
                                              nullptr,
                                              IID_PPV_ARGS(&list)));
      list->Close();
      ring_queue_push(&ret.lists, list);
    }
  
  
    return ret;
  }
  
  void
  destroy_cmd_list_allocator(CmdListAllocator* allocator)
  {
    destroy_fence(&allocator->fence);
  
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
  
    block_for_fence_value(&allocator->fence, cmd_allocator.fence_value);
  
    ring_queue_pop(&allocator->lists, &ret.d3d12_list);
  
    ret.d3d12_allocator = cmd_allocator.d3d12_allocator;
  
    ret.d3d12_allocator->Reset();
    ret.d3d12_list->Reset(ret.d3d12_allocator, nullptr);
  
    return ret;
  }
  
  FenceValue
  submit_cmd_lists(CmdListAllocator* allocator, Span<CmdList> lists, Option<Fence*> fence)
  {
    FenceValue ret = 0;
  
    USE_SCRATCH_ARENA();
    auto d3d12_cmd_lists = init_array<ID3D12CommandList*>(SCRATCH_ARENA_PASS, lists.size);

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
  get_d3d12_heap_type(GpuHeapType type)
  {
    switch(type)
    {
      case kGpuHeapTypeLocal:
        return D3D12_HEAP_TYPE_DEFAULT;
      case kGpuHeapTypeUpload:
        return D3D12_HEAP_TYPE_UPLOAD;
      default:
        UNREACHABLE;
    }
    return D3D12_HEAP_TYPE_DEFAULT;
  }
  
  GpuResourceHeap
  init_gpu_resource_heap(const GraphicsDevice* device, u64 size, GpuHeapType type)
  {
    D3D12_HEAP_DESC desc = {0};
    desc.SizeInBytes = size;
    desc.Properties = CD3DX12_HEAP_PROPERTIES(get_d3d12_heap_type(type));
  
    // TODO(Brandon): If we ever do MSAA textures then this needs to change.
    desc.Alignment = KiB(64);
    desc.Flags = D3D12_HEAP_FLAG_NONE;
  
    GpuResourceHeap ret = {0};
    ret.size = size;
    ret.type = type;
  
    HASSERT(device->d3d12->CreateHeap(&desc, IID_PPV_ARGS(&ret.d3d12_heap)));
  
    return ret;
  }
  
  void
  destroy_gpu_resource_heap(GpuResourceHeap* heap)
  {
    COM_RELEASE(heap->d3d12_heap);
    zero_memory(heap, sizeof(GpuResourceHeap));
  }
  
  GpuLinearAllocator
  init_gpu_linear_allocator(const GraphicsDevice* device, u64 size, GpuHeapType type)
  {
    GpuLinearAllocator ret = {0};
    ret.heap = init_gpu_resource_heap(device, size, type);
    ret.pos = 0;
    return ret;
  }
  
  void
  destroy_gpu_linear_allocator(GpuLinearAllocator* allocator)
  {
    destroy_gpu_resource_heap(&allocator->heap);
  }

  GraphicsDevice
  init_graphics_device(MEMORY_ARENA_PARAM)
  {
#ifdef DEBUG_LAYER
    ID3D12Debug* debug_interface = nullptr;
    HASSERT(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_interface)));
    debug_interface->EnableDebugLayer();
//    defer { COM_RELEASE(debug_interface); };
#endif
  
    GraphicsDevice res;
    IDXGIFactory7* factory = init_factory();
    defer { COM_RELEASE(factory); };
  
    IDXGIAdapter1* adapter; 
    GraphicsDevice ret = {0};
    init_d3d12_device(factory, &adapter, &ret.d3d12);
    defer { COM_RELEASE(adapter); };

    ret.graphics_queue = init_cmd_queue(&ret, kCmdQueueTypeGraphics);

    ret.graphics_cmd_allocator = init_cmd_list_allocator(MEMORY_ARENA_FWD,
                                                        &ret,
                                                        &ret.graphics_queue,
                                                        kFramesInFlight * 16);
    ret.compute_queue = init_cmd_queue(&ret, kCmdQueueTypeCompute);
    ret.compute_cmd_allocator = init_cmd_list_allocator(MEMORY_ARENA_FWD,
                                                        &ret,
                                                        &ret.compute_queue,
                                                        kFramesInFlight * 8);
    ret.copy_queue = init_cmd_queue(&ret, kCmdQueueTypeCopy);
    ret.copy_cmd_allocator = init_cmd_list_allocator(MEMORY_ARENA_FWD,
                                                    &ret,
                                                    &ret.copy_queue,
                                                    kFramesInFlight * 8);

    return ret;
  }
  
  void
  wait_for_device_idle(GraphicsDevice* device)
  {
    FenceValue value = cmd_queue_signal(&device->graphics_queue, &device->graphics_cmd_allocator.fence);
    block_for_fence_value(&device->graphics_cmd_allocator.fence, value);

    value = cmd_queue_signal(&device->compute_queue, &device->compute_cmd_allocator.fence);
    block_for_fence_value(&device->compute_cmd_allocator.fence, value);

    value = cmd_queue_signal(&device->copy_queue, &device->copy_cmd_allocator.fence);
    block_for_fence_value(&device->copy_cmd_allocator.fence, value);
  }
  
  void
  destroy_graphics_device(GraphicsDevice* device)
  {
    destroy_cmd_list_allocator(&device->graphics_cmd_allocator);
    destroy_cmd_list_allocator(&device->compute_cmd_allocator);
    destroy_cmd_list_allocator(&device->copy_cmd_allocator);
  
    destroy_cmd_queue(&device->graphics_queue);
    destroy_cmd_queue(&device->compute_queue);
    destroy_cmd_queue(&device->copy_queue);
  
    COM_RELEASE(device->d3d12);
    zero_memory(device, sizeof(GraphicsDevice));
  }
  
  bool
  is_depth_format(DXGI_FORMAT format)
  {
    return format == DXGI_FORMAT_D32_FLOAT ||
          format == DXGI_FORMAT_D16_UNORM ||
          format == DXGI_FORMAT_D24_UNORM_S8_UINT ||
          format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
  }

  static DXGI_FORMAT
  get_typeless_depth_format(DXGI_FORMAT format)
  {
    switch (format)
    {
      case DXGI_FORMAT_D32_FLOAT: return DXGI_FORMAT_R32_TYPELESS;
      case DXGI_FORMAT_D16_UNORM: return DXGI_FORMAT_R16_TYPELESS;
      default: UNREACHABLE; // TODO(Brandon): Implement
    }
  }
  
  GpuImage
  alloc_gpu_image_2D_no_heap(const GraphicsDevice* device, GpuImageDesc desc, const char* name)
  {
    GpuImage ret = {0};
    ret.desc = desc;
  
    D3D12_HEAP_PROPERTIES heap_props = CD3DX12_HEAP_PROPERTIES(get_d3d12_heap_type(kGpuHeapTypeLocal));
    D3D12_RESOURCE_DESC resource_desc;
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Format = desc.format;
    resource_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    resource_desc.Width = desc.width;
    resource_desc.Height = desc.height;
    resource_desc.DepthOrArraySize = MAX(desc.array_size, 1);
    resource_desc.MipLevels = 1;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resource_desc.Flags = desc.flags;
  
    D3D12_CLEAR_VALUE clear_value;
    D3D12_CLEAR_VALUE* p_clear_value = nullptr;
    clear_value.Format = desc.format;
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
  
    HASSERT(device->d3d12->CreateCommittedResource(&heap_props,
                                                  D3D12_HEAP_FLAG_NONE,
                                                  &resource_desc,
                                                  desc.initial_state,
                                                  p_clear_value,
                                                  IID_PPV_ARGS(&ret.d3d12_image)));
  
//    ret.d3d12_image->SetName(name);
    ret.d3d12_image->SetPrivateData(WKPDID_D3DDebugObjectName, (u32)strlen(name), name);
    ret.state = desc.initial_state;
  
    return ret;
  }
  
  void
  free_gpu_image(GpuImage* image)
  {
    COM_RELEASE(image->d3d12_image);
    zero_memory(image, sizeof(GpuImage));
  }
  
  GpuImage
  alloc_gpu_image_2D(const GraphicsDevice* device,
                    GpuLinearAllocator* allocator,
                    GpuImageDesc desc,
                    const char* name)
  {
    GpuImage ret = {0};
    ret.desc = desc;
  
    D3D12_RESOURCE_DESC resource_desc;
    resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    resource_desc.Format = desc.format;
    resource_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
    resource_desc.Width = desc.width;
    resource_desc.Height = desc.height;
    resource_desc.DepthOrArraySize = 1;
    resource_desc.MipLevels = 1;
    resource_desc.SampleDesc.Count = 1;
    resource_desc.SampleDesc.Quality = 0;
    resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    resource_desc.Flags = desc.flags;
  
    D3D12_CLEAR_VALUE clear_value;
    D3D12_CLEAR_VALUE* p_clear_value = nullptr;
    clear_value.Format = desc.format;
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

    D3D12_RESOURCE_ALLOCATION_INFO info = device->d3d12->GetResourceAllocationInfo(0, 1, &resource_desc);
  
    u64 new_pos = ALIGN_POW2(allocator->pos, info.Alignment) + info.SizeInBytes;
    ASSERT(new_pos <= allocator->heap.size);
  
    u64 allocation_offset = ALIGN_POW2(allocator->pos, info.Alignment);
    HASSERT(device->d3d12->CreatePlacedResource(allocator->heap.d3d12_heap,
                                                allocation_offset,
                                                &resource_desc,
                                                desc.initial_state,
                                                p_clear_value,
                                                IID_PPV_ARGS(&ret.d3d12_image)));
  
//    ret.d3d12_image->SetName(name);
    ret.state = desc.initial_state;
  
    allocator->pos = new_pos;
  
    return ret;
  }
  
  GpuBuffer
  alloc_gpu_buffer_no_heap(const GraphicsDevice* device,
                          GpuBufferDesc desc,
                          GpuHeapType type,
                          const char* name)
  {
    GpuBuffer ret = {0};
    ret.desc = desc;
  
    D3D12_HEAP_PROPERTIES heap_props = CD3DX12_HEAP_PROPERTIES(get_d3d12_heap_type(type));
    auto resource_desc = CD3DX12_RESOURCE_DESC::Buffer(desc.size, desc.flags);
  
    HASSERT(device->d3d12->CreateCommittedResource(&heap_props,
                                                  D3D12_HEAP_FLAG_NONE,
                                                  &resource_desc,
                                                  desc.initial_state,
                                                  nullptr,
                                                  IID_PPV_ARGS(&ret.d3d12_buffer)));
    ret.d3d12_buffer->SetPrivateData(WKPDID_D3DDebugObjectName, (u32)strlen(name), name);
    ret.gpu_addr = ret.d3d12_buffer->GetGPUVirtualAddress();
    if (type == kGpuHeapTypeUpload)
    {
      void* mapped = nullptr;
      ret.d3d12_buffer->Map(0, nullptr, &mapped);
      ret.mapped = mapped;
    }
    ret.state = desc.initial_state;
  
    return ret;
  }
  
  void
  free_gpu_buffer(GpuBuffer* buffer)
  {
    COM_RELEASE(buffer->d3d12_buffer);
    zero_memory(buffer, sizeof(GpuBuffer));
  }
  
  GpuBuffer
  alloc_gpu_buffer(const GraphicsDevice* device,
                  GpuLinearAllocator* allocator,
                  GpuBufferDesc desc,
                  const char* name)
  {
    // NOTE(Brandon): For simplicity's sake of CBVs, I just align all the sizes to 256.
    desc.size = ALIGN_POW2(desc.size, 256);

    u64 aligned_pos = ALIGN_POW2(allocator->pos, KiB(64));
    ASSERT(allocator->pos <= allocator->heap.size);
    allocator->pos = aligned_pos;
    u64 new_pos = allocator->pos + desc.size;
    ASSERT(new_pos <= allocator->heap.size);
  
    GpuBuffer ret = {0};
    ret.desc = desc;
  
    auto resource_desc = CD3DX12_RESOURCE_DESC::Buffer(desc.size, desc.flags);
  
    HASSERT(device->d3d12->CreatePlacedResource(allocator->heap.d3d12_heap,
                                                allocator->pos,
                                                &resource_desc,
                                                desc.initial_state,
                                                nullptr,
                                                IID_PPV_ARGS(&ret.d3d12_buffer)));
  
//    ret.d3d12_buffer->SetName(name);
    ret.gpu_addr = ret.d3d12_buffer->GetGPUVirtualAddress();
    if (allocator->heap.type == kGpuHeapTypeUpload)
    {
      void* mapped = nullptr;
      ret.d3d12_buffer->Map(0, nullptr, &mapped);
      ret.mapped = mapped;
    }
  
    allocator->pos = new_pos;

    ret.state = desc.initial_state;
  
    return ret;
  }
  
  GpuUploadRingBuffer
  alloc_gpu_ring_buffer(MEMORY_ARENA_PARAM, const GraphicsDevice* device, u64 size)
  {
    ASSERT(is_pow2(size));
    ASSERT(size > 0);
    GpuUploadRingBuffer ret = {0};
  
    GpuBufferDesc desc = {0};
    desc.size = size;
  
    // TODO(Brandon): Maybe flag deny shader resource?
    desc.flags = D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
  
    ret.gpu_buffer = alloc_gpu_buffer_no_heap(device, desc, kGpuHeapTypeUpload, "GPU Upload Ring Buffer");
    ret.size = size;
    ret.read = 0;
    ret.write = 0;
    ret.upload_fence_values = init_ring_queue<GpuUploadRange>(MEMORY_ARENA_FWD, size / KiB(1));
    ret.fence = init_fence(device);
    ret.cpu_event = CreateEventW(NULL, FALSE, FALSE, NULL);
  
    return ret;
  }
  
  void
  free_gpu_ring_buffer(GpuUploadRingBuffer* ring_buffer)
  {
    CloseHandle(ring_buffer->cpu_event);
    destroy_fence(&ring_buffer->fence);
    free_gpu_buffer(&ring_buffer->gpu_buffer);
  }
  
  static u64
  gpu_ring_buffer_size(GpuUploadRingBuffer* ring_buffer)
  {
    return ring_buffer->write - ring_buffer->read;
  }
  
  static u64
  gpu_ring_buffer_remaining_size(GpuUploadRingBuffer* ring_buffer)
  {
    return ring_buffer->size - gpu_ring_buffer_size(ring_buffer);
  }
  
  static bool
  gpu_ring_buffer_is_full(GpuUploadRingBuffer* ring_buffer)
  {
    return gpu_ring_buffer_size(ring_buffer) == ring_buffer->size;
  }
  
  static bool
  gpu_ring_buffer_is_empty(GpuUploadRingBuffer* ring_buffer)
  {
    return ring_buffer->read == ring_buffer->write;
  }
  
  static u64
  gpu_ring_buffer_mask(GpuUploadRingBuffer* ring_buffer, u64 val)
  {
    return (val & (ring_buffer->size - 1));
  }
  
  FenceValue
  yield_gpu_upload_buffer(CmdListAllocator* cmd_allocator,
                          GpuUploadRingBuffer* ring_buffer,
                          GpuBuffer* dst,
                          u64 dst_offset,
                          const void* src,
                          u64 size,
                          u64 alignment)
  {
    // TODO(Brandon): This whole fucking thing is broken.
    UNREACHABLE;
    void* mapped = unwrap(ring_buffer->gpu_buffer.mapped);
    u64 write_masked = ring_buffer->write & (ring_buffer->size - 1);
    u64 write_addr = reinterpret_cast<u64>(mapped) + write_masked;
    u64 aligned_addr = align_address(write_addr, alignment);
    u64 aligned_diff = aligned_addr - write_addr;
    u64 total_allocation_size = aligned_diff + size;
    ASSERT(total_allocation_size <= ring_buffer->size);
    while (gpu_ring_buffer_remaining_size(ring_buffer) < total_allocation_size)
    {
      block_for_fence_value(&ring_buffer->fence, ring_buffer->fence.last_completed_value);
      GpuUploadRange range = {0};
      ring_queue_peak_front(ring_buffer->upload_fence_values, &range);
  
      if (is_fence_complete(&ring_buffer->fence, range.fence_value))
      {
        ring_queue_pop(&ring_buffer->upload_fence_values, &range);
        ring_buffer->read += range.size;
        break;
      }
    }
  
    ring_buffer->write += total_allocation_size;
  
    memcpy(reinterpret_cast<void*>(aligned_addr), src, size);
  
    auto cmd = alloc_cmd_list(cmd_allocator);
  
    cmd.d3d12_list->CopyBufferRegion(dst->d3d12_buffer,
                                    dst_offset,
                                    ring_buffer->gpu_buffer.d3d12_buffer, 
                                    aligned_addr - reinterpret_cast<u64>(mapped), 
                                    size);

    FenceValue fence_value = submit_cmd_lists(cmd_allocator, {cmd}, &ring_buffer->fence);
  
    GpuUploadRange upload_range = {0};
    upload_range.size = total_allocation_size;
    upload_range.fence_value = fence_value;
  
    ring_queue_push(&ring_buffer->upload_fence_values, upload_range);
    return fence_value;
  }
  
  static D3D12_DESCRIPTOR_HEAP_TYPE
  get_d3d12_descriptor_type(DescriptorHeapType type)
  {
    switch(type)
    {
      case kDescriptorHeapTypeCbvSrvUav:
        return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
      case kDescriptorHeapTypeSampler:
        return D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
      case kDescriptorHeapTypeRtv:
        return D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
      case kDescriptorHeapTypeDsv:
        return D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
      default:
        UNREACHABLE;
    }
    return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
  }
  
  static bool
  descriptor_type_is_shader_visible(DescriptorHeapType type)
  {
    return type == kDescriptorHeapTypeCbvSrvUav || type == kDescriptorHeapTypeSampler;
  }

  static ID3D12DescriptorHeap*
  init_d3d12_descriptor_heap(const GraphicsDevice* device, u32 size, DescriptorHeapType type)
  {
    D3D12_DESCRIPTOR_HEAP_DESC desc;
    desc.Type = get_d3d12_descriptor_type(type);
    desc.NumDescriptors = size;
    desc.NodeMask = 1;
    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
  
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
  init_descriptor_pool(MEMORY_ARENA_PARAM, const GraphicsDevice* device, u32 size, DescriptorHeapType type)
  {
    DescriptorPool ret = {0};
    ret.num_descriptors = size;
    ret.type = type;
    ret.free_descriptors = init_ring_queue<u32>(MEMORY_ARENA_FWD, size);
    ret.d3d12_heap = init_d3d12_descriptor_heap(device, size, type);
  
    ret.descriptor_size = device->d3d12->GetDescriptorHandleIncrementSize(get_d3d12_descriptor_type(type));
    ret.cpu_start = ret.d3d12_heap->GetCPUDescriptorHandleForHeapStart();
    if (descriptor_type_is_shader_visible(type))
    {
      ret.gpu_start = ret.d3d12_heap->GetGPUDescriptorHandleForHeapStart();
    }
  
    for (u32 i = 0; i < size; i++)
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
  init_descriptor_linear_allocator(const GraphicsDevice* device,
                                   u32 size,
                                   DescriptorHeapType type)
  {
    DescriptorLinearAllocator ret = {0};
    ret.pos = 0;
    ret.num_descriptors = size;
    ret.type = type;
    ret.d3d12_heap = init_d3d12_descriptor_heap(device, size, type);

    ret.descriptor_size = device->d3d12->GetDescriptorHandleIncrementSize(get_d3d12_descriptor_type(type));
    ret.cpu_start = ret.d3d12_heap->GetCPUDescriptorHandleForHeapStart();
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
  
  Descriptor
  alloc_descriptor(DescriptorPool* pool)
  {
    u32 index = 0;
    ring_queue_pop(&pool->free_descriptors, &index);
    u64 offset = index * pool->descriptor_size;
  
    Descriptor ret = {0};
    ret.cpu_handle.ptr = pool->cpu_start.ptr + offset;
    ret.gpu_handle = None;
    ret.index = index;
  
    if (pool->gpu_start)
    {
      ret.gpu_handle = D3D12_GPU_DESCRIPTOR_HANDLE{unwrap(pool->gpu_start).ptr + offset};
    }
  
    ret.type = pool->type;
  
    return ret;
  }
  
  void
  free_descriptor(DescriptorPool* pool, Descriptor* descriptor)
  {
    ASSERT(descriptor->cpu_handle.ptr >= pool->cpu_start.ptr);
    ASSERT(descriptor->index < pool->num_descriptors);
    ring_queue_push(&pool->free_descriptors, descriptor->index);
    zero_memory(descriptor, sizeof(Descriptor));
  }

  Descriptor
  alloc_descriptor(DescriptorLinearAllocator* allocator)
  {
    Descriptor ret = {0};
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
  init_rtv(const GraphicsDevice* device, Descriptor* descriptor, const GpuImage* image)
  {
    ASSERT(descriptor->type == kDescriptorHeapTypeRtv);
  
    device->d3d12->CreateRenderTargetView(image->d3d12_image,
                                          nullptr,
                                          descriptor->cpu_handle);
  
  }
  
  void
  init_dsv(const GraphicsDevice* device, Descriptor* descriptor, const GpuImage* image)
  {
    ASSERT(descriptor->type == kDescriptorHeapTypeDsv);
  
    D3D12_DEPTH_STENCIL_VIEW_DESC desc;
    desc.Texture2D.MipSlice = 0;
    desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
    desc.Flags = D3D12_DSV_FLAG_NONE;
    desc.Format = image->desc.format;
    device->d3d12->CreateDepthStencilView(image->d3d12_image,
                                          &desc,
                                          descriptor->cpu_handle);
  }
  
  void
  init_buffer_srv(const GraphicsDevice* device,
                  Descriptor* descriptor,
                  const GpuBuffer* buffer,
                  u32 first_element,
                  u32 num_elements,
                  u32 stride)
  {
    ASSERT(descriptor->type == kDescriptorHeapTypeCbvSrvUav);
    D3D12_SHADER_RESOURCE_VIEW_DESC desc = {0};
    desc.Buffer.FirstElement = first_element;
    desc.Buffer.NumElements = num_elements;
    desc.Buffer.StructureByteStride = stride;
    desc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  
    device->d3d12->CreateShaderResourceView(buffer->d3d12_buffer, &desc, descriptor->cpu_handle);
  }
  
  static bool
  buffer_is_aligned(const GpuBuffer* buffer, u64 alignment, u64 offset = 0)
  {
    return (buffer->gpu_addr + offset) % alignment == 0;
  }
  
  void
  init_buffer_cbv(const GraphicsDevice* device,
                  Descriptor* descriptor,
                  const GpuBuffer* buffer,
                  u64 offset,
                  u32 size)
  {
    ASSERT(descriptor->type == kDescriptorHeapTypeCbvSrvUav);
    ASSERT(buffer_is_aligned(buffer, 256, offset));
  
    D3D12_CONSTANT_BUFFER_VIEW_DESC desc = {0};
    desc.BufferLocation = buffer->gpu_addr + offset;
    desc.SizeInBytes = size;
    device->d3d12->CreateConstantBufferView(&desc, descriptor->cpu_handle);
  }
  
  void
  init_buffer_uav(const GraphicsDevice* device,
                  Descriptor* descriptor,
                  const GpuBuffer* buffer,
                  u32 first_element,
                  u32 num_elements,
                  u32 stride)
  {
    ASSERT(descriptor->type == kDescriptorHeapTypeCbvSrvUav);

    D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {0};
    desc.Buffer.FirstElement = first_element;
    desc.Buffer.NumElements = num_elements;
    desc.Buffer.StructureByteStride = stride;
    desc.Buffer.CounterOffsetInBytes = 0;
    desc.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_NONE;
    desc.Format = DXGI_FORMAT_UNKNOWN;
    desc.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    device->d3d12->CreateUnorderedAccessView(buffer->d3d12_buffer, nullptr, &desc, descriptor->cpu_handle);
  }
  
  void
  init_image_2D_srv(const GraphicsDevice* device, Descriptor* descriptor, const GpuImage* image)
  {
    ASSERT(descriptor->type == kDescriptorHeapTypeCbvSrvUav);

    D3D12_SHADER_RESOURCE_VIEW_DESC desc = {0};
    desc.Texture2D.MipLevels = 1;
    if (is_depth_format(image->desc.format))
    {
      desc.Format = (DXGI_FORMAT)((u32)image->desc.format + 1);
    }
    else
    {
      desc.Format = image->desc.format;
    }
    desc.ViewDimension = image->desc.array_size > 1 ? D3D12_SRV_DIMENSION_TEXTURE2DARRAY : D3D12_SRV_DIMENSION_TEXTURE2D;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    if (image->desc.array_size > 1)
    {
      desc.Texture2DArray.MostDetailedMip = 0;
      desc.Texture2DArray.MipLevels = 1;
      desc.Texture2DArray.FirstArraySlice = 0;
      desc.Texture2DArray.ArraySize = image->desc.array_size;
    }
  
    device->d3d12->CreateShaderResourceView(image->d3d12_image, &desc, descriptor->cpu_handle);   }
  
  void
  init_image_2D_uav(const GraphicsDevice* device, Descriptor* descriptor, const GpuImage* image)
  {
    ASSERT(descriptor->type == kDescriptorHeapTypeCbvSrvUav);

    D3D12_UNORDERED_ACCESS_VIEW_DESC desc = {0};
    desc.Format = image->desc.format;
    desc.ViewDimension = image->desc.array_size > 1 ? D3D12_UAV_DIMENSION_TEXTURE2DARRAY : D3D12_UAV_DIMENSION_TEXTURE2D;
    if (image->desc.array_size > 1)
    {
      desc.Texture2DArray.MipSlice = 0;
      desc.Texture2DArray.FirstArraySlice = 0;
      desc.Texture2DArray.ArraySize = image->desc.array_size;
      desc.Texture2DArray.PlaneSlice = 0;
    }
    device->d3d12->CreateUnorderedAccessView(image->d3d12_image, nullptr, &desc, descriptor->cpu_handle);
  }

  void
  init_sampler(const GraphicsDevice* device, Descriptor* descriptor)
  {
    ASSERT(descriptor->type == kDescriptorHeapTypeSampler);

    D3D12_SAMPLER_DESC desc;
    // TODO(Brandon): Don't hardcode these.
    desc.Filter = D3D12_FILTER_MIN_MAG_MIP_POINT;
    desc.AddressU = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    desc.AddressV = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    desc.AddressW = D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    desc.MipLODBias = 0.0f;
    desc.MinLOD = 0.0f;
    desc.MaxLOD = 100.0f;
    device->d3d12->CreateSampler(&desc, descriptor->cpu_handle);
  }

  void
  init_bvh_srv(const GraphicsDevice* device, Descriptor* descriptor, const GpuBvh* bvh)
  {
    ASSERT(descriptor->type == kDescriptorHeapTypeCbvSrvUav);

    D3D12_SHADER_RESOURCE_VIEW_DESC desc = {0};
    desc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
    desc.RaytracingAccelerationStructure.Location = bvh->top_bvh.gpu_addr;
    desc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  
    device->d3d12->CreateShaderResourceView(nullptr, &desc, descriptor->cpu_handle);
  }
  
  static ID3D12RootSignature* g_root_signature = nullptr;
  
  GpuShader
  load_shader_from_file(const GraphicsDevice* device, const wchar_t* path)
  {
    GpuShader ret = {0};
    HASSERT(D3DReadFileToBlob(path, &ret.d3d12_shader));
    if (g_root_signature == nullptr)
    {
      ID3DBlob* root_signature_blob = nullptr;
      defer { COM_RELEASE(root_signature_blob); };
  
      HASSERT(D3DGetBlobPart(ret.d3d12_shader->GetBufferPointer(),
                            ret.d3d12_shader->GetBufferSize(),
                            D3D_BLOB_ROOT_SIGNATURE, 0,
                            &root_signature_blob));
      device->d3d12->CreateRootSignature(0,
                                        root_signature_blob->GetBufferPointer(),
                                        root_signature_blob->GetBufferSize(),
                                        IID_PPV_ARGS(&g_root_signature));
    }
    return ret;
  }
  
  void
  destroy_shader(GpuShader* shader)
  {
    COM_RELEASE(shader->d3d12_shader);
  }
  
  GraphicsPSO
  init_graphics_pipeline(const GraphicsDevice* device,
                        GraphicsPipelineDesc desc,
                        const char* name)
  {
    GraphicsPSO ret = {0};
  
    D3D12_RENDER_TARGET_BLEND_DESC render_target_blend_desc;
    render_target_blend_desc.BlendEnable = FALSE;
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
    depth_stencil_desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
    depth_stencil_desc.DepthFunc = desc.comparison_func;
    depth_stencil_desc.StencilEnable = desc.stencil_enable;
    depth_stencil_desc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
    depth_stencil_desc.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;
  
  
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc = {0};
    pso_desc.VS = CD3DX12_SHADER_BYTECODE(desc.vertex_shader.d3d12_shader);
    pso_desc.PS = CD3DX12_SHADER_BYTECODE(desc.pixel_shader.d3d12_shader);
    pso_desc.BlendState = blend_desc,
    pso_desc.SampleMask = UINT32_MAX,
    pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT),
    pso_desc.DepthStencilState = depth_stencil_desc,
    pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
    pso_desc.NumRenderTargets = static_cast<u32>(desc.rtv_formats.size);
  
    pso_desc.DSVFormat = desc.dsv_format;
  
    pso_desc.SampleDesc.Count = 1;
    pso_desc.SampleDesc.Quality = 0;
    pso_desc.NodeMask = 0;
  
    pso_desc.RasterizerState.FrontCounterClockwise = false;
    pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
  
    for (u32 i = 0; i < desc.rtv_formats.size; i++)
    {
      pso_desc.RTVFormats[i] = desc.rtv_formats[i];
    }
  
    HASSERT(device->d3d12->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&ret.d3d12_pso)));
  
//    ret.d3d12_pso->SetName(name);
  
    return ret;
  }
  
  void
  destroy_graphics_pipeline(GraphicsPSO* pipeline)
  {
    COM_RELEASE(pipeline->d3d12_pso);
    zero_memory(pipeline, sizeof(GraphicsPSO));
  }

  ComputePSO
  init_compute_pipeline(const GraphicsDevice* device, GpuShader compute_shader, const char* name)
  {
    ComputePSO ret;
    D3D12_COMPUTE_PIPELINE_STATE_DESC desc;
    desc.pRootSignature = g_root_signature;
    desc.CS = CD3DX12_SHADER_BYTECODE(compute_shader.d3d12_shader);
    desc.NodeMask = 0;
    desc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
    desc.CachedPSO.pCachedBlob = nullptr;
    desc.CachedPSO.CachedBlobSizeInBytes = 0;
    HASSERT(device->d3d12->CreateComputePipelineState(&desc, IID_PPV_ARGS(&ret.d3d12_pso)));

    return ret;
  }

  void
  destroy_compute_pipeline(ComputePSO* pipeline)
  {
    COM_RELEASE(pipeline->d3d12_pso);
    zero_memory(pipeline, sizeof(ComputePSO));
  }

  GpuBvh
  init_acceleration_structure(GraphicsDevice* device,
                              const GpuBuffer& vertex_uber_buffer,
                              u32 vertex_count,
                              u32 vertex_stride,
                              const GpuBuffer& index_uber_buffer,
                              u32 index_count,
                              const char* name)
  {
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
		bottom_level_prebuild_info.ScratchDataSizeInBytes = ALIGN_POW2(bottom_level_prebuild_info.ScratchDataSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
		bottom_level_prebuild_info.ResultDataMaxSizeInBytes = ALIGN_POW2(bottom_level_prebuild_info.ResultDataMaxSizeInBytes, D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT);
    ASSERT(bottom_level_prebuild_info.ResultDataMaxSizeInBytes > 0);
  
    ret.bottom_bvh          = alloc_gpu_buffer_no_heap(device,
                                                      {.size = bottom_level_prebuild_info.ResultDataMaxSizeInBytes,
                                                        .flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                                        .initial_state = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE},
                                                      kGpuHeapTypeLocal,
                                                      "Bottom BVH Buffer");

    D3D12_RAYTRACING_INSTANCE_DESC instance_desc = {};
    instance_desc.Transform[0][0] = 1;
    instance_desc.Transform[1][1] = 1;
    instance_desc.Transform[2][2] = 1;
		instance_desc.InstanceID = 0;
    instance_desc.InstanceMask = 0xFF;
    instance_desc.InstanceContributionToHitGroupIndex = 0;
		instance_desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE;
    instance_desc.AccelerationStructure = ret.bottom_bvh.gpu_addr;
    ret.instance_desc_buffer = alloc_gpu_buffer_no_heap(device,
                                                        {.size = ALIGN_POW2(sizeof(instance_desc), D3D12_RAYTRACING_INSTANCE_DESCS_BYTE_ALIGNMENT)},
                                                        kGpuHeapTypeUpload, "Instance Desc");
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

    ret.top_bvh             = alloc_gpu_buffer_no_heap(device,
                                                      {.size = top_level_prebuild_info.ResultDataMaxSizeInBytes,
                                                        .flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                                        .initial_state = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE},
                                                      kGpuHeapTypeLocal,
                                                      "Top BVH Buffer");
  
    GpuBuffer top_level_scratch = alloc_gpu_buffer_no_heap(device,
                                                           {.size = top_level_prebuild_info.ScratchDataSizeInBytes,
                                                            .flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS},
                                                           kGpuHeapTypeLocal,
                                                           "BVH Scratch Buffer");

    GpuBuffer bottom_level_scratch = alloc_gpu_buffer_no_heap(device,
                                                           {.size = bottom_level_prebuild_info.ScratchDataSizeInBytes,
                                                            .flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS},
                                                           kGpuHeapTypeLocal,
                                                           "BVH Scratch Buffer");
//    defer 
//		{
//			free_gpu_buffer(&top_level_scratch); 
//			free_gpu_buffer(&bottom_level_scratch);
//		};
  
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
//    Fence fence = init_fence(device);
//    defer { destroy_fence(&fence); };
  
    submit_cmd_lists(&device->graphics_cmd_allocator, {cmd_list});
  
    wait_for_device_idle(device);
//    block_for_fence_value(&fence, fence_value);

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
  init_ray_tracing_pipeline(const GraphicsDevice* device, GpuShader ray_tracing_library, const char* name)
  {
    RayTracingPSO ret;

    CD3DX12_STATE_OBJECT_DESC desc = {D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE};

    auto* library = desc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    auto shader_byte_code = CD3DX12_SHADER_BYTECODE(ray_tracing_library.d3d12_shader);
    library->SetDXILLibrary(&shader_byte_code);
    HASSERT(device->d3d12->CreateStateObject(desc, IID_PPV_ARGS(&ret.d3d12_pso)));

    HASSERT(ret.d3d12_pso->QueryInterface(IID_PPV_ARGS(&ret.d3d12_properties)));

    return ret;
  }

  void
  destroy_ray_tracing_pipeline(RayTracingPSO* pipeline)
  {
    COM_RELEASE(pipeline->d3d12_pso);
    zero_memory(pipeline, sizeof(RayTracingPSO));
  }

  ShaderTable
  init_shader_table(const GraphicsDevice* device, RayTracingPSO pipeline, const char* name)
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
    ret.buffer = alloc_gpu_buffer_no_heap(device, desc, kGpuHeapTypeUpload, name);

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
  alloc_back_buffers_from_swap_chain(const SwapChain* swap_chain,
                                    GpuImage** back_buffers,
                                    u32 num_back_buffers)
  {
    GpuImageDesc desc = {0};
    desc.width = swap_chain->width;
    desc.height = swap_chain->height;
    desc.format = swap_chain->format;
    desc.initial_state = D3D12_RESOURCE_STATE_PRESENT;
    for (u32 i = 0; i < num_back_buffers; i++)
    {
      HASSERT(swap_chain->d3d12_swap_chain->GetBuffer(i, IID_PPV_ARGS(&back_buffers[i]->d3d12_image)));
      back_buffers[i]->desc = desc;
    }
  }
  
  
  SwapChain
  init_swap_chain(MEMORY_ARENA_PARAM, HWND window, const GraphicsDevice* device)
  {
    auto* factory = init_factory();
    defer { COM_RELEASE(factory); };
  
    SwapChain ret = {0};
  
    RECT client_rect;
    GetClientRect(window, &client_rect);
    ret.width = client_rect.right - client_rect.left;
    ret.height = client_rect.bottom - client_rect.top;
    ret.format = DXGI_FORMAT_R8G8B8A8_UNORM;
    ret.tearing_supported = check_tearing_support(factory);
    ret.vsync = true;
  
  
    DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = { 0 };
    swap_chain_desc.Width = ret.width;
    swap_chain_desc.Height = ret.height;
    swap_chain_desc.Format = ret.format;
    swap_chain_desc.Stereo = FALSE;
    swap_chain_desc.SampleDesc = { 1, 0 };
    swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    swap_chain_desc.BufferCount = ARRAY_LENGTH(ret.back_buffers);
    swap_chain_desc.Scaling = DXGI_SCALING_STRETCH;
    swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
    swap_chain_desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
    swap_chain_desc.Flags = 0;  //ret.tearing_supported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;
  
    IDXGISwapChain1* swap_chain1 = nullptr;
    HASSERT(factory->CreateSwapChainForHwnd(device->graphics_queue.d3d12_queue,
                                            window,
                                            &swap_chain_desc,
                                            nullptr,
                                            nullptr,
                                            &swap_chain1));
  
    HASSERT(factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER));
    HASSERT(swap_chain1->QueryInterface(IID_PPV_ARGS(&ret.d3d12_swap_chain)));
    COM_RELEASE(swap_chain1);
  
    ret.fence = init_fence(device);
    zero_memory(ret.frame_fence_values, sizeof(ret.frame_fence_values));
  
    for (u32 i = 0; i < ARRAY_LENGTH(ret.back_buffers); i++)
    {
      ret.back_buffers[i] = push_memory_arena<GpuImage>(MEMORY_ARENA_FWD);
    }
  //  ret.depth_buffer = push_memory_arena<GpuImage>(MEMORY_ARENA_FWD);
  
    alloc_back_buffers_from_swap_chain(&ret,
                                      ret.back_buffers,
                                      ARRAY_LENGTH(ret.back_buffers));
    ret.back_buffer_index = 0;
  
  //  GpuImageDesc desc = {0};
  //  desc.width = ret.width;
  //  desc.height = ret.height;
  //  desc.format = DXGI_FORMAT_D32_FLOAT;
  //  desc.initial_state = D3D12_RESOURCE_STATE_DEPTH_WRITE;
  //  D3D12_CLEAR_VALUE depth_clear_value;
  //  depth_clear_value.Format = desc.format;
  //  depth_clear_value.DepthStencil.Depth = 0.0f;
  //  depth_clear_value.DepthStencil.Stencil = 0;
  //  desc.clear_value = depth_clear_value;
  //  *ret.depth_buffer = alloc_gpu_image_2D_no_heap(device, desc, L"SwapChain Depth Buffer");
  
  //  ret.render_target_view_heap = init_descriptor_heap(MEMORY_ARENA_FWD,
  //                                                     device,
  //                                                     ARRAY_LENGTH(ret.back_buffers),
  //                                                     kDescriptorHeapTypeRtv);
  //  ret.depth_stencil_view_heap = init_descriptor_heap(MEMORY_ARENA_FWD,
  //                                                     device,
  //                                                     1,
  //                                                     kDescriptorHeapTypeDsv);
  
  //  for (u32 i = 0; i < ARRAY_LENGTH(ret.back_buffers); i++)
  //  {
  //    ret.back_buffer_views[i] = alloc_rtv(device, &ret.render_target_view_heap, ret.back_buffers[i]);
  //  }
  
  //  ret.depth_stencil_view = alloc_dsv(device, &ret.depth_stencil_view_heap, ret.depth_buffer);
  
    return ret;
  }
  
  void
  destroy_swap_chain(SwapChain* swap_chain)
  {
  //  destroy_descriptor_heap(&swap_chain->depth_stencil_view_heap);
  //  destroy_descriptor_heap(&swap_chain->render_target_view_heap);
  
  //  free_gpu_image(swap_chain->depth_buffer);
    for (auto* image : swap_chain->back_buffers)
    {
      free_gpu_image(image);
    }
    destroy_fence(&swap_chain->fence);
    COM_RELEASE(swap_chain->d3d12_swap_chain);
  }
  
  
  const GpuImage*
  swap_chain_acquire(SwapChain* swap_chain)
  {
    u32 index = swap_chain->back_buffer_index;
    block_for_fence_value(&swap_chain->fence,
                          swap_chain->frame_fence_values[index]);
  
    return swap_chain->back_buffers[index];
  }
  
  void
  swap_chain_submit(SwapChain* swap_chain, const GraphicsDevice* device, const GpuImage* rtv)
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
  cmd_set_descriptor_heaps(CmdList* cmd, const DescriptorPool* heaps, u32 num_heaps)
  {
    USE_SCRATCH_ARENA();
    auto d3d12_heaps = init_array<ID3D12DescriptorHeap*>(SCRATCH_ARENA_PASS, num_heaps);
    for (u32 i = 0; i < num_heaps; i++)
    {
      *array_add(&d3d12_heaps) = heaps[i].d3d12_heap;
    }
  
    cmd->d3d12_list->SetDescriptorHeaps(num_heaps, &d3d12_heaps[0]);
  }

  void
  cmd_set_descriptor_heaps(CmdList* cmd, Span<const DescriptorLinearAllocator*> heaps)
  {
    USE_SCRATCH_ARENA();
    auto d3d12_heaps = init_array<ID3D12DescriptorHeap*>(SCRATCH_ARENA_PASS, heaps.size);
    for (u32 i = 0; i < heaps.size; i++)
    {
      *array_add(&d3d12_heaps) = heaps[i]->d3d12_heap;
    }
  
    cmd->d3d12_list->SetDescriptorHeaps(static_cast<u32>(heaps.size), &d3d12_heaps[0]);
  }
  
  void
  cmd_set_graphics_root_signature(CmdList* cmd)
  {
    ASSERT(g_root_signature != nullptr);
    cmd->d3d12_list->SetGraphicsRootSignature(g_root_signature);
  }

  void
  cmd_set_compute_root_signature(CmdList* cmd)
  {
    ASSERT(g_root_signature != nullptr);
    cmd->d3d12_list->SetComputeRootSignature(g_root_signature);
  }
  
  void
  cmd_set_primitive_topology(CmdList* cmd, D3D_PRIMITIVE_TOPOLOGY topology)
  {
    cmd->d3d12_list->IASetPrimitiveTopology(topology);
  }
  
  void
  init_imgui_ctx(const GraphicsDevice* device,
                 const SwapChain* swap_chain,
                 HWND window,
                 DescriptorLinearAllocator* cbv_srv_uav_heap)
  {
    ASSERT(cbv_srv_uav_heap->type == kDescriptorHeapTypeCbvSrvUav);
  
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO* io = &ImGui::GetIO();
    io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  
    ImGui::StyleColorsDark();
  
    Descriptor descriptor = alloc_descriptor(cbv_srv_uav_heap);
  
    ImGui_ImplWin32_Init(window);
    ImGui_ImplDX12_Init(device->d3d12,
                        kFramesInFlight,
                        swap_chain->format,
                        cbv_srv_uav_heap->d3d12_heap,
                        descriptor.cpu_handle,
                        unwrap(descriptor.gpu_handle));
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
  cmd_imgui_render(CmdList* cmd)
  {
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), cmd->d3d12_list);
  }
}

