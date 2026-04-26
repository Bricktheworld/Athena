#pragma once
#include "Core/Foundation/types.h"

#include "Core/Foundation/Containers/array.h"
#include "Core/Foundation/Containers/error_or.h"
#include "Core/Foundation/Containers/ring_buffer.h"
#include "Core/Foundation/Containers/hash_table.h"

#include "Core/Foundation/Gpu/gpu.h"

#include "Core/Foundation/math.h"

#include "Core/Engine/Shaders/interlop.hlsli"

#include "Core/Vendor/D3D12/d3d12.h"

#define PROFILE
#include "Core/Engine/Vendor/PIX/pix3.h"
#undef PROFILE

#include <dxgidebug.h>
#include <dxgi1_6.h>
#include <wrl.h>

struct GpuDevice;
extern GpuDevice* g_GpuDevice;

static const char* kTotalFrameGpuMarker = "GPU Total Frametime";

enum : u8
{
  kBackBufferCount       = 3,
  kFramesInFlight        = 2,
  kMaxCommandListThreads = 8,
  kCommandAllocators     = kBackBufferCount * kMaxCommandListThreads,
};

u64 get_gpu_memory_budget();
u64 get_gpu_memory_usage();

typedef u64 FenceValue;

typedef Vec4 Rgba;
typedef Vec3 Rgb;

struct GpuFence
{
  ID3D12Fence* d3d12_fence          = nullptr;
  FenceValue   value                = 0;
  FenceValue   last_completed_value = 0;
  HANDLE       cpu_event            = nullptr;
  bool         already_waiting      = false;
};

GpuFence   init_gpu_fence(void);
void       destroy_gpu_fence(GpuFence* fence);
bool       is_gpu_fence_complete(GpuFence* fence, FenceValue value);
FenceValue poll_gpu_fence_value(GpuFence* fence);
void       block_gpu_fence(GpuFence* fence, FenceValue value);

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
  CmdQueueType        type        = kCmdQueueTypeGraphics;
};

CmdQueue   init_cmd_queue(const GpuDevice* device, CmdQueueType type);
void       destroy_cmd_queue(CmdQueue* queue);
void       cmd_queue_gpu_wait_for_fence(const CmdQueue* queue, GpuFence* fence, FenceValue value);
FenceValue cmd_queue_signal(const CmdQueue* queue, GpuFence* fence);

struct CmdAllocator
{
  ID3D12CommandAllocator* d3d12_allocator = 0;
  FenceValue              fence_value     = 0;
};

struct CmdListAllocator
{
  ID3D12CommandQueue*                    d3d12_queue = nullptr;

  RingQueue<CmdAllocator>                allocators;
  RingQueue<ID3D12GraphicsCommandList7*> lists;
  GpuFence                               fence;
};

struct CmdList
{
  ID3D12GraphicsCommandList7*   d3d12_list      = nullptr;
  ID3D12CommandAllocator*       d3d12_allocator = nullptr;
};

CmdListAllocator init_cmd_list_allocator(
  AllocHeap heap,
  const GpuDevice* device,
  const CmdQueue* queue,
  u16 pool_size
);

void       destroy_cmd_list_allocator(CmdListAllocator* allocator);
CmdList    alloc_cmd_list(CmdListAllocator* allocator);
FenceValue submit_cmd_lists(
  CmdListAllocator* allocator,
  Span<CmdList> lists,
  Option<GpuFence*> fence = None
);

enum GpuHeapLocation : u8
{
  // GPU only
  kGpuHeapGpuOnly,

  // CPU to GPU living in SysRAM (CPU RAM)
  // This is also referred to as SysRAM upload
  kGpuHeapSysRAMCpuToGpu,

  // CPU to GPU living in VRAM
  // This is also referred to as VRAM upload
  // (Only supported on systems with ReBAR)
  kGpuHeapVRAMCpuToGpu,

  // GPU to CPU living in SysRAM (CPU RAM)
  // This is also referred to as a readback heap
  kGpuHeapSysRAMGpuToCpu,
};

enum GpuHeapType : u8
{
  kGpuAllocHeap,
  kGpuFreeHeap,
};

struct GpuAllocation
{
  u32             size       = 0;
  u32             offset     = 0;
  ID3D12Heap*     d3d12_heap = nullptr;

  // Optional metadata (usually an ID or offset of the allocation so that we can free)
  u32             metadata   = 0;
  GpuHeapLocation location   = kGpuHeapGpuOnly;
};

struct GpuAllocHeap
{
  GpuAllocation (*alloc_fn)(void* allocator, u32 size, u32 alignment)    = nullptr;
  void* allocator = nullptr;
};

struct GpuFreeHeap
{
  GpuAllocation (*alloc_fn)(void* allocator, u32 size, u32 alignment)    = nullptr;
  void          (*free_fn) (void* allocator, const GpuAllocation& alloc) = nullptr;
  void* allocator = nullptr;
};

#define GPU_HEAP_ALLOC(heap, size, alignment)( (heap).alloc_fn ((heap).allocator, (size), (alignment)) )
#define GPU_HEAP_FREE(heap,  alloc)( (heap).free_fn((heap).allocator, (GpuAllocation)alloc) )


GpuAllocation gpu_linear_alloc(void* allocator, u32 size, u32 alignment);
struct GpuLinearAllocator
{
  ID3D12Heap*     d3d12_heap = nullptr;
  u32             size       = 0;
  u32             pos        = 0;
  GpuHeapLocation location   = kGpuHeapGpuOnly;

  operator GpuAllocHeap()
  {
    GpuAllocHeap ret = {0};
    ret.alloc_fn     = &gpu_linear_alloc;
    ret.allocator    = this;
    return ret;
  }
};


GpuLinearAllocator init_gpu_linear_allocator(u32 size, GpuHeapLocation location);
void destroy_gpu_linear_allocator(GpuLinearAllocator* allocator);

inline void
reset_gpu_linear_allocator(GpuLinearAllocator* allocator)
{
  allocator->pos = 0;
}

enum GpuTextureUsageFlags
{
};


struct GpuTextureDesc
{
  u32                   width         = 0;
  u32                   height        = 0;
  u16                   array_size    = 1;

  GpuFormat             format        = kGpuFormatRGBA8Unorm;
  D3D12_RESOURCE_FLAGS  flags         = D3D12_RESOURCE_FLAG_NONE;
  union
  {
    Vec4                color_clear_value;
    struct
    {
      f32               depth_clear_value;
      s8                stencil_clear_value;
    };
  };
};


enum GpuTextureLayout : u32
{
  kGpuTextureLayoutGeneral = 0,
  kGpuTextureLayoutShaderResource,
  kGpuTextureLayoutUnorderedAccess,
  kGpuTextureLayoutRenderTarget,
  kGpuTextureLayoutDepthStencil,
  kGpuTextureLayoutDiscard,
};

struct GpuTexture
{
  GpuTextureDesc        desc;
  ID3D12Resource*       d3d12_texture = nullptr;
  GpuTextureLayout      layout        = kGpuTextureLayoutGeneral;
};

GpuTexture alloc_gpu_texture_no_heap(
  const GpuDevice* device,
  GpuTextureDesc desc,
  const char* name
);
void free_gpu_texture(GpuTexture* texture);

GpuTexture alloc_gpu_texture(
  const GpuDevice* device,
  GpuAllocHeap heap,
  GpuTextureDesc desc,
  const char* name
);

bool is_depth_format(GpuFormat format);

struct GpuBufferDesc
{
  u32                   size      = 0;
  D3D12_RESOURCE_FLAGS  flags     = D3D12_RESOURCE_FLAG_NONE;
  bool                  is_rt_bvh = false;
};

struct GpuBuffer
{
  GpuBufferDesc         desc;
  ID3D12Resource*       d3d12_buffer = nullptr;
  u64                   gpu_addr     = 0;

  Option<void*>         mapped       = None;
};
GpuBuffer alloc_gpu_buffer_no_heap(
  const GpuDevice* device,
  GpuBufferDesc desc,
  GpuHeapLocation location,
  const char* name
);
void free_gpu_buffer(GpuBuffer* buffer);

GpuBuffer alloc_gpu_buffer(
  const GpuDevice* device,
  GpuAllocHeap heap,
  GpuBufferDesc desc,
  const char* name
);

struct GpuRingBuffer
{
  GpuBuffer  buffer;
  GpuFence   fence;

  u32        write;
  u32        read;
  u32        used;

  struct GpuAllocationFence
  {
    FenceValue value  = 0;
    u32        size   = 0;
    u32        offset = 0;
  };

  RingQueue<GpuAllocationFence> queued_fences;
};

GpuRingBuffer alloc_gpu_ring_buffer_no_heap(AllocHeap heap, GpuBufferDesc desc, GpuHeapLocation location, const char* name);

struct GpuRingBufferAllocation
{
  FenceValue    wait_for = 0;
  u32           offset   = 0;
  Option<void*> mapped   = nullptr;
};

// Blocking wait for available size
void gpu_ring_buffer_wait(GpuRingBuffer* buffer, u32 size);

// Either returns the offset or the fence value to wait for
Result<u64, FenceValue> gpu_ring_buffer_alloc(GpuRingBuffer* buffer, u32 size, u32 alignment = 1);
// You need to commit the allocations otherwise they will stall. 
// Commit after you are done using the memory/submitted the command buffer using it.
void gpu_ring_buffer_commit(const GpuRingBuffer* buffer, CmdQueue* queue);
void gpu_ring_buffer_commit(const GpuRingBuffer* buffer, CmdListAllocator* cmd_buffer_allocator);

void free_gpu_ring_buffer(GpuRingBuffer* buffer);


struct GpuBvh
{
  GpuBuffer tlas;
  GpuBuffer blas;
  GpuBuffer instance_desc_buffer;
};

struct GpuRtBlasDesc
{
  // I am very intentionally not storing references to the GpuBuffer for index/vertex buffers
  // as that is very unsafe (and we don't really need it). This description is stored just so
  // we can access it later when actually building the BLAS
  u32              vertex_start         = 0;
  u32              vertex_count         = 0;
  GpuFormat        vertex_format        = kGpuFormatRGB32Float;
  u32              vertex_stride        = sizeof(Vertex);

  u32              index_start          = 0;
  u32              index_count          = 0;
  u32              index_stride         = sizeof(u16);

  // Will make make tracing slower
  bool             allow_updates:     1 = false;
  // May possibly make tracing slower, but will reduce VRAM usage
  bool             minimize_memory:   1 = false;
};

struct GpuRtBlas
{
  GpuBuffer     buffer;

  GpuRtBlasDesc desc;
  u32           scratch_size = 0;
};

GpuRtBlas alloc_gpu_rt_blas(
  GpuAllocHeap heap,
  const GpuBuffer& vertex_buffer,
  const GpuBuffer& index_buffer,

  GpuRtBlasDesc    desc,

  const char*      name
);

struct GpuRtTlas
{
  GpuBuffer buffer;

  u32       max_instances = 0;

  u32       scratch_size  = 0;
};

GpuRtTlas alloc_gpu_rt_tlas_no_heap(
  u32              num_descs,
  const char*      name
);

struct GpuRtTlasSizeInfo
{
  u32 max_size     = 0;
  u32 scratch_size = 0;
};

GpuRtTlasSizeInfo query_gpu_rt_tlas_size_info(u32 max_instances);

GpuRtTlas alloc_gpu_rt_tlas(
  GpuAllocHeap     heap,
  u32              num_descs,
  const char*      name
);

enum DescriptorType : u8
{
  kDescriptorTypeNull       = 0x0,
  kDescriptorTypeCbv        = 0x1 << 0,
  kDescriptorTypeSrv        = 0x1 << 1,
  kDescriptorTypeUav        = 0x1 << 2,
  kDescriptorTypeSampler    = 0x1 << 3,
  kDescriptorTypeRtv        = 0x1 << 4,
  kDescriptorTypeDsv        = 0x1 << 5,
};

enum DescriptorHeapType : u8
{
  kDescriptorHeapTypeCbvSrvUav       = kDescriptorTypeCbv | kDescriptorTypeSrv | kDescriptorTypeUav,
  kDescriptorHeapTypeSampler         = kDescriptorTypeSampler,
  kDescriptorHeapTypeRtv             = kDescriptorTypeRtv,
  kDescriptorHeapTypeDsv             = kDescriptorTypeDsv,
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

DescriptorPool init_descriptor_pool(
  AllocHeap heap,
  u32 size,
  DescriptorHeapType type,
  u32 table_reserved = 0
);

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

DescriptorLinearAllocator init_descriptor_linear_allocator(
  const GpuDevice* device,
  u32 size,
  DescriptorHeapType type
);

void reset_descriptor_linear_allocator(DescriptorLinearAllocator* allocator);
void destroy_descriptor_linear_allocator(DescriptorLinearAllocator* allocator);

struct GpuDescriptor
{
  D3D12_CPU_DESCRIPTOR_HANDLE         cpu_handle = {0};
  Option<D3D12_GPU_DESCRIPTOR_HANDLE> gpu_handle = None;
  DescriptorHeapType                  type       = kDescriptorHeapTypeCbvSrvUav;
  u32                                 index      = 0;
};

GpuDescriptor alloc_descriptor(DescriptorPool* pool);
GpuDescriptor alloc_table_descriptor(DescriptorPool* pool, u32 idx);
void          free_descriptor(DescriptorPool* heap, GpuDescriptor* descriptor);

GpuDescriptor alloc_descriptor(DescriptorLinearAllocator* allocator);

struct GpuBufferCbvDesc
{
  u64 buffer_offset = 0;
  u64 size          = 0;
};

void init_buffer_cbv(
  GpuDescriptor* descriptor,
  const GpuBuffer* buffer,
  const GpuBufferCbvDesc& desc
);

struct GpuBufferSrvDesc
{
  u64       first_element = 0;
  u32       num_elements  = 0;
  u32       stride        = 0;
  GpuFormat format        = kGpuFormatUnknown;
  bool      is_raw        = false;
  u16       __pad__       = 0;
};

void init_buffer_srv(
  GpuDescriptor* descriptor,
  const GpuBuffer* buffer,
  const GpuBufferSrvDesc& desc
);

struct GpuBufferUavDesc
{
  u64       first_element  = 0;
  u32       num_elements   = 0;
  u32       stride         = 0;
  GpuFormat format         = kGpuFormatUnknown;
  u8        counter_offset = 0;
  bool      is_raw         = false;
  u8        __pad__        = 0;
};

void init_buffer_uav(
  GpuDescriptor* descriptor,
  const GpuBuffer* buffer,
  const GpuBufferUavDesc& desc
);

void init_buffer_counted_uav(
  GpuDescriptor*          descriptor,
  const GpuBuffer*        buffer,
  const GpuBuffer*        counter,
  const GpuBufferUavDesc& desc
);

struct GpuTextureSrvDesc
{
  u32       mip_levels        = 0;
  u32       most_detailed_mip = 0;
  u32       array_size        = 0;
  GpuFormat format            = kGpuFormatUnknown;
};

void init_texture_srv(GpuDescriptor* descriptor, const GpuTexture* texture, const GpuTextureSrvDesc& desc);

struct GpuTextureUavDesc
{
  u32       array_size        = 0;
  GpuFormat format            = kGpuFormatUnknown;
};
void init_texture_uav(GpuDescriptor* descriptor, const GpuTexture* texture, const GpuTextureUavDesc& desc);

void init_rtv(GpuDescriptor* descriptor, const GpuTexture* texture);
void init_dsv(GpuDescriptor* descriptor, const GpuTexture* texture);

void init_bvh_srv(GpuDescriptor* descriptor, const GpuRtTlas* tlas);
void init_bvh_srv(GpuDescriptor* descriptor, const GpuBvh* bvh);

struct GpuShader
{
  ID3DBlob* d3d12_shader = nullptr;
  u32       generation   = 0;

  u32       __padding__  = 0;
  // Used by aftermath for diagnosing GPU crashes
  u64       hash         = 0;
};

GpuShader load_shader_from_file  (const GpuDevice* device, const wchar_t* path);
GpuShader load_shader_from_memory(const GpuDevice* device, const u8* src, size_t size);
void reload_shader_from_memory(GpuShader* shader, const u8* src, size_t size);
void destroy_shader(GpuShader* shader);

enum PrimitiveTopologyType : u8
{
  kPrimitiveTopologyUndefined,
  kPrimitiveTopologyPoint,
  kPrimitiveTopologyLine,
  kPrimitiveTopologyTriangle,
  kPrimitiveTopologyPatch,

  kPrimitiveTopologyCount,
};

enum DepthFunc : u8
{
  kDepthFuncNone,
  kDepthFuncNever,
  kDepthFuncLess,
  kDepthFuncEqual,
  kDepthFuncLessEqual,
  kDepthFuncGreater,
  kDepthFuncNotEqual,
  kDepthFuncGreaterEqual,
  kDepthFuncAlways,

  kDepthFuncCount,
};

struct GraphicsPipelineDesc
{
  const GpuShader*      vertex_shader           = nullptr;
  const GpuShader*      pixel_shader            = nullptr;
  Array<GpuFormat, 8>   rtv_formats;            
  GpuFormat             dsv_format              = kGpuFormatUnknown;
  DepthFunc             depth_func              = kDepthFuncGreater;
  PrimitiveTopologyType topology                = kPrimitiveTopologyTriangle;
  bool                  stencil_enable:  1      = false;
  bool                  blend_enable:    1      = false;
  bool                  depth_read_only: 1      = false;
  u8                    __padding__[2]{0};

  auto operator<=>(const GraphicsPipelineDesc& rhs) const = default;
};

struct GraphicsPSO
{
  ID3D12PipelineState* d3d12_pso                = nullptr;
  const char*          name                     = nullptr;
  u32                  vertex_shader_generation = 0;
  u32                  pixel_shader_generation  = 0;
  GraphicsPipelineDesc desc;

};
GraphicsPSO init_graphics_pipeline(
  const GpuDevice* device,
  const GraphicsPipelineDesc& desc,
  const char* name
);
void reload_graphics_pipeline(GraphicsPSO* pipeline);
void destroy_graphics_pipeline(GraphicsPSO* pipeline);

struct ComputePSO
{
  ID3D12PipelineState* d3d12_pso                 = nullptr;
  const char*          name                      = nullptr;
  const GpuShader*     compute_shader            = nullptr;
  u32                  compute_shader_generation = 0;
  u32                  __padding__               = 0;
};

ComputePSO init_compute_pipeline(const GpuDevice* device, const GpuShader* compute_shader, const char* name);
// Handle shader hot reloading
void reload_compute_pipeline(ComputePSO* pipeline);
void destroy_compute_pipeline(ComputePSO* pipeline);

struct RayTracingPSO
{
  ID3D12StateObject*           d3d12_pso                      = nullptr;
  ID3D12StateObjectProperties* d3d12_properties               = nullptr;
  const char*                  name                           = nullptr;
  const GpuShader*             ray_tracing_library            = nullptr;
  u32                          ray_tracing_library_generation = 0;
  u32                          __padding__                    = 0;

};

RayTracingPSO init_ray_tracing_pipeline(
  const GpuDevice* device,
  const GpuShader* ray_tracing_library,
  const char* name
);
void reload_ray_tracing_pipeline(RayTracingPSO* pipeline);
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

ShaderTable init_shader_table(
  const GpuDevice* device,
  RayTracingPSO pipeline,
  const char* name
);
void destroy_shader_table(ShaderTable* shader_table);

static constexpr u32 kMaxGpuTimestamps = 128;

struct GpuTimestamp
{
  const char* name = nullptr;
  bool in_flight   = false;
};

struct GpuProfiler
{
  ID3D12QueryHeap*            d3d12_timestamp_heap = nullptr;
  u64                         gpu_frequency = 0;

  GpuBuffer                   timestamp_readback;
  GpuTimestamp                timestamps[kMaxGpuTimestamps];
  u32                         next_free_idx = 0;


  HashTable<const char*, u32> name_to_timestamp;
};

struct GpuDevice
{
  ID3D12Device15*         d3d12           = nullptr;
  IDXGIAdapter4*          dxgi_adapter     = nullptr;
  IDXGIDebug*             dxgi_debug      = nullptr;
  u32                     flags;

  ID3D12InfoQueue1*       d3d12_info_queue = nullptr;
  IDXGIInfoQueue*         dxgi_info_queue  = nullptr;

  ID3D12CommandSignature* d3d12_multi_draw_indirect_signature         = nullptr;
  ID3D12CommandSignature* d3d12_multi_draw_indirect_indexed_signature = nullptr;
  ID3D12CommandSignature* d3d12_dispatch_indirect_signature           = nullptr;

  wchar_t                 gpu_name[128];

  GpuProfiler             profiler;


  CmdQueue                graphics_queue;
  CmdListAllocator        graphics_cmd_allocator;
  CmdQueue                compute_queue;
  CmdListAllocator        compute_cmd_allocator;
  CmdQueue                copy_queue;
  CmdListAllocator        copy_cmd_allocator;

};

enum GpuDeviceFlags : u32
{
  kGpuFlagsEnableValidationLayers = 0x1 << 0,
  kGpuFlagsEnableGpuValidation    = 0x1 << 1,
  kGpuFlagsEnableRtValidation     = 0x1 << 2,
};

void init_gpu_device(HWND window, u32 flags);
void destroy_gpu_device();

void wait_for_gpu_device_idle(GpuDevice* device);

void begin_gpu_profiler_timestamp(CmdList* cmd_buffer, STRING_LITERAL const char* name);
void end_gpu_profiler_timestamp(CmdList* cmd_buffer, STRING_LITERAL const char* name);
f64  query_gpu_profiler_timestamp(STRING_LITERAL const char* name);

struct SwapChain
{
  u32       width  = 0;
  u32       height = 0;
  GpuFormat format = kGpuFormatUnknown;
  u32       flags  = 0;

  IDXGISwapChain4* d3d12_swap_chain       = nullptr;
  HANDLE           d3d12_latency_waitable = nullptr;
  GpuFence         fence;
  FenceValue       frame_fence_values[kBackBufferCount] = {0};

  GpuTexture* back_buffers[kBackBufferCount] = {0};
  u32         back_buffer_index              = 0;

  bool missed_vsync:      1 = false;
  bool vsync:             1 = false;
  bool tearing_supported: 1 = false;
  bool fullscreen:        1 = false;
};

SwapChain init_swap_chain(HWND window, const GpuDevice* device);
void destroy_swap_chain(SwapChain* swap_chain);

const GpuTexture* swap_chain_acquire(SwapChain* swap_chain);
void swap_chain_wait_latency(SwapChain* swap_chain);
void swap_chain_submit(SwapChain* swap_chain, const GpuDevice* device, const GpuTexture* rtv);
void swap_chain_resize(SwapChain* swap_chain, HWND window, GpuDevice* device);

void set_descriptor_heaps(CmdList* cmd, const DescriptorPool* heaps, u32 num_heaps);
void set_descriptor_heaps(CmdList* cmd, Span<const DescriptorPool*> heaps);
void set_descriptor_table(CmdList* cmd, const DescriptorPool* heap, u32 start_idx, u32 bind_slot);
void set_graphics_root_signature(CmdList* cmd);
void set_compute_root_signature(CmdList* cmd);

enum GpuRtasBuildFlags : u32
{
  // Perform an incremental update at the cost of tracing performance
  kGpuRtasBuildIncremental = 0x1 << 0,
};

void build_rt_blas(
  CmdList*         cmd,
  const GpuRtBlas& blas,
  const GpuBuffer& scratch,
  u32              scratch_offset,
  const GpuBuffer& index_buffer,
  const GpuBuffer& vertex_buffer,
  u32              flags = 0
);

void build_rt_tlas(
  CmdList*         cmd,
  const GpuRtTlas& tlas,
  const GpuBuffer& instance_buffer,
  u32              instance_count,
  const GpuBuffer& scratch,
  u32              scratch_offset,
  u32              flags = 0
);

void gpu_copy_buffer(
  CmdList* cmd,
  const    GpuBuffer& dst,
  u64      dst_offset,
  const    GpuBuffer& src,
  u64      src_offset,
  u64      bytes
);

static constexpr u32 kGpuTextureAlignment = 512;
// Copy buffer to texture
void gpu_copy_texture(
        CmdList*    cmd,
        GpuTexture* dst,
  const GpuBuffer&  src,
        u64         src_offset,
        u64         src_size
);

void gpu_memory_barrier(CmdList* cmd);
// NOTE(bshihabi): It is the caller's responsibility to not submit the CmdList's out of order here. 
// Doing so would put the GpuTexture in a bad state since the layout is tracked within the GpuTexture struct
void gpu_texture_layout_transition(CmdList* cmd, GpuTexture* texture, GpuTextureLayout layout);

void init_imgui_ctx(
  const GpuDevice* device,
  GpuFormat rtv_format,
  HWND window,
  DescriptorLinearAllocator* cbv_srv_uav_heap
);
void destroy_imgui_ctx();
void imgui_begin_frame();
void imgui_end_frame();
void imgui_render(CmdList* cmd);

#define U32_COLOR(r, g, b) (0xff000000u | ((u32)r << 16) | ((u32)g << 8) | (u32)b)
#define GPU_SCOPED_EVENT(color, cmdlist, fmt, ...) PIXBeginEvent((cmdlist)->d3d12_list, color, fmt, ##__VA_ARGS__); begin_gpu_profiler_timestamp((cmdlist), fmt); defer { PIXEndEvent((cmdlist)->d3d12_list); end_gpu_profiler_timestamp((cmdlist), fmt); }


template <typename T>
inline GpuFormat gpu_format_from_type();

template <>
inline GpuFormat gpu_format_from_type<float>()    { return kGpuFormatR32Float;    }

template <>
inline GpuFormat gpu_format_from_type<f16>()      { return kGpuFormatR16Float;    }

template <>
inline GpuFormat gpu_format_from_type<float2>()   { return kGpuFormatRG32Float;   }

template <>
inline GpuFormat gpu_format_from_type<float3>()   { return kGpuFormatRGB32Float;  }

template <>
inline GpuFormat gpu_format_from_type<float4>()   { return kGpuFormatRGBA32Float; }

template <>
inline GpuFormat gpu_format_from_type<u8>()       { return kGpuFormatR8Uint;      }

template <>
inline GpuFormat gpu_format_from_type<Vec2u8>()   { return kGpuFormatRG8Uint;     }

template <>
inline GpuFormat gpu_format_from_type<Vec4u8>()   { return kGpuFormatRGBA8Uint;   }

template <>
inline GpuFormat gpu_format_from_type<u16>()      { return kGpuFormatR16Uint;     }

template <>
inline GpuFormat gpu_format_from_type<Vec2u16>()  { return kGpuFormatRG16Uint;    }

template <>
inline GpuFormat gpu_format_from_type<Vec4u16>()  { return kGpuFormatRGBA16Uint;  }

template <>
inline GpuFormat gpu_format_from_type<uint>()     { return kGpuFormatR32Uint;     }

template <>
inline GpuFormat gpu_format_from_type<uint2>()    { return kGpuFormatRG32Uint;    }

template <>
inline GpuFormat gpu_format_from_type<uint3>()    { return kGpuFormatRGB32Uint;   }

template <>
inline GpuFormat gpu_format_from_type<uint4>()    { return kGpuFormatRGBA32Uint;  }

