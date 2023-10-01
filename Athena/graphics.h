#pragma once
#include "array.h"
#include "ring_buffer.h"
#include "hash_table.h"
#include "types.h"
#include "math/math.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

namespace gfx
{
  typedef Vec4 Rgba;
  typedef Vec3 Rgb;
  
  constant u8 kFramesInFlight = 2;
  constant u8 kMaxCommandListThreads = 8;
  constant u8 kCommandAllocators = kFramesInFlight * kMaxCommandListThreads;
  
  struct GraphicsDevice;
  typedef u64 FenceValue;
  struct Fence
  {
    ID3D12Fence* d3d12_fence = nullptr;
    FenceValue value = 0;
    FenceValue last_completed_value = 0;
    HANDLE cpu_event = nullptr;
    bool already_waiting = false;
  };
  
  Fence init_fence(const GraphicsDevice* device);
  void destroy_fence(Fence* fence);
  void yield_for_fence_value(Fence* fence, FenceValue value);
  void block_for_fence_value(Fence* fence, FenceValue value);
  
  enum CmdQueueType : u8
  {
    kCmdQueueTypeGraphics,
    kCmdQueueTypeCompute,
    kCmdQueueTypeCopy,
  
    kCmdQueueTypeCount,
  };
  
  struct CmdQueue
  {
    ID3D12CommandQueue* d3d12_queue = nullptr;
    CmdQueueType type = kCmdQueueTypeGraphics;
  };
  
  CmdQueue init_cmd_queue(const GraphicsDevice* device, CmdQueueType type);
  void destroy_cmd_queue(const CmdQueue* queue);
  
  void cmd_queue_gpu_wait_for_fence(const CmdQueue* queue, Fence* fence, FenceValue value);
  FenceValue cmd_queue_signal(const CmdQueue* queue, Fence* fence);
  
  struct CmdAllocator
  {
    ID3D12CommandAllocator* d3d12_allocator = 0;
    FenceValue fence_value = 0;
  };
  
  struct CmdListAllocator
  {
    ID3D12CommandQueue* d3d12_queue = nullptr;
  
    RingQueue<CmdAllocator> allocators;
    RingQueue<ID3D12GraphicsCommandList4*> lists;
    Fence fence;
  };
  
  struct CmdList
  {
    ID3D12GraphicsCommandList4* d3d12_list = nullptr;
    ID3D12CommandAllocator* d3d12_allocator = nullptr;
  };
  
  CmdListAllocator init_cmd_list_allocator(MEMORY_ARENA_PARAM,
                                          const GraphicsDevice* device,
                                          const CmdQueue* queue,
                                          u16 pool_size);
  void destroy_cmd_list_allocator(CmdListAllocator* allocator);
  CmdList alloc_cmd_list(CmdListAllocator* allocator);
  FenceValue submit_cmd_lists(CmdListAllocator* allocator,
                              Span<CmdList> lists,
                              Option<Fence*> fence = None);
  
  struct GraphicsDevice
  {
    ID3D12Device6* d3d12 = nullptr;
    CmdQueue graphics_queue;
    CmdListAllocator graphics_cmd_allocator;
    CmdQueue compute_queue;
    CmdListAllocator compute_cmd_allocator;
    CmdQueue copy_queue;
    CmdListAllocator copy_cmd_allocator;
  };
  
  GraphicsDevice init_graphics_device(MEMORY_ARENA_PARAM);
  void destroy_graphics_device(GraphicsDevice* device);

  void wait_for_device_idle(GraphicsDevice* device);
  
  enum GpuHeapType : u8
  {
    // GPU only
    kGpuHeapTypeLocal,
    // CPU to GPU
    kGpuHeapTypeUpload,
  
    kGpuHeapTypeCount,
  };
  
  struct GpuResourceHeap
  {
    ID3D12Heap* d3d12_heap = nullptr;
    u64 size = 0;
    GpuHeapType type = kGpuHeapTypeLocal;
  };
  
  GpuResourceHeap init_gpu_resource_heap(const GraphicsDevice* device,
                                        u64 size,
                                        GpuHeapType type);
  void destroy_gpu_resource_heap(GpuResourceHeap* heap);
  
  struct GpuLinearAllocator
  {
    GpuResourceHeap heap;
    u64 pos = 0;
  };
  
  GpuLinearAllocator init_gpu_linear_allocator(const GraphicsDevice* device,
                                              u64 size,
                                              GpuHeapType type);
  void destroy_gpu_linear_allocator(GpuLinearAllocator* allocator);
  inline void
  reset_gpu_linear_allocator(GpuLinearAllocator* allocator)
  {
    allocator->pos = 0;
  }
  
  
  struct GpuImageDesc
  {
    u32 width = 0;
    u32 height = 0;
    u32 array_size = 1;
  
    // TODO(Brandon): Eventually make these less verbose and platform agnostic.
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COMMON;
  
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
    union
    {
      Vec4 color_clear_value;
      struct
      {
        f32 depth_clear_value;
        s8 stencil_clear_value;
      };
    };
  };
  
  struct GpuImage
  {
    GpuImageDesc desc;
    ID3D12Resource* d3d12_image = nullptr;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
  };
  
  GpuImage alloc_gpu_image_2D_no_heap(const GraphicsDevice* device,
                                      GpuImageDesc desc,
                                      const char* name);
  void free_gpu_image(GpuImage* image);
  
  GpuImage alloc_gpu_image_2D(const GraphicsDevice* device,
                              GpuLinearAllocator* allocator,
                              GpuImageDesc desc,
                              const char* name);

  void     upload_gpu_image_2D(const GraphicsDevice* device,
                               const void* rgba,
                               GpuImage* dst);

  bool is_depth_format(DXGI_FORMAT format);
  
  struct GpuBufferDesc
  {
    u64 size = 0;
    D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
    D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COMMON;
  };
  
  struct GpuBuffer
  {
    GpuBufferDesc desc;
    ID3D12Resource* d3d12_buffer = nullptr;
    u64 gpu_addr = 0;
  
    Option<void*> mapped = None;
    D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
  };
  GpuBuffer alloc_gpu_buffer_no_heap(const GraphicsDevice* device,
                                    GpuBufferDesc desc,
                                    GpuHeapType type,
                                    const char* name);
  void free_gpu_buffer(GpuBuffer* buffer);
  
  GpuBuffer alloc_gpu_buffer(const GraphicsDevice* device,
                            GpuLinearAllocator* allocator,
                            GpuBufferDesc desc,
                            const char* name);

  struct GpuBvh
  {
    GpuBuffer top_bvh;
    GpuBuffer bottom_bvh;
    GpuBuffer instance_desc_buffer;
  };

  // TODO(Brandon): We eventually will want to have this not take uber buffers but instead be more fine-grained...
  GpuBvh init_acceleration_structure(GraphicsDevice* device,
                                     const GpuBuffer& vertex_uber_buffer,
                                     u32 vertex_count,
                                     u32 vertex_stride,
                                     const GpuBuffer& index_uber_buffer,
                                     u32 index_count,
                                     const char* name);
  void destroy_acceleration_structure(GpuBvh* bvh);
  
  struct GpuUploadRange
  {
    u64 size = 0;
    FenceValue fence_value = 0;
  };
  
  struct GpuUploadRingBuffer
  {
    GpuBuffer gpu_buffer;
    u64 write = 0;
    u64 read = 0;
    u64 size = 0;
  
    RingQueue<GpuUploadRange> upload_fence_values;
    Fence fence;
    HANDLE cpu_event;
  };
  
  GpuUploadRingBuffer alloc_gpu_ring_buffer(MEMORY_ARENA_PARAM,
                                            const GraphicsDevice* device,
                                            u64 size);
  void free_gpu_ring_buffer(GpuUploadRingBuffer* gpu_upload_ring_buffer);
  FenceValue yield_gpu_upload_buffer(CmdListAllocator* cmd_allocator,
                                     GpuUploadRingBuffer* ring_buffer,
                                     GpuBuffer* dst,
                                     u64 dst_offset,
                                     const void* src,
                                     u64 size,
                                     u64 alignment = 1);
  
  enum DescriptorType : u8
  {
    kDescriptorTypeCbv     = 0x1,
    kDescriptorTypeSrv     = 0x2,
    kDescriptorTypeUav     = 0x4,
    kDescriptorTypeSampler = 0x8,
    kDescriptorTypeRtv     = 0x10,
    kDescriptorTypeDsv     = 0x20,
  };
  
  enum DescriptorHeapType : u8
  {
    kDescriptorHeapTypeCbvSrvUav = kDescriptorTypeCbv | kDescriptorTypeSrv | kDescriptorTypeUav,
    kDescriptorHeapTypeSampler   = kDescriptorTypeSampler,
    kDescriptorHeapTypeRtv       = kDescriptorTypeRtv,
    kDescriptorHeapTypeDsv       = kDescriptorTypeDsv,
  };
  
  struct DescriptorPool
  {
    ID3D12DescriptorHeap* d3d12_heap = nullptr;
    RingQueue<u32> free_descriptors;
    u64 descriptor_size = 0;
  
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_start = {0};
    Option<D3D12_GPU_DESCRIPTOR_HANDLE> gpu_start = None;
  
    u32 num_descriptors = 0;
    DescriptorHeapType type = kDescriptorHeapTypeCbvSrvUav;
  };
  
  DescriptorPool init_descriptor_pool(MEMORY_ARENA_PARAM,
                                      const GraphicsDevice* device,
                                      u32 size,
                                      DescriptorHeapType type);
  
  void destroy_descriptor_pool(DescriptorPool* pool);

  struct DescriptorLinearAllocator
  {
    ID3D12DescriptorHeap* d3d12_heap = nullptr;
    u32 pos = 0;
    u32 num_descriptors = 0;

    u64 descriptor_size = 0;

    D3D12_CPU_DESCRIPTOR_HANDLE cpu_start = {0};
    Option<D3D12_GPU_DESCRIPTOR_HANDLE> gpu_start = None;

    DescriptorHeapType type = kDescriptorHeapTypeCbvSrvUav;
  };

  DescriptorLinearAllocator init_descriptor_linear_allocator(const GraphicsDevice* device,
                                                             u32 size,
                                                             DescriptorHeapType type);
  
  void reset_descriptor_linear_allocator(DescriptorLinearAllocator* allocator);
  void destroy_descriptor_linear_allocator(DescriptorLinearAllocator* allocator);
  
  struct Descriptor
  {
    D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = {0};
    Option<D3D12_GPU_DESCRIPTOR_HANDLE> gpu_handle = None;
    u32 index = 0;
    DescriptorHeapType type = kDescriptorHeapTypeCbvSrvUav;
  };
  
  Descriptor alloc_descriptor(DescriptorPool* pool);
  void free_descriptor(DescriptorPool* heap, Descriptor* descriptor);

  Descriptor alloc_descriptor(DescriptorLinearAllocator* allocator);

  
  void init_buffer_cbv(const GraphicsDevice* device,
                       Descriptor* descriptor,
                       const GpuBuffer* buffer,
                       u64 offset,
                       u32 size);
  
  void init_buffer_srv(const GraphicsDevice* device,
                       Descriptor* descriptor,
                       const GpuBuffer* buffer,
                       u32 first_element,
                       u32 num_elements,
                       u32 stride);
  
  void init_buffer_uav(const GraphicsDevice* device,
                       Descriptor* descriptor,
                       const GpuBuffer* buffer,
                       u32 first_element,
                       u32 num_elements,
                       u32 stride);
  
  void init_rtv(const GraphicsDevice* device, Descriptor* descriptor, const GpuImage* image);
  void init_dsv(const GraphicsDevice* device, Descriptor* descriptor, const GpuImage* image);
  void init_image_2D_srv(const GraphicsDevice* device, Descriptor* descriptor, const GpuImage* image);
  void init_image_2D_uav(const GraphicsDevice* device, Descriptor* descriptor, const GpuImage* image);

  void init_sampler(const GraphicsDevice* device, Descriptor* descriptor);

  void init_bvh_srv(const GraphicsDevice* device, Descriptor* descriptor, const GpuBvh* bvh);
  
  struct GpuShader
  {
    ID3DBlob* d3d12_shader = nullptr;
  };
  
  GpuShader load_shader_from_file(const GraphicsDevice* device, const wchar_t* path);
  void destroy_shader(GpuShader* shader);
  
  struct GraphicsPipelineDesc
  {
    GpuShader vertex_shader;
    GpuShader pixel_shader;
    Array<DXGI_FORMAT, 8> rtv_formats;
    DXGI_FORMAT dsv_format = DXGI_FORMAT_UNKNOWN;
    D3D12_COMPARISON_FUNC comparison_func = D3D12_COMPARISON_FUNC_GREATER;
    bool stencil_enable = false;
    u8 __padding__[3]{0};

    auto operator<=>(const GraphicsPipelineDesc& rhs) const = default;
  };
  static_assert(offsetof(GraphicsPipelineDesc, vertex_shader)   == 0);
  static_assert(offsetof(GraphicsPipelineDesc, pixel_shader)    == 8);
  static_assert(sizeof(DXGI_FORMAT) == 4);
  static_assert(offsetof(GraphicsPipelineDesc, rtv_formats)     == 16);
  static_assert(sizeof(Array<DXGI_FORMAT, 8>) == 48);
  static_assert(offsetof(GraphicsPipelineDesc, dsv_format)      == 64);
  static_assert(offsetof(GraphicsPipelineDesc, comparison_func) == 68);
  static_assert(offsetof(GraphicsPipelineDesc, stencil_enable)  == 72);
  static_assert(sizeof(GraphicsPipelineDesc) == 80);
  
  struct GraphicsPSO
  {
    ID3D12PipelineState* d3d12_pso = nullptr;
  };
  GraphicsPSO init_graphics_pipeline(const GraphicsDevice* device,
                                     GraphicsPipelineDesc desc,
                                     const char* name);
  void destroy_graphics_pipeline(GraphicsPSO* pipeline);

  struct ComputePSO
  {
    ID3D12PipelineState* d3d12_pso = nullptr;
  };

  ComputePSO init_compute_pipeline(const GraphicsDevice* device, GpuShader compute_shader, const char* name);
  void destroy_compute_pipeline(ComputePSO* pipeline);

  struct RayTracingPSO
  {
    ID3D12StateObject* d3d12_pso = nullptr;
    ID3D12StateObjectProperties* d3d12_properties = nullptr;
  };

  RayTracingPSO init_ray_tracing_pipeline(const GraphicsDevice* device,
                                          GpuShader ray_tracing_library,
                                          const char* name);
  void destroy_ray_tracing_pipeline(RayTracingPSO* pipeline);

  struct ShaderTable
  {
    GpuBuffer buffer;
    u32 record_size = 0;

    // TODO(Brandon): If we need more bytes then there's plenty to steal here using offset ptrs
    u64 ray_gen_addr = 0;
    u64 ray_gen_size = 0;
    u64 miss_addr = 0;
    u64 miss_size = 0;
    u64 hit_addr  = 0;
    u64 hit_size = 0;
  };

  ShaderTable init_shader_table(const GraphicsDevice* device,
                                RayTracingPSO pipeline,
                                const char* name);
  void destroy_shader_table(ShaderTable* shader_table);

  struct SwapChain
  {
    u32 width = 0;
    u32 height = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
  
    IDXGISwapChain4* d3d12_swap_chain = nullptr;
    Fence fence;
    FenceValue frame_fence_values[kFramesInFlight] = {0};
  
    GpuImage* back_buffers[kFramesInFlight] = {0};
    u32 back_buffer_index = 0;
  
    bool vsync = false;
    bool tearing_supported = false;
    bool fullscreen = false;
  };
  
  SwapChain init_swap_chain(MEMORY_ARENA_PARAM, HWND window, const GraphicsDevice* device);
  void destroy_swap_chain(SwapChain* swap_chain);
  
  const GpuImage* swap_chain_acquire(SwapChain* swap_chain);
  void swap_chain_submit(SwapChain* swap_chain, const GraphicsDevice* device, const GpuImage* rtv);
  
  void cmd_set_descriptor_heaps(CmdList* cmd, const DescriptorPool* heaps, u32 num_heaps);
  void cmd_set_descriptor_heaps(CmdList* cmd, Span<const DescriptorLinearAllocator*> heaps);
  void cmd_set_primitive_topology(CmdList* cmd, D3D_PRIMITIVE_TOPOLOGY topology = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  void cmd_set_graphics_root_signature(CmdList* cmd);
  void cmd_set_compute_root_signature(CmdList* cmd);
  
  void init_imgui_ctx(const GraphicsDevice* device,
                      const SwapChain* swap_chain,
                      HWND window,
                      DescriptorLinearAllocator* cbv_srv_uav_heap);
  void destroy_imgui_ctx();
  void imgui_begin_frame();
  void imgui_end_frame();
  void cmd_imgui_render(CmdList* cmd);
}


