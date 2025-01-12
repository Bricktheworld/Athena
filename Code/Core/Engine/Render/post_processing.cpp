#include "Core/Engine/memory.h"

#include "Core/Engine/Render/renderer.h"
#include "Core/Engine/Render/post_processing.h"
#include "Core/Engine/Shaders/interlop.hlsli"

struct ToneMappingParams
{
  RgTexture2D<float4> hdr_buffer;
  RgRtv               dst;

  GraphicsPSO         pso;
};

static void 
render_handler_tonemapping(RenderContext* ctx, const RenderSettings& settings, const void* data)
{
  const ToneMappingParams* params = (const ToneMappingParams*)data;
  ctx->clear_render_target_view(params->dst, Vec4(0.0f, 0.0f, 0.0f, 0.0f));

  ctx->om_set_render_targets({params->dst}, None);

  ToneMappingSrt srt;
  srt.texture     = params->hdr_buffer;
  srt.disable_hdr = settings.disable_hdr;

  ctx->graphics_bind_srt(srt);
  ctx->set_graphics_pso(&params->pso);

  ctx->draw_instanced(3, 1, 0, 0);
}

RgHandle<GpuTexture>
init_tonemapping(
  AllocHeap heap,
  RgBuilder* builder,
  RgHandle<GpuTexture> hdr_buffer
) {
  ToneMappingParams* params = HEAP_ALLOC(ToneMappingParams, g_InitHeap, 1);
  zero_memory(params, sizeof(ToneMappingParams));

  RgPassBuilder* pass      = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "Tone Mapping", params, &render_handler_tonemapping);

  RgHandle<GpuTexture> ret = rg_create_texture(builder, "Tone Mapping Buffer", FULL_RES(builder), kGpuFormatRGBA16Float);

  params->hdr_buffer       = RgTexture2D<float4>(pass, hdr_buffer);
  params->dst              = RgRtv(pass, &ret);

  GraphicsPipelineDesc desc = 
  {
    .vertex_shader = get_engine_shader(kVS_Fullscreen),
    .pixel_shader  = get_engine_shader(kPS_ToneMapping),
    .rtv_formats   = Span{kGpuFormatRGBA16Float},
  };
  params->pso              = init_graphics_pipeline(g_GpuDevice, desc, "Tone Mapping");

  return ret;
}

struct CoCGenerateParams
{
  RgTexture2D<float>    depth_buffer;
  RgRWTexture2D<float2> coc_buffer;
  ComputePSO            pso;
};

static void
render_handler_coc_generate(RenderContext* ctx, const RenderSettings& settings, const void* data)
{
  const CoCGenerateParams* params = (const CoCGenerateParams*)data;

  ctx->set_compute_pso(&params->pso);
  DofCocSrt srt;

  srt.coc_buffer   = params->coc_buffer;
  srt.depth_buffer = params->depth_buffer;

  srt.z_near       = kZNear;
  srt.aperture     = settings.aperture;
  srt.focal_dist   = settings.focal_dist;
  srt.focal_range  = settings.focal_range;

  ctx->compute_bind_srt(srt);
  ctx->dispatch(ctx->m_Width / 8, ctx->m_Height / 8, 1);
}

struct CoCDilateParams
{
  RgTexture2D<float2>   coc_buffer;
  RgRWTexture2D<float2> coc_dilate_buffer;

  ComputePSO            pso;
};

static void
render_handler_coc_dilate(RenderContext* ctx, const RenderSettings&, const void* data)
{
  const CoCDilateParams* params = (const CoCDilateParams*)data;

  ctx->set_compute_pso(&params->pso);

  DofCoCDilateSrt srt;
  srt.coc_buffer        = params->coc_buffer;
  srt.coc_dilate_buffer = params->coc_dilate_buffer;
  ctx->compute_bind_srt(srt);
  ctx->dispatch(ctx->m_Width / 8, ctx->m_Height / 8, 1);
}

struct DoFBlurParams
{
  RgTexture2D<float>    depth_buffer;
  RgTexture2D<float4>   hdr_buffer;
  RgTexture2D<float2>   coc_dilate_buffer;
  RgRWTexture2D<float4> blurred;

  ComputePSO            pso;
};

static void
render_handler_depth_of_field_bokeh_blur(RenderContext* ctx, const RenderSettings& settings, const void* data)
{
  const DoFBlurParams* params = (const DoFBlurParams*)data;

  ctx->set_compute_pso(&params->pso);

  DoFBokehBlurSrt srt;
  srt.depth_buffer      = params->depth_buffer;
  srt.coc_dilate_buffer = params->coc_dilate_buffer;
  srt.hdr_buffer        = params->hdr_buffer;
  srt.blurred           = params->blurred;

  srt.z_near            = kZNear;
  srt.blur_radius       = settings.dof_blur_radius;
  srt.sample_count      = settings.dof_sample_count;

  ctx->compute_bind_srt(srt);
  ctx->dispatch(ctx->m_Width / kDoFResolutionScale / 8, ctx->m_Height / kDoFResolutionScale / 8, 1);
}

struct DoFCompositeParams
{
  RgTexture2D<float2>   coc_dilate_buffer;
  RgTexture2D<float4>   hdr_buffer;
  RgTexture2D<float4>   near_blur_buffer;

  RgRWTexture2D<float4> render_target;

  ComputePSO            pso;
};

static void
render_handler_depth_of_field_composite(RenderContext* ctx, const RenderSettings&, const void* data)
{
  const DoFCompositeParams* params = (const DoFCompositeParams*)data;

  ctx->set_compute_pso(&params->pso);

  DoFCompositeSrt srt;
  srt.coc_dilate_buffer = params->coc_dilate_buffer;
  srt.hdr_buffer        = params->hdr_buffer;
  srt.near_blur_buffer  = params->near_blur_buffer;
  srt.render_target     = params->render_target;

  ctx->compute_bind_srt(srt);
  ctx->dispatch(ctx->m_Width / 8, ctx->m_Height / 8, 1);
}

RgHandle<GpuTexture> init_depth_of_field(
  AllocHeap heap,
  RgBuilder* builder,
  RgHandle<GpuTexture> depth_buffer,
  RgHandle<GpuTexture> taa_buffer
) {
  RgHandle<GpuTexture> coc_buffer        = rg_create_texture(builder, "CoC Buffer",             FULL_RES(builder), kGpuFormatRG16Float);
  RgHandle<GpuTexture> coc_dilate_buffer = rg_create_texture(builder, "CoC Dilate Buffer",      FULL_RES(builder), kGpuFormatRG16Float);
  RgHandle<GpuTexture> near_blur         = rg_create_texture(builder, "Bokeh Near Blur Buffer", FULL_RES(builder), kGpuFormatRGBA16Float);
  RgHandle<GpuTexture> depth_of_field    = rg_create_texture(builder, "Depth Of Field Buffer",  FULL_RES(builder), kGpuFormatRGBA16Float);

  {
    CoCGenerateParams* params = HEAP_ALLOC(CoCGenerateParams, g_InitHeap, 1);

    RgPassBuilder*     pass   = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "Depth of Field CoC Generate", params, &render_handler_coc_generate);
    params->depth_buffer      = RgTexture2D<float>   (pass, depth_buffer);
    params->coc_buffer        = RgRWTexture2D<float2>(pass, &coc_buffer);

    params->pso               = init_compute_pipeline(g_GpuDevice, get_engine_shader(kCS_DoFCoC), "Depth of Field CoC Generate");
  }

  {
    CoCDilateParams*   params = HEAP_ALLOC(CoCDilateParams, g_InitHeap, 1);

    RgPassBuilder*     pass   = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "Depth of Field CoC Dilate", params, &render_handler_coc_dilate);
    params->coc_buffer        = RgTexture2D  <float2>(pass, coc_buffer);
    params->coc_dilate_buffer = RgRWTexture2D<float2>(pass, &coc_dilate_buffer);

    params->pso               = init_compute_pipeline(g_GpuDevice, get_engine_shader(kCS_DoFCoCDilate), "Depth of Field Dilate");
  }

  {
    DoFBlurParams*     params = HEAP_ALLOC(DoFBlurParams, g_InitHeap, 1);

    RgPassBuilder*     pass   = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "Depth of Field Bokeh Blur", params, &render_handler_depth_of_field_bokeh_blur);
    params->depth_buffer      = RgTexture2D  <float> (pass, depth_buffer);
    params->coc_dilate_buffer = RgTexture2D  <float2>(pass, coc_dilate_buffer);
    params->hdr_buffer        = RgTexture2D  <float4>(pass, taa_buffer);
    params->blurred           = RgRWTexture2D<float4>(pass, &near_blur);

    params->pso               = init_compute_pipeline(g_GpuDevice, get_engine_shader(kCS_DoFBokehBlur), "Depth of Field Bokeh Blur");
  }

  {
    DoFCompositeParams* params = HEAP_ALLOC(DoFCompositeParams, g_InitHeap, 1);

    RgPassBuilder*     pass   = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "Depth of Field Composite", params, &render_handler_depth_of_field_composite);
    params->coc_dilate_buffer = RgTexture2D  <float2>(pass, coc_dilate_buffer);
    params->hdr_buffer        = RgTexture2D  <float4>(pass, taa_buffer);
    params->near_blur_buffer  = RgTexture2D  <float4>(pass, near_blur);
    params->render_target     = RgRWTexture2D<float4>(pass, &depth_of_field);

    params->pso               = init_compute_pipeline(g_GpuDevice, get_engine_shader(kCS_DoFComposite), "Depth of Field Composite");
  }

  return depth_of_field;
}

