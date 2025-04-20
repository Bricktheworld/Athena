#pragma once
#include "Core/Foundation/types.h"

#include "Core/Foundation/Containers/array.h"
#include "Core/Foundation/Containers/ring_buffer.h"
#include "Core/Foundation/Containers/hash_table.h"

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

typedef u64 FenceValue;

typedef Vec4 Rgba;
typedef Vec3 Rgb;

enum GpuFormat : u8
{
  kGpuFormatUnknown                  = 0,
  kGpuFormatRGBA32Typeless           = 1,
  kGpuFormatRGBA32Float              = 2,
  kGpuFormatRGBA32Uint               = 3,
  kGpuFormatRGBA32Sint               = 4,
  kGpuFormatRGB32Typeless            = 5,
  kGpuFormatRGB32Float               = 6,
  kGpuFormatRGB32Uint                = 7,
  kGpuFormatRGB32Sint                = 8,
  kGpuFormatRGBA16Typeless           = 9,
  kGpuFormatRGBA16Float              = 10,
  kGpuFormatRGBA16Unorm              = 11,
  kGpuFormatRGBA16Uint               = 12,
  kGpuFormatRGBA16Snorm              = 13,
  kGpuFormatRGBA16Sint               = 14,
  kGpuFormatRG32Typeless             = 15,
  kGpuFormatRG32Float                = 16,
  kGpuFormatRG32Uint                 = 17,
  kGpuFormatRG32Sint                 = 18,
  kGpuFormatR32G8x24Typeless         = 19,
  kGpuFormatD32FloatS8x24Uint        = 20,
  kGpuFormatR32FloatX8x24Typeless    = 21,
  kGpuFormatX32TypelessG8x24Uint     = 22,
  kGpuFormatRGB10A2Typeless          = 23,
  kGpuFormatRGB10A2Unorm             = 24,
  kGpuFormatRGB10A2Uint              = 25,
  kGpuFormatR11G11B10Float           = 26,
  kGpuFormatRGBA8Typeless            = 27,
  kGpuFormatRGBA8Unorm               = 28,
  kGpuFormatRGBA8UnormSrgb           = 29,
  kGpuFormatRGBA8Uint                = 30,
  kGpuFormatRGBA8Snorm               = 31,
  kGpuFormatRGBA8Sint                = 32,
  kGpuFormatRG16Typeless             = 33,
  kGpuFormatRG16Float                = 34,
  kGpuFormatRG16Unorm                = 35,
  kGpuFormatRG16Uint                 = 36,
  kGpuFormatRG16Snorm                = 37,
  kGpuFormatRG16Sint                 = 38,
  kGpuFormatR32Typeless              = 39,
  kGpuFormatD32Float                 = 40,
  kGpuFormatR32Float                 = 41,
  kGpuFormatR32Uint                  = 42,
  kGpuFormatR32Sint                  = 43,
  kGpuFormatR24G8Typeless            = 44,
  kGpuFormatD24UnormS8Uint           = 45,
  kGpuFormatR24UnormX8Typeless       = 46,
  kGpuFormatX24TypelessG8Uint        = 47,
  kGpuFormatRG8Typeless              = 48,
  kGpuFormatRG8Unorm                 = 49,
  kGpuFormatRG8Uint                  = 50,
  kGpuFormatRG8Snorm                 = 51,
  kGpuFormatRG8Sint                  = 52,
  kGpuFormatR16Typeless              = 53,
  kGpuFormatR16Float                 = 54,
  kGpuFormatD16Unorm                 = 55,
  kGpuFormatR16Unorm                 = 56,
  kGpuFormatR16Uint                  = 57,
  kGpuFormatR16Snorm                 = 58,
  kGpuFormatR16Sint                  = 59,
  kGpuFormatR8Typeless               = 60,
  kGpuFormatR8Unorm                  = 61,
  kGpuFormatR8Uint                   = 62,
  kGpuFormatR8Snorm                  = 63,
  kGpuFormatR8Sint                   = 64,
  kGpuFormatA8Unorm                  = 65,
  kGpuFormatR1Unorm                  = 66,
  kGpuFormatRGB9E5SharedExp          = 67,
  kGpuFormatRG8B8G8Unorm             = 68,
  kGpuFormatG8R8G8B8Unorm            = 69,
  kGpuFormatBC1Typeless              = 70,
  kGpuFormatBC1Unorm                 = 71,
  kGpuFormatBC1UnormSrgb             = 72,
  kGpuFormatBC2Typeless              = 73,
  kGpuFormatBC2Unorm                 = 74,
  kGpuFormatBC2UnormSrgb             = 75,
  kGpuFormatBC3Typeless              = 76,
  kGpuFormatBC3Unorm                 = 77,
  kGpuFormatBC3UnormSrgb             = 78,
  kGpuFormatBC4Typeless              = 79,
  kGpuFormatBC4Unorm                 = 80,
  kGpuFormatBC4Snorm                 = 81,
  kGpuFormatBC5Typeless              = 82,
  kGpuFormatBC5Unorm                 = 83,
  kGpuFormatBC5Snorm                 = 84,
  kGpuFormatB5G6R5Unorm              = 85,
  kGpuFormatB5G5R5A1Unorm            = 86,
  kGpuFormatBGRA8Unorm               = 87,
  kGpuFormatBGRX8Unorm               = 88,
  kGpuFormatRGB10XRBiasA2Unorm       = 89,
  kGpuFormatBGRA8Typeless            = 90,
  kGpuFormatBGRA8UnormSrgb           = 91,
  kGpuFormatBGRX8Typeless            = 92,
  kGpuFormatBGRX8UnormSrgb           = 93,
  kGpuFormatBC6HTypeless             = 94,
  kGpuFormatBC6HUF16                 = 95,
  kGpuFormatBC6HSF16                 = 96,
  kGpuFormatBC7Typeless              = 97,
  kGpuFormatBC7Unorm                 = 98,
  kGpuFormatBC7UnormSrgb             = 99,
  kGpuFormatAYUV                     = 100,
  kGpuFormatY410                     = 101,
  kGpuFormatY416                     = 102,
  kGpuFormatNV12                     = 103,
  kGpuFormatP010                     = 104,
  kGpuFormatP016                     = 105,
  kGpuFormatOpaque420                = 106,
  kGpuFormatYUY2                     = 107,
  kGpuFormatY210                     = 108,
  kGpuFormatY216                     = 109,
  kGpuFormatNV11                     = 110,
  kGpuFormatAI44                     = 111,
  kGpuFormatIA44                     = 112,
  kGpuFormatP8                       = 113,
  kGpuFormatA8P8                     = 114,
  kGpuFormatB4G4R4A4Unorm            = 115,
  kGpuFormatP208                     = 130,
  kGpuFormatV208                     = 131,
  kGpuFormatV408                     = 132,
  kGpuFormatSamplerFeedbackMinMip    = 189,
  kGpuFormatSamplerFeedbackMipRegion = 190,
};


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
  RingQueue<ID3D12GraphicsCommandList4*> lists;
  GpuFence                                  fence;
};

struct CmdList
{
  ID3D12GraphicsCommandList4* d3d12_list      = nullptr;
  ID3D12CommandAllocator*     d3d12_allocator = nullptr;
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



struct GpuTextureDesc
{
  u32                   width         = 0;
  u32                   height        = 0;
  u16                   array_size    = 1;

  // TODO(Brandon): Eventually make these less verbose and platform agnostic.
  GpuFormat             format        = kGpuFormatRGBA8Unorm;
  D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COMMON;

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

struct GpuTexture
{
  GpuTextureDesc        desc;
  ID3D12Resource*       d3d12_texture = nullptr;
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

void upload_gpu_texture(
  const GpuDevice* device,
  const void* rgba,
  GpuTexture* dst
);

bool is_depth_format(GpuFormat format);

struct GpuBufferDesc
{
  u32                   size          = 0;
  D3D12_RESOURCE_FLAGS  flags         = D3D12_RESOURCE_FLAG_NONE;
  D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COMMON;
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

struct GpuBvh
{
  GpuBuffer top_bvh;
  GpuBuffer bottom_bvh;
  GpuBuffer instance_desc_buffer;
};

// TODO(Brandon): We eventually will want to have this not take uber buffers but instead be more fine-grained...
GpuBvh init_gpu_bvh(
  GpuDevice* device,
  const GpuBuffer& vertex_uber_buffer,
  u32 vertex_count,
  u32 vertex_stride,
  const GpuBuffer& index_uber_buffer,
  u32 index_count,
  const char* name
);
void destroy_acceleration_structure(GpuBvh* bvh);

enum DescriptorType : u8
{
  kDescriptorTypeNull    = 0x0,
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

DescriptorPool init_descriptor_pool(
  AllocHeap heap,
  const GpuDevice* device,
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
  u64       first_element = 0;
  u32       num_elements  = 0;
  u32       stride        = 0;
  GpuFormat format        = kGpuFormatUnknown;
  bool      is_raw        = false;
  u16       __pad__       = 0;
};

void init_buffer_uav(
  GpuDescriptor* descriptor,
  const GpuBuffer* buffer,
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

void init_bvh_srv(GpuDescriptor* descriptor, const GpuBvh* bvh);

struct GpuShader
{
  ID3DBlob* d3d12_shader = nullptr;
};

GpuShader load_shader_from_file  (const GpuDevice* device, const wchar_t* path);
GpuShader load_shader_from_memory(const GpuDevice* device, const u8* src, size_t size);
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
  const GpuShader* vertex_shader;
  const GpuShader* pixel_shader;
  Array<GpuFormat, 8> rtv_formats;
  GpuFormat dsv_format = kGpuFormatUnknown;
  DepthFunc depth_func = kDepthFuncGreater;
  PrimitiveTopologyType topology = kPrimitiveTopologyTriangle;
  bool stencil_enable:  1 = false;
  bool blend_enable:    1 = false;
  bool depth_read_only: 1 = false;
  u8 __padding__[2]{0};

  auto operator<=>(const GraphicsPipelineDesc& rhs) const = default;
};

struct GraphicsPSO
{
  ID3D12PipelineState* d3d12_pso = nullptr;
};
GraphicsPSO init_graphics_pipeline(
  const GpuDevice* device,
  const GraphicsPipelineDesc& desc,
  const char* name
);
void destroy_graphics_pipeline(GraphicsPSO* pipeline);

struct ComputePSO
{
  ID3D12PipelineState* d3d12_pso = nullptr;
};

ComputePSO init_compute_pipeline(const GpuDevice* device, const GpuShader* compute_shader, const char* name);
void destroy_compute_pipeline(ComputePSO* pipeline);

struct RayTracingPSO
{
  ID3D12StateObject* d3d12_pso = nullptr;
  ID3D12StateObjectProperties* d3d12_properties = nullptr;
};

RayTracingPSO init_ray_tracing_pipeline(
  const GpuDevice* device,
  const GpuShader* ray_tracing_library,
  const char* name
);
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
  ID3D12Device6*          d3d12       = nullptr;
  IDXGIDebug*             d3d12_debug = nullptr;

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
void init_gpu_device(HWND window);
void destroy_gpu_device();

void wait_for_gpu_device_idle(GpuDevice* device);

void begin_gpu_profiler_timestamp(const CmdList& cmd_buffer, const char* name);
void end_gpu_profiler_timestamp(const CmdList& cmd_buffer, const char* name);
f64  query_gpu_profiler_timestamp(const char* name);

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
#define GPU_SCOPED_EVENT(color, cmdlist, fmt, ...) PIXBeginEvent(cmdlist.d3d12_list, color, fmt, ##__VA_ARGS__); begin_gpu_profiler_timestamp(cmdlist, fmt); defer { PIXEndEvent(cmdlist.d3d12_list); end_gpu_profiler_timestamp(cmdlist, fmt); }

