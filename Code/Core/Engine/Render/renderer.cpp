#include "Core/Foundation/sort.h"

#include "Core/Engine/memory.h"
#include "Core/Engine/asset_streaming.h"

#include "Core/Engine/Render/renderer.h"
#include "Core/Engine/Render/blit.h"
#include "Core/Engine/Render/depth_of_field.h"
#include "Core/Engine/Render/ddgi.h"
#include "Core/Engine/Render/gbuffer.h"
#include "Core/Engine/Render/lighting.h"
#include "Core/Engine/Render/misc.h"
#include "Core/Engine/Render/post_processing.h"
#include "Core/Engine/Render/taa.h"
#include "Core/Engine/Render/visibility_buffer.h"

#include "Core/Engine/Shaders/root_signature.hlsli"
#include "Core/Engine/Shaders/Include/clear_common.hlsli"
#include "Core/Engine/Shaders/Include/gbuffer_common.hlsli"
#include "Core/Engine/Shaders/Include/debug_common.hlsli"

#include "Core/Engine/Vendor/imgui/imgui.h"
#include "Core/Engine/Vendor/imgui/imgui_impl_win32.h"
#include "Core/Engine/Vendor/imgui/imgui_impl_dx12.h"

Renderer g_Renderer;
RenderHandlerState g_RenderHandlerState;

UnifiedGeometryBuffer g_UnifiedGeometryBuffer;
ShaderManager*  g_ShaderManager           = nullptr;
DescriptorPool* g_DescriptorCbvSrvUavPool = nullptr;

void
init_shader_manager(const GpuDevice* device)
{
  if (g_ShaderManager == nullptr)
  {
    g_ShaderManager = HEAP_ALLOC(ShaderManager, g_InitHeap, 1);
  }

  for (u32 i = 0; i < kEngineShaderCount; i++)
  {
    g_ShaderManager->shaders[i] = load_shader_from_memory(device, kEngineShaderBinSrcs[i], kEngineShaderBinSizes[i]);
  }
}

void
destroy_shader_manager()
{
  for (u32 i = 0; i < kEngineShaderCount; i++)
  {
    destroy_shader(&g_ShaderManager->shaders[i]);
  }

  zero_memory(g_ShaderManager, sizeof(ShaderManager));
}

void
reload_engine_shader(const char* entry_point_name, const u8* bin, u64 bin_size)
{
  for (u32 i = 0; i < kEngineShaderCount; i++)
  {
    if (_stricmp(entry_point_name, kEngineShaderNames[i]) == 0)
    {
      wait_for_gpu_device_idle(g_GpuDevice);
      reload_shader_from_memory(g_ShaderManager->shaders + i, bin, bin_size);
      dbgln("Hot reloaded shader %s", entry_point_name);
      return;
    }
  }
  ASSERT_MSG(false, "Failed to reload engine shader %s, not found!", entry_point_name);
}

const GpuShader*
get_engine_shader(u32 index)
{
  ASSERT(g_ShaderManager != nullptr);
  ASSERT(index < kEngineShaderCount);
  return &g_ShaderManager->shaders[index];
}

RenderBuffers
init_render_buffers(
  const SwapChain* swap_chain
) {
  ScratchAllocator scratch_arena = alloc_scratch_arena();
  defer { free_scratch_arena(&scratch_arena); };

  RenderBuffers ret = {0};

  static constexpr u32 kRenderBufferReadbackSize = KiB(8);
  static constexpr u32 kRenderBufferUploadSize   = MiB(32);

  GpuLinearAllocator upload_heap   = init_gpu_linear_allocator(kRenderBufferUploadSize,   kGpuHeapSysRAMCpuToGpu);
  GpuLinearAllocator readback_heap = init_gpu_linear_allocator(kRenderBufferReadbackSize, kGpuHeapSysRAMGpuToCpu);

  DescriptorLinearAllocator rtv_descriptor_heap = init_descriptor_linear_allocator(g_GpuDevice, 64, kDescriptorHeapTypeRtv);
  DescriptorLinearAllocator dsv_descriptor_heap = init_descriptor_linear_allocator(g_GpuDevice, 16, kDescriptorHeapTypeDsv);

  struct SizeAndLifetime
  {
    u32 size           = 0;
    u32 offset         = 0;
    u32 alignment      = 0;
    u32 lifetime_start = 0;
    u32 lifetime_end   = 0;
    u32 buffer_idx     = 0;
  };

  struct BufferArgs
  {
    const char*      name            = nullptr;
    u32              size            = 0;
    GpuBuffer*       dst             = nullptr;
    GpuBuffer*       dst_counter     = nullptr;
    GpuBufferSrvDesc srv_desc;
    GpuDescriptor*   dst_srv         = nullptr;
    GpuBufferUavDesc uav_desc;
    GpuDescriptor*   dst_uav         = nullptr;
    GpuBufferUavDesc counter_uav_desc;
    GpuDescriptor*   dst_counter_uav = nullptr;
    RenderLayer      lifetime_start  = kRenderLayerInit;
    RenderLayer      lifetime_end    = kRenderLayerInit;
  };

  struct TextureArgs
  {
    const char*       name           = nullptr;
    GpuTextureDesc    desc;
    GpuTexture*       dst            = nullptr;
    GpuTextureSrvDesc srv_desc;
    GpuDescriptor*    dst_srv        = nullptr;
    GpuTextureUavDesc uav_desc;
    GpuDescriptor*    dst_uav        = nullptr;
    GpuDescriptor*    dst_rtv        = nullptr;
    GpuDescriptor*    dst_dsv        = nullptr;
    RenderLayer       lifetime_start = kRenderLayerInit;
    RenderLayer       lifetime_end   = kRenderLayerInit;
  };

  struct RenderBufferCreateArgs
  {
    union
    {
      BufferArgs  buffer_args;
      TextureArgs texture_args;
    };

    u64  offset = 0;
    u64  size   = 0;
    bool is_texture;
  };

  static constexpr u32 kMaxRenderBuffers = 1024;
  auto* args_start = HEAP_ALLOC(RenderBufferCreateArgs, scratch_arena, kMaxRenderBuffers);
  auto* dst_args   = args_start;


  // Lifetime is inclusive so [begin_lifetime, end_lifetime]
  //
  auto alloc_render_target = [&](RenderTarget* dst, const char* name, u32 width, u32 height, GpuFormat format, RenderLayer begin_lifetime = kRenderLayerInit, RenderLayer end_lifetime = kRenderLayerSubmit)
  {
    // TODO(bshihabi): We should alias the textures using these, but that requires graph coloring which I'm not totally sure how we should do
    GpuTextureDesc desc = {0};
    desc.width             = width;
    desc.height            = height;
    desc.format            = format;
    desc.array_size        = 1;
    desc.mip_levels        = 1;
    desc.flags             = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    desc.color_clear_value = Vec4(0.0f, 0.0f, 0.0f, 1.0f);

    GpuTextureSrvDesc srv_desc = {0};
    srv_desc.format            = desc.format;

    GpuTextureUavDesc uav_desc = {0};
    uav_desc.format            = desc.format;

    auto* args = ALLOC_OFF(dst_args, sizeof(RenderBufferCreateArgs));
    zero_struct(args);
    args->is_texture            = true;
    args->texture_args.name     = name;
    args->texture_args.desc     = desc;
    args->texture_args.dst      = &dst->texture;
    args->texture_args.dst_rtv  = &dst->rtv;
    args->texture_args.srv_desc = srv_desc;
    args->texture_args.dst_srv  = &dst->srv;
    args->texture_args.uav_desc = uav_desc;
    args->texture_args.dst_uav  = &dst->uav;

    args->texture_args.lifetime_start = begin_lifetime;
    args->texture_args.lifetime_end   = end_lifetime;
  };

  auto alloc_back_buffer_rtv = [&](RenderTarget* dst, u32 idx)
  {
    dst->texture = *swap_chain->back_buffers[idx];
  };

  auto alloc_temporal_render_target = [&](TemporalResource<RenderTarget>* dst, const char* name, u32 width, u32 height, GpuFormat format)
  {
    for (u32 iframe = 0; iframe < kFramesInFlight; iframe++)
    {
      alloc_render_target(dst->m_Resource + iframe, name, width, height, format);
    }
  };

  auto alloc_depth_target = [&](DepthTarget* dst, const char* name, u32 width, u32 height, GpuFormat format, RenderLayer begin_lifetime = kRenderLayerInit, RenderLayer end_lifetime = kRenderLayerSubmit)
  {
    GpuTextureDesc desc = {0};
    desc.width             = width;
    desc.height            = height;
    desc.format            = format;
    desc.array_size        = 1;
    desc.mip_levels        = 1;
    desc.flags             = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    // TODO(bshihabi): If there ever becomes a need, there's no reason that this can't be set by the caller. I'm just putting this
    // as a reasonable default because I can't imagine that we're ever going to use a non reverse-z depth buffer
    desc.depth_clear_value = 0;

    GpuTextureSrvDesc srv_desc = {0};
    srv_desc.format            = desc.format;

    auto* args = ALLOC_OFF(dst_args, sizeof(RenderBufferCreateArgs));
    zero_struct(args);
    args->is_texture            = true;
    args->texture_args.name     = name;
    args->texture_args.desc     = desc;
    args->texture_args.dst      = &dst->texture;
    args->texture_args.dst_dsv  = &dst->dsv;
    args->texture_args.srv_desc = srv_desc;
    args->texture_args.dst_srv  = &dst->srv;

    args->texture_args.lifetime_start = begin_lifetime;
    args->texture_args.lifetime_end   = end_lifetime;
  };

  auto alloc_scratch_texture = [&]<typename T>(Texture2D<T>* dst, const char* name, u32 width, u32 height, RenderLayer begin_lifetime = kRenderLayerInit, RenderLayer end_lifetime = kRenderLayerSubmit, u8 mipmap_count = 1)
  {
    GpuTextureDesc desc = {0};
    desc.width          = width;
    desc.height         = height;
    desc.format         = gpu_format_from_type<T>();
    desc.array_size     = 1;
    desc.mip_levels     = mipmap_count;
    desc.flags          = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    GpuTextureSrvDesc srv_desc = {0};
    srv_desc.format            = desc.format;
    GpuTextureUavDesc uav_desc = {0};
    uav_desc.format            = desc.format;

    auto* args = ALLOC_OFF(dst_args, sizeof(RenderBufferCreateArgs));
    zero_struct(args);
    args->is_texture            = true;
    args->texture_args.name     = name;
    args->texture_args.desc     = desc;
    args->texture_args.dst      = &dst->texture;
    args->texture_args.srv_desc = srv_desc;
    args->texture_args.dst_srv  = &dst->srv;
    args->texture_args.uav_desc = uav_desc;
    args->texture_args.dst_uav  = &dst->uav;

    args->texture_args.lifetime_start = begin_lifetime;
    args->texture_args.lifetime_end   = end_lifetime;
  };

  auto alloc_temporal_scratch_texture = [&]<typename T>(TemporalResource<Texture2D<T>>* dst, const char* name, u32 width, u32 height, u8 mipmap_count = 1)
  {
    for (u32 iframe = 0; iframe < kFramesInFlight; iframe++)
    {
      alloc_scratch_texture(dst->m_Resource + iframe, name, width, height, kRenderLayerInit, kRenderLayerSubmit, mipmap_count);
    }
  };

  auto alloc_scratch_texture_array = [&]<typename T>(Texture2DArray<T>* dst, const char* name, u32 width, u32 height, u32 array_size, RenderLayer begin_lifetime = kRenderLayerInit, RenderLayer end_lifetime = kRenderLayerSubmit)
  {
    GpuTextureDesc desc = {0};
    desc.width          = width;
    desc.height         = height;
    desc.format         = gpu_format_from_type<T>();
    desc.array_size     = (u16)array_size;
    desc.mip_levels     = 1;
    desc.flags          = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    GpuTextureSrvDesc srv_desc = {0};
    srv_desc.format            = desc.format;
    srv_desc.array_size        = array_size;

    GpuTextureUavDesc uav_desc = {0};
    uav_desc.format            = desc.format;
    uav_desc.array_size        = array_size;

    auto* args = ALLOC_OFF(dst_args, sizeof(RenderBufferCreateArgs));
    zero_struct(args);
    args->is_texture            = true;
    args->texture_args.name     = name;
    args->texture_args.desc     = desc;
    args->texture_args.dst      = &dst->texture;
    args->texture_args.srv_desc = srv_desc;
    args->texture_args.dst_srv  = &dst->srv;
    args->texture_args.uav_desc = uav_desc;
    args->texture_args.dst_uav  = &dst->uav;

    args->texture_args.lifetime_start = begin_lifetime;
    args->texture_args.lifetime_end   = end_lifetime;
  };

  auto alloc_temporal_scratch_texture_array = [&alloc_scratch_texture_array]<typename T>(TemporalResource<Texture2DArray<T>>* dst, const char* name, u32 width, u32 height, u32 array_size)
  {
    for (u32 iframe = 0; iframe < kFramesInFlight; iframe++)
    {
      alloc_scratch_texture_array(dst->m_Resource + iframe, name, width, height, array_size);
    }
  };

  auto alloc_scratch_buffer = [&](GpuBuffer* dst, const char* name, u32 size, RenderLayer begin_lifetime = kRenderLayerInit, RenderLayer end_lifetime = kRenderLayerSubmit)
  {
    auto* args = ALLOC_OFF(dst_args, sizeof(RenderBufferCreateArgs));
    zero_struct(args);
    args->is_texture                 = false;
    args->buffer_args.name           = name;
    args->buffer_args.size           = size;
    args->buffer_args.dst            = dst;

    args->buffer_args.lifetime_start = begin_lifetime;
    args->buffer_args.lifetime_end   = end_lifetime;
  };

  auto alloc_structured_buffer = [&]<typename T>(StructuredBuffer<T>* dst, const char* name, u32 size, RenderLayer begin_lifetime = kRenderLayerInit, RenderLayer end_lifetime = kRenderLayerSubmit)
  {
    GpuBufferSrvDesc srv_desc = {0};
    srv_desc.first_element    = 0;
    srv_desc.num_elements     = size / sizeof(T);
    srv_desc.stride           = sizeof(T);
    srv_desc.format           = kGpuFormatUnknown;
    srv_desc.is_raw           = false;

    GpuBufferUavDesc uav_desc = {0};
    uav_desc.first_element    = 0;
    uav_desc.num_elements     = size / sizeof(T);
    uav_desc.stride           = sizeof(T);
    uav_desc.format           = kGpuFormatUnknown;
    uav_desc.counter_offset   = 0;
    uav_desc.is_raw           = false;

    auto* args = ALLOC_OFF(dst_args, sizeof(RenderBufferCreateArgs));
    zero_struct(args);
    args->is_texture                 = false;
    args->buffer_args.name           = name;
    args->buffer_args.size           = size;
    args->buffer_args.dst            = &dst->buffer;
    args->buffer_args.dst_srv        = &dst->srv;
    args->buffer_args.srv_desc       = srv_desc;
    args->buffer_args.dst_uav        = &dst->uav;
    args->buffer_args.uav_desc       = uav_desc;

    args->buffer_args.lifetime_start = begin_lifetime;
    args->buffer_args.lifetime_end   = end_lifetime;
  };

  auto alloc_append_structured_buffer = [&]<typename T>(AppendStructuredBuffer<T>* dst, const char* name, u32 size, RenderLayer begin_lifetime = kRenderLayerInit, RenderLayer end_lifetime = kRenderLayerSubmit)
  {
    GpuBufferSrvDesc srv_desc = {0};
    srv_desc.first_element    = 0;
    srv_desc.num_elements     = size / sizeof(T);
    srv_desc.stride           = sizeof(T);
    srv_desc.format           = kGpuFormatUnknown;
    srv_desc.is_raw           = false;

    GpuBufferUavDesc uav_desc = {0};
    uav_desc.first_element    = 0;
    uav_desc.num_elements     = size / sizeof(T);
    uav_desc.stride           = sizeof(T);
    uav_desc.format           = kGpuFormatUnknown;
    uav_desc.counter_offset   = 0;
    uav_desc.is_raw           = false;

    GpuBufferUavDesc counter_uav_desc = {0};
    counter_uav_desc.first_element    = 0;
    counter_uav_desc.num_elements     = 1;
    counter_uav_desc.stride           = sizeof(u32);
    counter_uav_desc.format           = kGpuFormatUnknown;
    counter_uav_desc.counter_offset   = 0;
    counter_uav_desc.is_raw           = false;

    auto* args = ALLOC_OFF(dst_args, sizeof(RenderBufferCreateArgs));
    zero_struct(args);
    args->is_texture                   = false;
    args->buffer_args.name             = name;
    args->buffer_args.size             = size;
    args->buffer_args.dst              = &dst->buffer;
    args->buffer_args.dst_counter      = &dst->counter;
    args->buffer_args.dst_srv          = &dst->srv;
    args->buffer_args.srv_desc         = srv_desc;
    args->buffer_args.dst_uav          = &dst->uav;
    args->buffer_args.uav_desc         = uav_desc;
    args->buffer_args.dst_counter_uav  = &dst->counter_uav;
    args->buffer_args.counter_uav_desc = counter_uav_desc;

    args->buffer_args.lifetime_start = begin_lifetime;
    args->buffer_args.lifetime_end   = end_lifetime;
  };

  auto alloc_upload_buffer   = [&upload_heap](TemporalResource<GpuBuffer>* dst, const char* name, u32 size)
  {
    for (u32 iframe = 0; iframe < kFramesInFlight; iframe++)
    {
      dst->m_Resource[iframe] = alloc_gpu_buffer(upload_heap, size, name);
    }
  };

  auto alloc_readback_buffer = [&readback_heap](GpuBuffer* dst, const char* name, u32 size)
  {
    *dst = alloc_gpu_buffer(readback_heap, size, name);
  };

  u32 w = swap_chain->width;
  u32 h = swap_chain->height;

  alloc_render_target                 (&ret.gbuffer.material_id,      "GBuffer Material ID",                w,                       h, kGpuFormatR32Uint,       kRenderLayerGBuffer,     kRenderLayerLighting);
  alloc_render_target                 (&ret.gbuffer.diffuse_metallic, "GBuffer Diffuse Metallic",           w,                       h, kGpuFormatRGBA8Unorm,    kRenderLayerGBuffer,     kRenderLayerLighting);
  alloc_render_target                 (&ret.gbuffer.normal_roughness, "GBuffer Normal Roughness",           w,                       h, kGpuFormatRGBA16Float,   kRenderLayerGBuffer,     kRenderLayerLighting);
  alloc_temporal_render_target        (&ret.gbuffer.velocity,         "GBuffer Velocity",                   w,                       h, kGpuFormatRG32Float);
  alloc_depth_target                  (&ret.gbuffer.depth,            "GBuffer Depth",                      w,                       h, kGpuFormatD32Float,      kRenderLayerGBuffer,     kRenderLayerPost);
  alloc_scratch_texture               (&ret.gbuffer.hzb,              "HZB",                      UCEIL_DIV(w, 4),         UCEIL_DIV(h, 4),                      kRenderLayerGBuffer,     kRenderLayerPost, kHZBMipCount);


  // Frame / misc
  // All of these must last the entire frame, however they are small.
  alloc_upload_buffer                 (&ret.viewport_buffer,          "Viewport Buffer",                    sizeof(ViewportGpu));
  alloc_upload_buffer                 (&ret.render_settings,          "Render Settings",                    sizeof(RenderSettingsGpu));
  alloc_scratch_buffer                (&ret.debug_draw_args_buffer,   "Debug Draw Args Buffer",             sizeof(MultiDrawIndirectArgs) * 2);
  alloc_structured_buffer             (&ret.debug_line_vert_buffer,   "Debug Lines Vertices Buffer",        sizeof(DebugLinePoint) * kDebugMaxVertices);
  alloc_structured_buffer             (&ret.debug_sdf_buffer,         "Debug SDF Buffer",                   sizeof(DebugSdf)       * kDebugMaxSdfs);

  // Scene                            
  alloc_structured_buffer             (&ret.scene_obj_buffer,         "Scene Object Buffer",                sizeof(SceneObjGpu) * kMaxSceneObjs);
  alloc_structured_buffer             (&ret.rt_obj_buffer,            "RT Object Buffer",                   sizeof(RtObjGpu)    * kMaxSceneObjs);
  alloc_append_structured_buffer      (&ret.rt_tlas_instances,        "TLAS Instance Buffer",               sizeof(D3D12RaytracingInstanceDesc) * kMaxSceneObjs, kRenderLayerInit,        kRenderLayerSubmit);

  GpuRtTlasSizeInfo rt_tlas_size_info = query_gpu_rt_tlas_size_info(kMaxSceneObjs);
  alloc_scratch_buffer                (&ret.tlas_scratch,             "TLAS Scratch Buffer",                rt_tlas_size_info.scratch_size);

  // GBuffer indirect
  alloc_append_structured_buffer      (&ret.indirect_args,            "GBuffer Indirect Args",              sizeof(MultiDrawIndirectIndexedArgs) * kMaxSceneObjs);
  alloc_structured_buffer             (&ret.scene_obj_gpu_ids,        "GBuffer Indirect Scene Obj GPU IDs", sizeof(u32) * kMaxSceneObjs);
  alloc_structured_buffer             (&ret.occlusion_results,        "GBuffer Occlusion Results",          sizeof(u64) * UCEIL_DIV(kMaxSceneObjs, 64));

  // Lighting / post-process          
  alloc_scratch_texture               (&ret.hdr,                      "HDR Buffer",                         w,                       h,                          kRenderLayerLighting,    kRenderLayerPost);
  alloc_temporal_render_target        (&ret.taa,                      "TAA Buffer",                         w,                       h, kGpuFormatRGBA16Float);
  alloc_scratch_texture               (&ret.coc_buffer,               "CoC Buffer",                         w / kDoFResolutionScale, h / kDoFResolutionScale,    kRenderLayerPost,        kRenderLayerPost);
  alloc_scratch_texture               (&ret.blur_buffer,              "Bokeh Blur Buffer",                  w / kDoFResolutionScale, h / kDoFResolutionScale,    kRenderLayerPost,        kRenderLayerPost);
  alloc_scratch_texture               (&ret.depth_of_field,           "Depth Of Field Buffer",              w,                       h,                          kRenderLayerPost,        kRenderLayerPost);
  alloc_render_target                 (&ret.tonemapped_buffer,        "Tone Mapping Buffer",                w,                       h, kGpuFormatRGBA16Float,   kRenderLayerPost,        kRenderLayerSubmit);

  // DDGI
  alloc_temporal_scratch_texture_array(&ret.probe_page_table, "RT Diffuse GI - Probe Page Table", kProbeCountPerClipmap.x, kProbeCountPerClipmap.z, kProbeCountPerClipmap.y * kProbeClipmapCount);
  alloc_structured_buffer             (&ret.probe_buffer,     "RT Diffuse GI - Luminance Probe Buffer",     sizeof(DiffuseGiProbe) * kProbeMaxActiveCount);
  alloc_structured_buffer             (&ret.ray_luminance,    "RT Diffuse GI - Ray Luminance Data",         sizeof(GiRayLuminance) * kProbeMaxRayCount,          kRenderLayerRtDiffuseGi, kRenderLayerRtDiffuseGi);


  u32 render_buffer_count = (u32)(dst_args - args_start);
  ASSERT_MSG_FATAL(render_buffer_count <= kMaxRenderBuffers, "Please bump kMaxRenderBuffers to something larger than %u", render_buffer_count);

  SizeAndLifetime* unallocated_resources  = HEAP_ALLOC(SizeAndLifetime, scratch_arena, render_buffer_count);
  SizeAndLifetime* allocated_resources    = HEAP_ALLOC(SizeAndLifetime, scratch_arena, render_buffer_count);
  u32              allocated_count        = 0;
  for (u32 ibuffer = 0; ibuffer < render_buffer_count; ibuffer++)
  {
    auto* args = args_start + ibuffer;
    unallocated_resources[ibuffer].buffer_idx       = ibuffer;
    if (args->is_texture)
    {
      u64 alignment = 0;
      unallocated_resources[ibuffer].size           = (u32)query_gpu_texture_size(args->texture_args.desc, &alignment);
      unallocated_resources[ibuffer].alignment      = (u32)alignment;
      unallocated_resources[ibuffer].lifetime_start = args->texture_args.lifetime_start;
      unallocated_resources[ibuffer].lifetime_end   = args->texture_args.lifetime_end;
    }
    else
    {
      unallocated_resources[ibuffer].size           = args->buffer_args.size;
      // For some reason _every_ resource has to be aligned to 64 KiB
      unallocated_resources[ibuffer].alignment      = KiB(64);
      unallocated_resources[ibuffer].lifetime_start = args->buffer_args.lifetime_start;
      unallocated_resources[ibuffer].lifetime_end   = args->buffer_args.lifetime_end;
    }
  }

  struct MemoryRegion
  {
    enum : u32
    {
      kStart = 0,
      kEnd   = 1,
    };
    u32 offset         = 0;
    u32 buffer_idx: 31 = 0;
    u32 type:        1 = kStart;
  };
  u32           total_size     = 0;
  MemoryRegion* memory_regions = HEAP_ALLOC(MemoryRegion, scratch_arena, render_buffer_count + 2);

  while (allocated_count < render_buffer_count)
  {
    // Sort all of the unallocated resources biggest to largest (already allocated resources get zeroed out and put at the end of the list on the next time around)
    radix_sort(unallocated_resources, render_buffer_count, sizeof(SizeAndLifetime), offsetof(SizeAndLifetime, size), kSortDecreasing);

    // Create a new bucket with the largest resource and mark it as allocated
    memcpy(allocated_resources + allocated_count, unallocated_resources + 0, sizeof(SizeAndLifetime));
    // Mark the bucket resource as allocated
    zero_struct(unallocated_resources + 0);

    // Initialize data about new bucket
    SizeAndLifetime* bucket_resource = allocated_resources + allocated_count;
    bucket_resource->offset          = ALIGN_UP(total_size, bucket_resource->alignment);
    total_size                       = bucket_resource->offset + bucket_resource->size;

    // Since we allocated the bucket resource increment the allocated count
    allocated_count++;

    // This is how many more resources we need to allocate
    u32 unallocated_count = render_buffer_count - allocated_count;

    for (u32 inext_resource = 1; inext_resource <= unallocated_count; inext_resource++)
    {
      // Get the next unallocated resource (largest)
      SizeAndLifetime* next_resource = unallocated_resources + inext_resource;

      auto lifetimes_intersect = [&](const SizeAndLifetime* a, const SizeAndLifetime* b) -> bool
      {
        return !(a->lifetime_end < b->lifetime_start || b->lifetime_end < a->lifetime_start);
      };

      // If the unallocated resource intersects with the lifetime of the bucket, then we obviously can't fit it in this bucket
      if (lifetimes_intersect(bucket_resource, next_resource))
      {
        continue;
      }
      
      // Go through every resource and add markers if the lifetimes intersect
      u32 bucket_resource_count = allocated_count - (u32)(bucket_resource - allocated_resources);
      // Initialize the memory region markers for the bucket
      memory_regions[0].offset     = bucket_resource->offset;
      memory_regions[0].buffer_idx = 0;
      memory_regions[0].type       = MemoryRegion::kEnd;
      memory_regions[1].offset     = bucket_resource->offset + bucket_resource->size;
      memory_regions[1].buffer_idx = 0;
      memory_regions[1].type       = MemoryRegion::kStart;
      MemoryRegion* dst_memory_regions = memory_regions + 2 /* 2 reserved for the bucket memory markers */;
      for (u32 iother = 1; iother < bucket_resource_count; iother++)
      {
        // Get the other resource
        SizeAndLifetime* other_resource = bucket_resource + iother;
        if (lifetimes_intersect(other_resource, next_resource))
        {
          MemoryRegion* start = ALLOC_OFF(dst_memory_regions, sizeof(MemoryRegion));
          start->offset       = other_resource->offset;
          start->type         = MemoryRegion::kStart;
          start->buffer_idx   = other_resource->buffer_idx;

          MemoryRegion* end   = ALLOC_OFF(dst_memory_regions, sizeof(MemoryRegion));
          end->offset         = other_resource->offset + other_resource->size;
          end->type           = MemoryRegion::kEnd;
          end->buffer_idx     = other_resource->buffer_idx;
        }
      }

      u32 memory_region_count = (u32)(dst_memory_regions - memory_regions);
      // Sort the memory regions
      radix_sort(memory_regions, memory_region_count, sizeof(MemoryRegion), offsetof(MemoryRegion, offset), kSortIncreasing);

      // Loop through the memory regions to find the best fit
      s32         overlap_count  = 0;
      u32         optimal_offset = 0;
      Option<u32> optimal_size   = None;
      for (u32 imemory_region = 0; imemory_region < memory_region_count - 1; imemory_region++)
      {
        MemoryRegion* curr = memory_regions + imemory_region;
        MemoryRegion* next = curr + 1;
        // Overlap count keeps track of how many start/end are encountered. When overlap_count = 0, that means that no resource
        // currently overlaps the region
        overlap_count += curr->type == MemoryRegion::kStart ? 1 : -1;
        overlap_count  = MAX(overlap_count, 0);

        bool can_alias = overlap_count == 0 && curr->type == MemoryRegion::kEnd && next->type == MemoryRegion::kStart;
        if (!can_alias)
        {
          continue;
        }

        u32 aliasable_size = next->offset - curr->offset;
        u32 base_offset    = curr->offset;
        u32 aligned_offset = ALIGN_UP(curr->offset, next_resource->alignment);
        u32 padding        = aligned_offset - base_offset;
        u32 padded_size    = next_resource->size + padding;
        // If the resource can't fit, skip it
        if (padded_size > aliasable_size)
        {
          continue;
        }

        // If the new region is bigger than one we found before, we should use the smaller one.
        if (aliasable_size >= unwrap_or(optimal_size, U32_MAX))
        {
          continue;
        }

        optimal_offset = aligned_offset;
        optimal_size   = aliasable_size;
      }

      // If we didn't find any aliasable regions, then we have to skip the resource and try again in another bucket
      if (!optimal_size)
      {
        continue;
      }

      // Update the offset
      next_resource->offset = optimal_offset;

      // Copy the resource to the allocated resources
      memcpy(allocated_resources + allocated_count, next_resource, sizeof(SizeAndLifetime));

      // Mark the resource as allocated
      zero_struct(next_resource);

      allocated_count++;
    }
  }

  total_size = ALIGN_POW2(total_size, KiB(64));
  
  u32 non_aliasable_size = rt_tlas_size_info.max_size + MiB(1);
  char fmt_total_size[512];
  bytes_to_readable_str(fmt_total_size, sizeof(fmt_total_size), total_size + non_aliasable_size);
  dbgln("Total memory for render buffers: %s", fmt_total_size);

  GpuPhysicalMemory  vram_memory           = alloc_gpu_physical_memory(total_size, kGpuHeapGpuOnly);
  GpuLinearAllocator vram_non_aliased_heap = init_gpu_linear_allocator(non_aliasable_size, kGpuHeapGpuOnly);

  // Flush all of the offsets to the arguments
  for (u32 ibuffer = 0; ibuffer < render_buffer_count; ibuffer++)
  {
    SizeAndLifetime*        info = allocated_resources + ibuffer;
    RenderBufferCreateArgs* args = args_start + info->buffer_idx;
    args->offset                 = info->offset;
    args->size                   = info->size;
  }

  // Debug: visual memory layout for aliasing inspection
  {
    radix_sort(allocated_resources, render_buffer_count, sizeof(SizeAndLifetime), offsetof(SizeAndLifetime, offset));

    static constexpr u32 kBarWidth = 64;
    char bar[kBarWidth + 3];

    dbgln("Render Buffer Memory Map");
    dbgln("%-44s  %-10s  %-12s  %-30s  Map", "Name", "Offset", "Size", "Lifetime");
    for (u32 ibuffer = 0; ibuffer < render_buffer_count; ibuffer++)
    {
      SizeAndLifetime*        info = allocated_resources + ibuffer;
      RenderBufferCreateArgs* args = args_start + info->buffer_idx;
      const char*             name = args->is_texture ? args->texture_args.name : args->buffer_args.name;

      u32 bar_start = (u32)((u64)info->offset * kBarWidth / total_size);
      u32 bar_end   = (u32)((u64)(info->offset + info->size) * kBarWidth / total_size);
      if (bar_end == bar_start) bar_end = bar_start + 1;

      bar[0] = '|';
      for (u32 i = 0; i < kBarWidth; i++)
        bar[1 + i] = (i >= bar_start && i < bar_end) ? '=' : ' ';
      bar[kBarWidth + 1] = '|';
      bar[kBarWidth + 2] = '\0';

      char fmt_size[64];
      bytes_to_readable_str(fmt_size, sizeof(fmt_size), info->size);

      dbgln("%-44s  0x%08X  %-12s  [%-12s -> %-12s]  %s",
        name,
        info->offset,
        fmt_size,
        kRenderLayerNames[info->lifetime_start],
        kRenderLayerNames[info->lifetime_end],
        bar);
    }
  }

  // Defer initialize all of the render buffers
  for (u32 ibuffer = 0; ibuffer < render_buffer_count; ibuffer++)
  {
    auto* args = args_start + ibuffer;
    GpuAllocation allocation;
    fill_gpu_physical_allocation_struct(&allocation, vram_memory, args->offset, args->size);
    if (args->is_texture)
    {
      auto* texture_args = &args->texture_args;

      *texture_args->dst      = init_gpu_texture(allocation, texture_args->desc, texture_args->name);
      if (texture_args->dst_rtv)
      {
        *texture_args->dst_rtv = alloc_descriptor(&rtv_descriptor_heap);
        init_rtv(texture_args->dst_rtv, texture_args->dst);
      }
      
      if (texture_args->dst_dsv)
      {
        *texture_args->dst_dsv = alloc_descriptor(&dsv_descriptor_heap);
        init_dsv(texture_args->dst_dsv, texture_args->dst);
      }

      if (texture_args->dst_srv)
      {
        *texture_args->dst_srv = alloc_descriptor(g_DescriptorCbvSrvUavPool);
        init_texture_srv(texture_args->dst_srv, texture_args->dst, texture_args->srv_desc);
      }

      if (texture_args->dst_uav)
      {
        *texture_args->dst_uav = alloc_descriptor(g_DescriptorCbvSrvUavPool);
        init_texture_uav(texture_args->dst_uav, texture_args->dst, texture_args->uav_desc);
      }
    }
    else
    {
      auto* buffer_args = &args->buffer_args;

      *buffer_args->dst = init_gpu_buffer(allocation, buffer_args->size, buffer_args->name);
      if (buffer_args->dst_counter)
      {
        // TODO(bshihabi): We should really group all of these counters into a single buffer
        *buffer_args->dst_counter = alloc_gpu_buffer(vram_non_aliased_heap, sizeof(u32), buffer_args->name);
      }
      if (buffer_args->dst_srv)
      {
        *buffer_args->dst_srv = alloc_descriptor(g_DescriptorCbvSrvUavPool);
        init_buffer_srv(buffer_args->dst_srv, buffer_args->dst, buffer_args->srv_desc);
      }

      if (buffer_args->dst_uav)
      {
        *buffer_args->dst_uav = alloc_descriptor(g_DescriptorCbvSrvUavPool);
        if (buffer_args->dst_counter)
        {
          init_buffer_counted_uav(buffer_args->dst_uav, buffer_args->dst, buffer_args->dst_counter, buffer_args->uav_desc);
        }
        else
        {
          init_buffer_uav(buffer_args->dst_uav, buffer_args->dst, buffer_args->uav_desc);
        }
      }

      if (buffer_args->dst_counter_uav)
      {
        *buffer_args->dst_counter_uav = alloc_descriptor(g_DescriptorCbvSrvUavPool);
        init_buffer_uav(buffer_args->dst_counter_uav, buffer_args->dst_counter, buffer_args->counter_uav_desc);
      }
    }
  }


  // Initialize HZB mip UAVs
  for (u32 imip = 0; imip < kHZBMipCount; imip++)
  {
    ret.gbuffer.hzb_mip_uavs[imip] = alloc_descriptor(g_DescriptorCbvSrvUavPool);
    GpuTextureUavDesc mip_uav_desc = {0};
    mip_uav_desc.format            = kGpuFormatR32Float;
    mip_uav_desc.mip_slice         = imip;
    init_texture_uav(&ret.gbuffer.hzb_mip_uavs[imip], &ret.gbuffer.hzb.texture, mip_uav_desc);
  }

  ret.upload_buffer = alloc_gpu_ring_buffer(g_InitHeap, upload_heap, MiB(30), "Upload Ring Buffer");
  ret.rt_tlas       = alloc_gpu_rt_tlas(vram_non_aliased_heap, kMaxSceneObjs, "RT TLAS");


  // Initialize all of the GRVs
  gpu_init_grv<ConstantBufferPtr<ViewportGpu>>(kViewportBufferSlot, ret.viewport_buffer);
  gpu_init_grv<ConstantBufferPtr<RenderSettingsGpu>>(kRenderSettingsSlot, ret.render_settings);
  gpu_init_grv<RWStructuredBufferPtr<MultiDrawIndirectArgs>>(kDebugArgsBufferSlot, ret.debug_draw_args_buffer);
  gpu_init_grv<RWStructuredBufferPtr<DebugLinePoint>>(kDebugVertexBufferSlot, ret.debug_line_vert_buffer.buffer);
  gpu_init_grv<RWStructuredBufferPtr<DebugSdf>>(kDebugSdfBufferSlot, ret.debug_sdf_buffer.buffer);

  gpu_init_grv<StructuredBufferPtr<SceneObjGpu>>(kSceneObjBufferSlot, ret.scene_obj_buffer.buffer);
  gpu_init_grv<StructuredBufferPtr<RtObjGpu>>(kRtObjBufferSlot, ret.rt_obj_buffer.buffer);
  gpu_init_grv<RaytracingAccelerationStructurePtr>(kRaytracingAccelerationStructureSlot, ret.rt_tlas);

  // Back buffer RTV gets initialized every frame
  ret.back_buffer_rtv = alloc_descriptor(&rtv_descriptor_heap);

  return ret;
}

#define REGISTER_COMPUTE_PSO(pso_library, name) pso_library->compute_psos[k##name] = init_compute_pipeline(g_GpuDevice, get_engine_shader(k##name), #name)

static void
init_engine_psos(EnginePSOLibrary* library, const SwapChain* swap_chain)
{
  REGISTER_COMPUTE_PSO(library, CS_ClearStructuredBuffer);
  REGISTER_COMPUTE_PSO(library, CS_RtDiffuseGiPageTableInit);
  REGISTER_COMPUTE_PSO(library, CS_RtDiffuseGiPageTableReproject);
  REGISTER_COMPUTE_PSO(library, CS_RtDiffuseGiTraceRays);
  REGISTER_COMPUTE_PSO(library, CS_RtDiffuseGiProbeBlend);
  REGISTER_COMPUTE_PSO(library, CS_DebugDrawInitMultiDrawIndirectArgs);
  REGISTER_COMPUTE_PSO(library, CS_DoFCoC);
  REGISTER_COMPUTE_PSO(library, CS_DoFBokehBlur);
  REGISTER_COMPUTE_PSO(library, CS_DoFComposite);
  REGISTER_COMPUTE_PSO(library, CS_MaterialUpload);
  REGISTER_COMPUTE_PSO(library, CS_GBufferFillMultiDrawIndirectArgsPhaseOne);
  REGISTER_COMPUTE_PSO(library, CS_GBufferFillMultiDrawIndirectArgsPhaseTwo);
  REGISTER_COMPUTE_PSO(library, CS_GenerateHZB);
  REGISTER_COMPUTE_PSO(library, CS_RtTlasFillInstances);
  REGISTER_COMPUTE_PSO(library, CS_StandardBrdf);
  REGISTER_COMPUTE_PSO(library, CS_TAA);
  REGISTER_COMPUTE_PSO(library, CS_TextureCopy);
  REGISTER_COMPUTE_PSO(library, CS_TextureDownsampleHalf);
  REGISTER_COMPUTE_PSO(library, CS_DebugNormals);
  REGISTER_COMPUTE_PSO(library, CS_DebugDepth);


  {
    GraphicsPipelineDesc fullscreen_pipeline_desc =
    {
      .vertex_shader = get_engine_shader(kVS_Fullscreen),
      .pixel_shader  = get_engine_shader(kPS_Fullscreen),
      .rtv_formats   = Span{swap_chain->format},
    };

    library->back_buffer_blit = init_graphics_pipeline(g_GpuDevice, fullscreen_pipeline_desc, "VS_Fullscreen__PS_Fullscreen");
  }

  {
    GraphicsPipelineDesc desc = 
    {
      .vertex_shader = get_engine_shader(kVS_DebugDrawLine),
      .pixel_shader  = get_engine_shader(kPS_DebugDrawLine),
      .rtv_formats   = Span{kGpuFormatRGBA16Float},
      .dsv_format    = kGpuFormatD32Float,
      .depth_func    = kDepthComparison,
      .topology      = kPrimitiveTopologyLine,
      .blend_enable  = true,
    };
    library->debug_draw_line_pso = init_graphics_pipeline(g_GpuDevice, desc, "Debug Line Draw");
  }

  {
    GraphicsPipelineDesc desc = 
    {
      .vertex_shader = get_engine_shader(kVS_DebugDrawSdf),
      .pixel_shader  = get_engine_shader(kPS_DebugDrawSdf),
      .rtv_formats   = Span{kGpuFormatRGBA16Float},
      .dsv_format    = kGpuFormatD32Float,
      .depth_func    = kDepthComparison,
      .blend_enable  = true,
    };
    library->debug_draw_sdf_pso  = init_graphics_pipeline(g_GpuDevice, desc, "Debug SDF Draw");
  }

  {
    GraphicsPipelineDesc gbuffer_static_desc =
    {
      .vertex_shader  = get_engine_shader(kVS_MultiDrawIndirectIndexed),
      .pixel_shader   = get_engine_shader(kPS_BasicNormalGloss),
      .rtv_formats    = kGBufferRenderTargetFormats,
      .dsv_format     = kGpuFormatD32Float,
      .depth_func     = kDepthComparison,
      .stencil_enable = false,
    };

    library->gbuffer_static = init_graphics_pipeline(g_GpuDevice, gbuffer_static_desc, "GBuffer Static");
  }

  {
    GraphicsPipelineDesc tonemapping_desc =
    {
      .vertex_shader = get_engine_shader(kVS_Fullscreen),
      .pixel_shader  = get_engine_shader(kPS_ToneMapping),
      .rtv_formats   = Span{kGpuFormatRGBA16Float},
    };

    library->tonemapping = init_graphics_pipeline(g_GpuDevice, tonemapping_desc, "Tone Mapping");
  }
}

void
init_renderer(
  const GpuDevice* device,
  const SwapChain* swap_chain,
  HWND window
) {
  zero_memory(&g_Renderer, sizeof(g_Renderer));

  g_DescriptorCbvSrvUavPool   = HEAP_ALLOC(DescriptorPool, g_InitHeap, 1);
  *g_DescriptorCbvSrvUavPool  = init_descriptor_pool(g_InitHeap, 2048, kDescriptorHeapTypeCbvSrvUav, kGrvTemporalCount * kBackBufferCount + kGrvCount);

  // const uint32_t kGraphMemory = MiB(32);
  // g_Renderer.graph_allocator  = init_linear_allocator(HEAP_ALLOC_ALIGNED(g_InitHeap, kGraphMemory, alignof(u64)), kGraphMemory);

  init_scene();

  g_RenderHandlerState.buffers = init_render_buffers(swap_chain);
  init_engine_psos(&g_Renderer.pso_library, swap_chain);
  // Initialize the first cmdbuffer
  g_RenderHandlerState.cmd_list = alloc_cmd_list(&g_GpuDevice->graphics_cmd_allocator);

  // init_renderer_dependency_graph(swap_chain, kRgDestroyAll);


  g_Renderer.imgui_descriptor_heap = init_descriptor_linear_allocator(device, 1, kDescriptorHeapTypeCbvSrvUav);
  init_imgui_ctx(device, kGpuFormatRGBA16Float, window, &g_Renderer.imgui_descriptor_heap);

  new (&g_Renderer.settings) RenderSettings();
}

void
render_handler_debug_buffers(const RenderEntry*, u32)
{
  const ViewCtx* view_ctx = &g_RenderHandlerState.main_view;
  RenderBuffers* buffers  = &g_RenderHandlerState.buffers;
  RenderTarget*  dst      = &buffers->tonemapped_buffer;
  gpu_texture_layout_transition(&g_RenderHandlerState.cmd_list, &dst->texture, kGpuTextureLayoutUnorderedAccess);

  u32 layer = g_RenderHandlerState.settings.debug_layer;

  switch (layer)
  {
    case kRenderDebugNormal:
    {
      RenderTarget* src = &buffers->gbuffer.normal_roughness;
      gpu_texture_layout_transition(&g_RenderHandlerState.cmd_list, &src->texture, kGpuTextureLayoutShaderResource);

      DebugNormalSrt srt;
      srt.gbuffer_normal_roughness = {src->srv.index};
      srt.dst                      = {dst->uav.index};
      gpu_bind_compute_pso(&g_RenderHandlerState.cmd_list, kCS_DebugNormals);
      gpu_bind_srt(&g_RenderHandlerState.cmd_list, srt);
      gpu_dispatch(&g_RenderHandlerState.cmd_list, UCEIL_DIV(view_ctx->width, 8), UCEIL_DIV(view_ctx->height, 8), 1);
    } break;
    case kRenderDebugDiffuse:
    {
      // Not currently implemented
    } break;
    case kRenderDebugDepth:
    {
      DepthTarget* src = &buffers->gbuffer.depth;
      gpu_texture_layout_transition(&g_RenderHandlerState.cmd_list, &src->texture, kGpuTextureLayoutShaderResource);

      DebugDepthSrt srt;
      srt.gbuffer_depth = {src->srv.index};
      srt.dst           = {dst->uav.index};
      gpu_bind_compute_pso(&g_RenderHandlerState.cmd_list, kCS_DebugDepth);
      gpu_bind_srt(&g_RenderHandlerState.cmd_list, srt);
      gpu_dispatch(&g_RenderHandlerState.cmd_list, UCEIL_DIV(view_ctx->width, 8), UCEIL_DIV(view_ctx->height, 8), 1);
    } break;
  }
}

static constexpr RenderHandler* kRenderHandlers[]
{
  &render_handler_frame_init,
  &render_handler_scene_upload,
  &render_handler_build_tlas,
  &render_handler_rt_diffuse_gi_init,
  &render_handler_gbuffer_generate_multidraw_args,
  &render_handler_gbuffer_opaque,
  &render_handler_generate_hzb,
  &render_handler_rt_diffuse_gi_trace_rays,
  &render_handler_rt_diffuse_gi_probe_blend,
  &render_handler_lighting,
  &render_handler_dof_generate_coc,
  &render_handler_dof_bokeh_blur,
  &render_handler_dof_composite,
  &render_handler_temporal_aa,
  &render_handler_tonemapping,
  &render_handler_debug_ui,
  &render_handler_debug_buffers,
  &render_handler_indirect_debug_draw,
  &render_handler_back_buffer_blit,
};
static_assert(ARRAY_LENGTH(kRenderHandlers) == kRenderHandlerCount, "Mismatched render handlers! Double check you added it correctly here (in the right order)");

static const char* kRenderHandlerNames[] =
{
  "FrameInit",
  "SceneUpload",
  "BuildTLAS",
  "RtDiffuseGiInit",
  "GBufferGenerateMultiDrawArgs",
  "GBufferOpaque",
  "GenerateHZB",
  "RtDiffuseGiTraceRays",
  "RtDiffuseGiProbeBlend",
  "Lighting",
  "DoFGenerateCoC",
  "DoFBokehBlur",
  "DoFComposite",
  "TemporalAA",
  "Tonemapping",
  "DebugUi",
  "DebugBuffers",
  "DebugDrawIndirect",
  "BackBufferBlit",
};
static_assert(ARRAY_LENGTH(kRenderHandlerNames) == kRenderHandlerCount, "Mismatched render handler names! Double check you added it correctly here (in the right order)");


static constexpr u32 kMaxRenderBatches = 2048;

static
Mat4 view_from_camera(Camera* camera)
{
  constexpr float kPitchLimit = kPI / 2.0f - 0.05f;
  camera->pitch = MIN(kPitchLimit, MAX(-kPitchLimit, camera->pitch));
  if (camera->yaw > kPI)
  {
    camera->yaw -= kPI * 2.0f;
  }
  else if (camera->yaw < -kPI)
  {
    camera->yaw += kPI * 2.0f;
  }

  f32 y = sinf(camera->pitch);
  f32 r = cosf(camera->pitch);
  f32 z = r * cosf(camera->yaw);
  f32 x = r * sinf(camera->yaw);

  Vec3 lookat = Vec3(x, y, z);
  return look_at_lh(camera->world_pos, lookat, Vec3(0.0f, 1.0f, 0.0f));
}

static Vec2
get_taa_jitter(u32 frame_id, u32 width, u32 height)
{
  static const Vec2 kHaltonSequence[] =
  {
    Vec2(0.500000f, 0.333333f),
    Vec2(0.250000f, 0.666667f),
    Vec2(0.750000f, 0.111111f),
    Vec2(0.125000f, 0.444444f),
    Vec2(0.625000f, 0.777778f),
    Vec2(0.375000f, 0.222222f),
    Vec2(0.875000f, 0.555556f),
    Vec2(0.062500f, 0.888889f),
    Vec2(0.562500f, 0.037037f),
    Vec2(0.312500f, 0.370370f),
    Vec2(0.812500f, 0.703704f),
    Vec2(0.187500f, 0.148148f),
    Vec2(0.687500f, 0.481481f),
    Vec2(0.437500f, 0.814815f),
    Vec2(0.937500f, 0.259259f),
    Vec2(0.031250f, 0.592593f),
  };

  u32  idx = frame_id % ARRAY_LENGTH(kHaltonSequence);
  Vec2 ret = kHaltonSequence[idx] - Vec2(0.5f, 0.5f);
  ret.x   /= (f32)width;
  ret.y   /= (f32)height;

  ret     *= 2.0f;
  return ret;
}

static void
gpu_bind_engine_defaults()
{
  set_descriptor_heaps(&g_RenderHandlerState.cmd_list, {g_DescriptorCbvSrvUavPool});

  set_graphics_root_signature(&g_RenderHandlerState.cmd_list);
  set_compute_root_signature(&g_RenderHandlerState.cmd_list);

  set_descriptor_table(&g_RenderHandlerState.cmd_list, g_DescriptorCbvSrvUavPool, (g_FrameId % kBackBufferCount) * kGrvTemporalCount, kGrvTemporalTableSlot);
  set_descriptor_table(&g_RenderHandlerState.cmd_list, g_DescriptorCbvSrvUavPool, kBackBufferCount * kGrvTemporalCount, kGrvTableSlot);

  gpu_ia_set_primitive_topology(&g_RenderHandlerState.cmd_list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

  // TODO(bshihabi): These REALLY don't belong here, they should be GRVs
  gpu_bind_root_srv(&g_RenderHandlerState.cmd_list, kIndexBufferSlot,  g_UnifiedGeometryBuffer.index_buffer);
  gpu_bind_root_srv(&g_RenderHandlerState.cmd_list, kVertexBufferSlot, g_UnifiedGeometryBuffer.vertex_buffer);
}

ViewCtx*
submit_scene(const SwapChain* swap_chain, GpuTexture* back_buffer)
{
  // Initialize the back buffer descriptor
  init_rtv(&g_RenderHandlerState.buffers.back_buffer_rtv, back_buffer);

  // Copy the render settings over
  memcpy(&g_RenderHandlerState.settings, &g_Renderer.settings, sizeof(g_RenderHandlerState.settings));
  // Copy the previous view context over
  memcpy(&g_RenderHandlerState.prev_main_view, &g_RenderHandlerState.main_view, sizeof(g_RenderHandlerState.prev_main_view));
  g_RenderHandlerState.frame_id = g_FrameId;

  ViewCtx* view_ctx            = &g_RenderHandlerState.main_view;
  view_ctx->width              = swap_chain->width;
  view_ctx->height             = swap_chain->height;
  view_ctx->camera             = *get_scene_camera();
  view_ctx->directional_light  = *get_scene_directional_light();
  view_ctx->proj               = perspective_infinite_reverse_lh(kPI / 4.0f, (f32)view_ctx->width / (f32)view_ctx->height, kZNear);
  view_ctx->view               = view_from_camera(&view_ctx->camera);
  view_ctx->view_proj          = view_ctx->proj * view_ctx->view;
  view_ctx->inverse_view_proj  = inverse_mat4(view_ctx->view_proj);
  view_ctx->taa_jitter         = get_taa_jitter(g_RenderHandlerState.frame_id, view_ctx->width, view_ctx->height);
  view_ctx->frustum            = frustum_from_view_projection(view_ctx->view_proj);

  view_ctx->render_batches     = HEAP_ALLOC(RenderBatch, g_FrameHeap, kMaxRenderBatches);
  view_ctx->render_batch_count = 0;

  // Submit frame init
  u32 sort_key = 0;
  submit_render_entry(view_ctx, kRenderLayerInit,   kRenderHandlerFrameInit,       sort_key++, nullptr);
  submit_render_entry(view_ctx, kRenderLayerInit,   kRenderHandlerSceneUpload,     sort_key++, nullptr);
  submit_render_entry(view_ctx, kRenderLayerInit,   kRenderHandlerBuildTlas,       sort_key++, nullptr);
  submit_render_entry(view_ctx, kRenderLayerInit,   kRenderHandlerRtDiffuseGiInit, sort_key++, nullptr);

  sort_key = 0;
  if (!g_RenderHandlerState.settings.disable_diffuse_gi)
  {
    submit_render_entry(view_ctx, kRenderLayerRtDiffuseGi, kRenderHandlerRtDiffuseGiTraceRays,        sort_key++, nullptr);
    submit_render_entry(view_ctx, kRenderLayerRtDiffuseGi, kRenderHandlerRtDiffuseGiProbeBlend,       sort_key++, nullptr);
  }

  // Two pass occlusion culling
  sort_key = 0;
  {
    auto* params_generate_multi_draw_args  = HEAP_ALLOC(GBufferGenerateMultiDrawArgsEntry, g_FrameHeap, 2);
    params_generate_multi_draw_args[0].phase = 0;
    params_generate_multi_draw_args[1].phase = 1;

    auto* params_gbuffer_opaque  = HEAP_ALLOC(GBufferOpaqueEntry, g_FrameHeap, 2);
    params_gbuffer_opaque[0].should_clear_targets = true;
    params_gbuffer_opaque[1].should_clear_targets = false;

    // Phase one
    submit_render_entry(view_ctx, kRenderLayerGBuffer, kRenderHandlerGBufferGenerateMultiDrawArgs, sort_key++, params_generate_multi_draw_args + 0);
    submit_render_entry(view_ctx, kRenderLayerGBuffer, kRenderHandlerGBufferOpaque,                sort_key++, params_gbuffer_opaque           + 0);

    // Generate HZB
    submit_render_entry(view_ctx, kRenderLayerGBuffer, kRenderHandlerGenerateHZB,                  sort_key++, nullptr);

    // Phase two
    submit_render_entry(view_ctx, kRenderLayerGBuffer, kRenderHandlerGBufferGenerateMultiDrawArgs, sort_key++, params_generate_multi_draw_args + 1);
    submit_render_entry(view_ctx, kRenderLayerGBuffer, kRenderHandlerGBufferOpaque,                sort_key++, params_gbuffer_opaque           + 1);

    // Generate HZB again
    submit_render_entry(view_ctx, kRenderLayerGBuffer, kRenderHandlerGenerateHZB,                  sort_key++, nullptr);
  }

  sort_key = 0;
  submit_render_entry(view_ctx, kRenderLayerLighting, kRenderHandlerLighting, sort_key++, nullptr);

  sort_key = 0;
  submit_render_entry(view_ctx, kRenderLayerPost,     kRenderHandlerTemporalAA,     sort_key++, nullptr);
  submit_render_entry(view_ctx, kRenderLayerPost,     kRenderHandlerDoFGenerateCoC, sort_key++, nullptr);
  submit_render_entry(view_ctx, kRenderLayerPost,     kRenderHandlerDoFBokehBlur,   sort_key++, nullptr);
  submit_render_entry(view_ctx, kRenderLayerPost,     kRenderHandlerDoFComposite,   sort_key++, nullptr);
  submit_render_entry(view_ctx, kRenderLayerPost,     kRenderHandlerTonemapping,    sort_key++, nullptr);

  sort_key = 0;
  if (g_RenderHandlerState.settings.enabled_debug_draw)
  {
    submit_render_entry(view_ctx, kRenderLayerDebugDraw, kRenderHandlerIndirectDebugDraw, sort_key++, nullptr);
  }

  sort_key = 0;
  if (g_RenderHandlerState.settings.debug_layer != kRenderDebugDefault)
  {
    submit_render_entry(view_ctx, kRenderLayerDebug, kRenderHandlerDebugBuffers, sort_key++, nullptr);
  }

  sort_key = 0;
  submit_render_entry(view_ctx, kRenderLayerUI, kRenderHandlerDebugUi, sort_key++, nullptr);

  sort_key = 0;
  {
    BlitEntry* blit = HEAP_ALLOC(BlitEntry, g_FrameHeap, 1);
    blit->back_buffer = back_buffer;
    blit->src         = &g_RenderHandlerState.buffers.tonemapped_buffer;
    submit_render_entry(view_ctx, kRenderLayerSubmit, kRenderHandlerBackBufferBlit,  sort_key++, blit);
  }

  // Sort the batches/entries
  radix_sort(view_ctx->render_batches, view_ctx->render_batch_count, sizeof(RenderBatch), offsetof(RenderBatch, layer));
  for (u32 ibatch = 0; ibatch < view_ctx->render_batch_count; ibatch++)
  {
    const RenderBatch* batch = view_ctx->render_batches + ibatch;
    radix_sort(batch->entries, batch->entry_count, sizeof(RenderEntry), offsetof(RenderEntry, sort_key));
  }

  return view_ctx;
}

void
submit_render_entry  (ViewCtx* view_ctx, RenderLayer layer, RenderHandlerId handler, u32 sort_key, void* data)
{
  RenderEntry* entry = HEAP_ALLOC(RenderEntry, g_FrameHeap, 1);
  entry->sort_key = sort_key;
  entry->handler  = handler;
  entry->data     = data;
  submit_render_entries(view_ctx, layer, entry, 1);
}

void
submit_render_entries(ViewCtx* view_ctx, RenderLayer layer, RenderEntry* entries, u32 count)
{
  ASSERT_MSG_FATAL(view_ctx->render_batch_count < kMaxRenderBatches, "Ran out of room for render batches at %llu! Bump kMaxRenderBatches", kMaxRenderBatches);
  if (view_ctx->render_batch_count >= kMaxRenderBatches)
  {
    return;
  }

  RenderBatch* dst = view_ctx->render_batches + view_ctx->render_batch_count++;
  dst->layer       = layer;
  dst->entry_count = count;
  dst->entries     = entries;
}


void
render_view_ctx(ViewCtx* view_ctx)
{
  gpu_flush_cmds();

  GPU_SCOPED_EVENT(PIX_COLOR_DEFAULT, &g_RenderHandlerState.cmd_list, "Frame %lu", g_RenderHandlerState.frame_id);

  begin_gpu_profiler_timestamp(&g_RenderHandlerState.cmd_list, kTotalFrameGpuMarker);
  defer { end_gpu_profiler_timestamp(&g_RenderHandlerState.cmd_list, kTotalFrameGpuMarker); };

  RenderLayer layer = kRenderLayerCount;
  for (u32 ibatch = 0; ibatch < view_ctx->render_batch_count; ibatch++)
  {
    const RenderBatch* batch       = view_ctx->render_batches + ibatch;
    if (batch->layer != layer)
    {
      if (layer < kRenderLayerCount)
      {
        GPU_END_EVENT(&g_RenderHandlerState.cmd_list, kRenderLayerNames[layer]);
      }

      layer = batch->layer;
      GPU_BEGIN_EVENT(PIX_COLOR_DEFAULT, &g_RenderHandlerState.cmd_list, kRenderLayerNames[layer]);
    }
    const RenderEntry* start_entry = batch->entries;
    const RenderEntry* end_entry   = start_entry;

    while (end_entry < batch->entries + batch->entry_count)
    {
      end_entry++;
      if (end_entry >= batch->entries + batch->entry_count || end_entry->handler != start_entry->handler)
      {
        gpu_bind_engine_defaults();

        // GPU_SCOPED_EVENT(PIX_COLOR_DEFAULT, &g_RenderHandlerState.cmd_list, kRenderHandlerNames[start_entry->handler]);
        (kRenderHandlers[start_entry->handler])(start_entry, (u32)(end_entry - start_entry));
        start_entry = end_entry;
      }
    }
  }

  if (layer < kRenderLayerCount)
  {
    GPU_END_EVENT(&g_RenderHandlerState.cmd_list, kRenderLayerNames[layer]);
  }
  gpu_flush_cmds();
}

void
renderer_on_resize(const SwapChain* swap_chain)
{
  UNREFERENCED_PARAMETER(swap_chain);
}

void
destroy_renderer()
{
  destroy_imgui_ctx();
  zero_memory(&g_Renderer, sizeof(g_Renderer));
}

void
init_unified_geometry_buffer(const GpuDevice* device)
{
  zero_memory(&g_UnifiedGeometryBuffer, sizeof(g_UnifiedGeometryBuffer));

  GpuBufferDesc vertex_uber_desc = {0};
  vertex_uber_desc.size = kVertexBufferSize;

  g_UnifiedGeometryBuffer.lock              = init_spin_lock();
  g_UnifiedGeometryBuffer.vertex_buffer     = alloc_gpu_buffer_no_heap(device, vertex_uber_desc, kGpuHeapGpuOnly, "Vertex Buffer");
  g_UnifiedGeometryBuffer.vertex_buffer_pos = 0;

  GpuBufferDesc index_uber_desc = {0};
  index_uber_desc.size = kIndexBufferSize;

  g_UnifiedGeometryBuffer.index_buffer     = alloc_gpu_buffer_no_heap(device, index_uber_desc, kGpuHeapGpuOnly, "Index Buffer");
  g_UnifiedGeometryBuffer.index_buffer_pos = 0;

  g_UnifiedGeometryBuffer.blas_allocator   = init_gpu_linear_allocator(MiB(256), kGpuHeapGpuOnly);
}

void
destroy_unified_geometry_buffer()
{
  free_gpu_buffer(&g_UnifiedGeometryBuffer.vertex_buffer);
  free_gpu_buffer(&g_UnifiedGeometryBuffer.index_buffer);

  zero_memory(&g_UnifiedGeometryBuffer, sizeof(g_UnifiedGeometryBuffer));
}

u64
alloc_uber_vertex(u64 size)
{
  spin_acquire(&g_UnifiedGeometryBuffer.lock);
  defer { spin_release(&g_UnifiedGeometryBuffer.lock); };

  u64 ret      = g_UnifiedGeometryBuffer.vertex_buffer_pos;
  u64 capacity = g_UnifiedGeometryBuffer.vertex_buffer.desc.size;
  ASSERT_MSG_FATAL(ret + size <= capacity, "Failed to allocate %llu bytes from uber vertex buffer which already has %llu/%llu bytes allocated (%f %%). Consider bumping kVertexBufferSize.", size, ret, capacity, ((f64)ret / (f64)capacity * 100.0));
  g_UnifiedGeometryBuffer.vertex_buffer_pos += size;
  return ret;
}

u64
alloc_uber_index(u64 size)
{
  spin_acquire(&g_UnifiedGeometryBuffer.lock);
  defer { spin_release(&g_UnifiedGeometryBuffer.lock); };

  u64 ret      = g_UnifiedGeometryBuffer.index_buffer_pos;
  u64 capacity = g_UnifiedGeometryBuffer.index_buffer.desc.size;
  ASSERT_MSG_FATAL(ret + size <= capacity, "Failed to allocate %llu bytes from uber index buffer which already has %llu/%llu bytes allocated (%f %%). Consider bumping kIndexBufferSize.", size, ret, capacity, ((f64)ret / (f64)capacity * 100.0));
  g_UnifiedGeometryBuffer.index_buffer_pos += size;
  return ret;
}

GpuRtBlas
alloc_uber_blas(u32 vertex_start, u32 vertex_count, u32 index_start, u32 index_count, const char* name)
{
  spin_acquire(&g_UnifiedGeometryBuffer.lock);
  defer { spin_release(&g_UnifiedGeometryBuffer.lock); };

  GpuRtBlasDesc desc;
  desc.vertex_start  = vertex_start;
  desc.vertex_count  = vertex_count;
  // TODO(bshihabi): When we add vertex buffer compression we'll need to figure out how to construct the BLAS with the compressed data
  desc.vertex_format = kGpuFormatRGBA16Snorm;
  desc.vertex_stride = sizeof(Vertex);
  desc.index_start   = index_start;
  desc.index_count   = index_count;

  return alloc_gpu_rt_blas(g_UnifiedGeometryBuffer.blas_allocator, g_UnifiedGeometryBuffer.vertex_buffer, g_UnifiedGeometryBuffer.index_buffer, desc, name);
}

/////////// HELPER GPU FUNCTIONS /////////////
void
gpu_clear_render_target(CmdList* cmd, RenderTarget* rtv, const Vec4& clear_color)
{
  gpu_clear_render_target(cmd, &rtv->rtv, clear_color);
}

void
gpu_clear_depth_target(CmdList* cmd, DepthTarget* target, DepthStencilClearFlags flags, f32 depth, u8 stencil)
{
  gpu_clear_depth_stencil(cmd, &target->dsv, flags, depth, stencil);
}

void
gpu_bind_render_targets(CmdList* cmd, RenderTarget** rtvs, u32 rtv_count, DepthTarget* dsv)
{
  gpu_set_viewports(cmd, 0.0f, 0.0f, (f32)rtvs[0]->texture.desc.width, (f32)rtvs[0]->texture.desc.height);

  // Issue the layout transitions for the textures
  GpuDescriptor rtv_descriptors[kMaxRenderTargetCount];
  for (u32 irtv = 0; irtv < rtv_count; irtv++)
  {
    gpu_texture_layout_transition(cmd, &rtvs[irtv]->texture, kGpuTextureLayoutRenderTarget);
    rtv_descriptors[irtv] = rtvs[irtv]->rtv;
  }

  if (dsv != nullptr)
  {
    gpu_texture_layout_transition(cmd, &dsv->texture, kGpuTextureLayoutDepthStencil);
    gpu_bind_render_targets(cmd, rtv_descriptors, rtv_count, dsv->dsv);
  }
  else
  {
    gpu_bind_render_targets(cmd, rtv_descriptors, rtv_count, None);
  }
}

FenceValue
gpu_flush_cmds()
{
  FenceValue ret = submit_cmd_lists(&g_GpuDevice->graphics_cmd_allocator, {g_RenderHandlerState.cmd_list});
  gpu_ring_buffer_commit(&g_RenderHandlerState.buffers.upload_buffer, &g_GpuDevice->graphics_cmd_allocator);

  g_RenderHandlerState.cmd_list = alloc_cmd_list(&g_GpuDevice->graphics_cmd_allocator);

  gpu_bind_engine_defaults();

  return ret;
}

GpuStagingAllocation
gpu_alloc_staging_bytes(CmdList* cmd, u32 size, u32 alignment)
{
  UNREFERENCED_PARAMETER(cmd);
  GpuStagingAllocation ret;
  GpuRingBuffer* upload_buffer = &g_RenderHandlerState.buffers.upload_buffer;
  while (true)
  {
    Result<u64, FenceValue> res = gpu_ring_buffer_alloc(upload_buffer, size, alignment);
    if (res)
    {
      ret.gpu_offset = res.value();
      ret.cpu_dst    = (u8*)unwrap(upload_buffer->buffer.mapped) + ret.gpu_offset;
      return ret;
    }

    // Flush and wait for the GPU to catch up
    gpu_flush_cmds();
    gpu_ring_buffer_wait(upload_buffer, size);
  }
}

void
gpu_clear_buffer_u32(CmdList* cmd, RWStructuredBufferPtr<u32> dst, u32 count, u32 offset, u32 value)
{
  ClearStructuredBufferSrt srt;
  srt.clear_value = value;
  srt.count       = count;
  srt.offset      = offset;
  srt.dst         = dst;
  gpu_bind_compute_pso(cmd, g_Renderer.pso_library.compute_psos[kCS_ClearStructuredBuffer]);
  gpu_bind_srt(cmd, srt);
  gpu_dispatch(cmd, UCEIL_DIV(count, 64), 1, 1);
}


