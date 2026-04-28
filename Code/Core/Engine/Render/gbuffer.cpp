#include "Core/Foundation/math.h"

#include "Core/Engine/asset_streaming.h"
#include "Core/Engine/memory.h"
#include "Core/Engine/Render/renderer.h"
#include "Core/Engine/Render/gbuffer.h"
#include "Core/Engine/Render/render_graph.h"

#include "Core/Engine/Shaders/interlop.hlsli"
#include "Core/Engine/Shaders/Include/gbuffer_common.hlsli"


GBuffer
init_gbuffer(RgBuilder* builder)
{
  GBuffer ret = {0};
  ret.material_id      = rg_create_texture       (builder, "GBuffer Material ID",      FULL_RES(builder), kGpuFormatR32Uint               );
  ret.diffuse_metallic = rg_create_texture       (builder, "GBuffer Diffuse Metallic", FULL_RES(builder), kGpuFormatRGBA8Unorm            );
  ret.normal_roughness = rg_create_texture       (builder, "GBuffer Normal Roughness", FULL_RES(builder), kGpuFormatRGBA16Float           );
  ret.velocity         = rg_create_texture_ex    (builder, "GBuffer Velocity",         FULL_RES(builder), kGpuFormatRG32Float, 1, 1       );
  ret.depth            = rg_create_texture       (builder, "GBuffer Depth",            FULL_RES(builder), kGpuFormatD32Float              );
  ret.hzb              = rg_create_texture_mipped(builder, "HZB",                      QTR_RES (builder), kGpuFormatR32Float, kHZBMipCount);
  return ret;
}

struct GBufferFillIndirectArgsPhaseOneParams
{
  RgRWStructuredBufferCounted<MultiDrawIndirectIndexedArgs> multi_draw_args;
  RgRWStructuredBuffer<u32> scene_obj_gpu_ids;
  RgStructuredBuffer<u64>   scene_obj_occlusion;

  ComputePSO pso;
};

static void
render_handler_gbuffer_static_multi_draw_args_fill_phase_one(RenderContext* ctx, const RenderSettings&, const void* data)
{
  auto* params = (GBufferFillIndirectArgsPhaseOneParams*)data;

  u32 scene_obj_count = get_gpu_scene_obj_count();
  if (scene_obj_count == 0)
  {
    return;
  }

  ctx->clear_uav_u32(params->multi_draw_args.m_Counter, 0, 1);

  GBufferFillIndirectArgsPhaseOneSrt srt;
  srt.scene_obj_count     = scene_obj_count;
  srt.scene_obj_occlusion = params->scene_obj_occlusion;
  srt.scene_obj_gpu_ids   = params->scene_obj_gpu_ids;
  srt.multi_draw_args     = params->multi_draw_args;
  ctx->compute_bind_srt(srt);
  ctx->set_compute_pso(&params->pso);
  ctx->dispatch(UCEIL_DIV(scene_obj_count, 64), 1, 1);
}

struct HZBParams
{
  RgTexture2D<f32>   gbuffer_depth;
  RgRWTexture2D<f32> depth_mips[kHZBMipCount];

  ComputePSO pso;
};

static void
render_handler_generate_hzb(RenderContext* ctx, const RenderSettings& settings, const void* data)
{
  UNREFERENCED_PARAMETER(settings);
  UNREFERENCED_PARAMETER(ctx);
  HZBParams* params = (HZBParams*)data;

  GenerateHZBSrt srt;
  srt.gbuffer_depth = params->gbuffer_depth;
  srt.depth_mip0    = params->depth_mips[0];
  srt.depth_mip1    = params->depth_mips[1];
  srt.depth_mip2    = params->depth_mips[2];
  srt.depth_mip3    = params->depth_mips[3];
  ctx->compute_bind_srt(srt);
  ctx->set_compute_pso(&params->pso);
  ctx->dispatch(UCEIL_DIV(ctx->m_Width, 2 * kHZBDownsampleDimension), UCEIL_DIV(ctx->m_Height, 2 * kHZBDownsampleDimension), 1);

}

struct GBufferFillIndirectArgsPhaseTwoParams
{
  RgRWStructuredBufferCounted<MultiDrawIndirectIndexedArgs> multi_draw_args;
  RgRWStructuredBuffer<u32> scene_obj_gpu_ids;
  RgRWStructuredBuffer<u64> scene_obj_occlusion;
  RgTexture2D<f32> hzb;

  ComputePSO pso;
};

static void
render_handler_gbuffer_static_multi_draw_args_fill_phase_two(RenderContext* ctx, const RenderSettings&, const void* data)
{
  auto* params = (GBufferFillIndirectArgsPhaseTwoParams*)data;

  u32 scene_obj_count = get_gpu_scene_obj_count();
  if (scene_obj_count == 0)
  {
    return;
  }

  ctx->clear_uav_u32(params->multi_draw_args.m_Counter, 0, 1);

  GBufferFillIndirectArgsPhaseTwoSrt srt;
  srt.scene_obj_count     = scene_obj_count;
  srt.scene_obj_occlusion = params->scene_obj_occlusion;
  srt.scene_obj_gpu_ids   = params->scene_obj_gpu_ids;
  srt.multi_draw_args     = params->multi_draw_args;
  srt.hzb                 = params->hzb;
  ctx->compute_bind_srt(srt);
  ctx->set_compute_pso(&params->pso);
  ctx->dispatch(UCEIL_DIV(scene_obj_count, 64), 1, 1);
}

struct GBufferStaticParams
{
  RgIndirectArgsBuffer indirect_args;
  RgStructuredBuffer<u32> scene_obj_gpu_ids;

  GraphicsPSO gbuffer_pso;

  RgRtv material_id;
  RgRtv diffuse_metallic;
  RgRtv normal_roughness;
  RgRtv velocity;
  RgDsv depth;

  bool  should_clear = false;
};

static void
render_handler_gbuffer_static(RenderContext* ctx, const RenderSettings& settings, const void* data)
{
  UNREFERENCED_PARAMETER(settings);
  GBufferStaticParams* params = (GBufferStaticParams*)data;

  ctx->om_set_render_targets(
    {
      params->material_id,
      params->diffuse_metallic,
      params->normal_roughness,
      params->velocity
    },
    params->depth
  );

  ctx->rs_set_viewport(0.0f, 0.0f, (f32)ctx->m_Width, (f32)ctx->m_Height);
  ctx->rs_set_scissor_rect(0, 0, S32_MAX, S32_MAX);

  if (params->should_clear)
  {
    ctx->clear_render_target_view(params->material_id,      Vec4(0.0f));
    ctx->clear_render_target_view(params->diffuse_metallic, Vec4(0.0f));
    ctx->clear_render_target_view(params->normal_roughness, Vec4(0.0f));
    ctx->clear_render_target_view(params->velocity,         Vec4(0.0f));
    ctx->clear_depth_stencil_view(params->depth, kClearDepth, 0.0f, 0);
  }

  ctx->ia_set_primitive_topology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
  ctx->ia_set_index_buffer(&g_UnifiedGeometryBuffer.index_buffer, sizeof(u16));

  ctx->set_graphics_pso(&params->gbuffer_pso);
  GBufferIndirectSrt srt;
  srt.scene_obj_gpu_ids = params->scene_obj_gpu_ids;
  ctx->graphics_bind_srt(srt);
  ctx->multi_draw_indirect_indexed(params->indirect_args, 0, kMaxSceneObjs);
}

void
init_gbuffer_static(AllocHeap heap, RgBuilder* builder, GBuffer* gbuffer)
{

  // TODO(bshihabi): This should all be handled by asset streaming
  GraphicsPipelineDesc graphics_pipeline_desc =
  {
    .vertex_shader   = get_engine_shader(kVS_MultiDrawIndirectIndexed),
    .pixel_shader    = get_engine_shader(kPS_BasicNormalGloss),
    .rtv_formats     = kGBufferRenderTargetFormats,
    .dsv_format      = kGpuFormatD32Float,
    .depth_func      = kDepthComparison,
    .stencil_enable  = false,
  };

  GraphicsPSO gbuffer_pso = init_graphics_pipeline(g_GpuDevice, graphics_pipeline_desc, "GBuffer PSO");
  ComputePSO  hzb_pso     = init_compute_pipeline(g_GpuDevice, get_engine_shader(kCS_GenerateHZB), "HZB Generation");

  RgHandleCountedBuffer indirect_args     = rg_create_counted_buffer(builder, "GBuffer Indirect Args", sizeof(MultiDrawIndirectIndexedArgs) * kMaxSceneObjs);
  RgHandle<GpuBuffer>   scene_obj_gpu_ids = rg_create_buffer        (builder, "GBuffer Indirect Scene Obj GPU IDs", sizeof(u32) * kMaxSceneObjs);
  RgHandle<GpuBuffer>   occlusion_results = rg_create_buffer        (builder, "GBuffer Occlusion Results", sizeof(u64) * UCEIL_DIV(kMaxSceneObjs, 64));

  //////////// PHASE ONE ///////////
  // Init draw args
  {
    GBufferFillIndirectArgsPhaseOneParams* params = HEAP_ALLOC(GBufferFillIndirectArgsPhaseOneParams, g_InitHeap, 1);
    zero_memory(params, sizeof(GBufferFillIndirectArgsPhaseOneParams));

    RgPassBuilder* pass = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "GBuffer Static Fill Multidraw Args (Phase One)", params, &render_handler_gbuffer_static_multi_draw_args_fill_phase_one);
    params->multi_draw_args     = RgRWStructuredBufferCounted<MultiDrawIndirectIndexedArgs>(pass, &indirect_args);
    params->scene_obj_occlusion = RgStructuredBuffer<u64>  (pass, occlusion_results);
    params->scene_obj_gpu_ids   = RgRWStructuredBuffer<u32>(pass, &scene_obj_gpu_ids);
    params->pso                 = init_compute_pipeline(g_GpuDevice, get_engine_shader(kCS_GBufferFillMultiDrawIndirectArgsPhaseOne), "GBuffer Static Fill Multidraw Args (Phase One)");
  }

  {
    GBufferStaticParams* params   = HEAP_ALLOC(GBufferStaticParams, g_InitHeap, 1);
    zero_memory(params, sizeof(GBufferStaticParams));
    RgPassBuilder* pass = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "GBuffer Static (Phase One)", params, &render_handler_gbuffer_static);
    params->indirect_args         = RgIndirectArgsBuffer(pass, indirect_args);
    params->scene_obj_gpu_ids     = RgStructuredBuffer<u32>(pass, scene_obj_gpu_ids);
    params->material_id           = RgRtv(pass, &gbuffer->material_id);
    params->diffuse_metallic      = RgRtv(pass, &gbuffer->diffuse_metallic);
    params->normal_roughness      = RgRtv(pass, &gbuffer->normal_roughness);
    params->velocity              = RgRtv(pass, &gbuffer->velocity);
    params->depth                 = RgDsv(pass, &gbuffer->depth);

    params->gbuffer_pso           = gbuffer_pso;
    params->should_clear          = true;
  }

  // Generate HZB
  {
    HZBParams* params = HEAP_ALLOC(HZBParams, g_InitHeap, 1);
    zero_memory(params, sizeof(HZBParams));

    RgPassBuilder* pass = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "Generate HZB", params, &render_handler_generate_hzb);
    params->gbuffer_depth = RgTexture2D<f32>(pass, gbuffer->depth);
    for (u32 imip = 0; imip < kHZBMipCount; imip++)
    {
      GpuTextureUavDesc desc;
      desc.array_size = 0;
      desc.mip_slice  = imip;
      desc.format     = kGpuFormatR32Float;
      params->depth_mips[imip]  = RgRWTexture2D<f32>(pass, &gbuffer->hzb, desc);
    }
    params->pso = hzb_pso;
  }

  //////////// PHASE TWO ///////////
  // Init draw args
  {
    auto* params = HEAP_ALLOC(GBufferFillIndirectArgsPhaseTwoParams, g_InitHeap, 1);
    zero_struct(params);

    RgPassBuilder* pass = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "GBuffer Static Fill Multidraw Args (Phase Two)", params, &render_handler_gbuffer_static_multi_draw_args_fill_phase_two);
    params->multi_draw_args     = RgRWStructuredBufferCounted<MultiDrawIndirectIndexedArgs>(pass, &indirect_args);
    params->scene_obj_occlusion = RgRWStructuredBuffer<u64>(pass, &occlusion_results);
    params->scene_obj_gpu_ids   = RgRWStructuredBuffer<u32>(pass, &scene_obj_gpu_ids);
    params->hzb                 = RgTexture2D<f32>(pass, gbuffer->hzb);
    params->pso                 = init_compute_pipeline(g_GpuDevice, get_engine_shader(kCS_GBufferFillMultiDrawIndirectArgsPhaseTwo), "GBuffer Static Fill Multidraw Args (Phase Two)");
  }
  {
    GBufferStaticParams* params   = HEAP_ALLOC(GBufferStaticParams, g_InitHeap, 1);
    zero_memory(params, sizeof(GBufferStaticParams));
    RgPassBuilder* pass = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "GBuffer Static (Phase Two)", params, &render_handler_gbuffer_static);
    params->indirect_args         = RgIndirectArgsBuffer(pass, indirect_args);
    params->scene_obj_gpu_ids     = RgStructuredBuffer<u32>(pass, scene_obj_gpu_ids);
    params->material_id           = RgRtv(pass, &gbuffer->material_id);
    params->diffuse_metallic      = RgRtv(pass, &gbuffer->diffuse_metallic);
    params->normal_roughness      = RgRtv(pass, &gbuffer->normal_roughness);
    params->velocity              = RgRtv(pass, &gbuffer->velocity);
    params->depth                 = RgDsv(pass, &gbuffer->depth);

    params->gbuffer_pso           = gbuffer_pso;
    params->should_clear          = false;
  }

  // Generate HZB
  {
    HZBParams* params = HEAP_ALLOC(HZBParams, g_InitHeap, 1);
    zero_memory(params, sizeof(HZBParams));

    RgPassBuilder* pass = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "Generate HZB", params, &render_handler_generate_hzb);
    params->gbuffer_depth = RgTexture2D<f32>(pass, gbuffer->depth);
    for (u32 imip = 0; imip < kHZBMipCount; imip++)
    {
      GpuTextureUavDesc desc;
      desc.array_size = 0;
      desc.mip_slice  = imip;
      desc.format     = kGpuFormatR32Float;
      params->depth_mips[imip]  = RgRWTexture2D<f32>(pass, &gbuffer->hzb, desc);
    }
    params->pso = hzb_pso;
  }
}

ReadGBuffer
read_gbuffer(RgPassBuilder* pass_builder, const GBuffer& gbuffer)
{
  ReadGBuffer ret;
  ret.material_id      = RgTexture2D<uint>  (pass_builder, gbuffer.material_id);
  ret.diffuse_metallic = RgTexture2D<float4>(pass_builder, gbuffer.diffuse_metallic);
  ret.normal_roughness = RgTexture2D<float4>(pass_builder, gbuffer.normal_roughness);
  ret.velocity         = RgTexture2D<float2>(pass_builder, gbuffer.velocity);
  ret.depth            = RgTexture2D<float> (pass_builder, gbuffer.depth);

  return ret;
}
