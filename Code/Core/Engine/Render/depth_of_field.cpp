#include "Core/Engine/memory.h"

#include "Core/Engine/Render/renderer.h"
#include "Core/Engine/Render/depth_of_field.h"
#include "Core/Engine/Shaders/interlop.hlsli"

struct CoCGenerateParams
{
  RgTexture2D<float4>   hdr_buffer;
  RgTexture2D<float>    depth_buffer;
  RgRWTexture2D<float4> coc_buffer;
  ComputePSO            pso;
};

static void
render_handler_coc_generate(RenderContext* ctx, const RenderSettings& settings, const void* data)
{
  if (settings.disable_dof)
  {
    return;
  }

  const CoCGenerateParams* params = (const CoCGenerateParams*)data;

  ctx->set_compute_pso(&params->pso);
  DoFCocSrt srt;

  srt.coc_buffer   = params->coc_buffer;
  srt.hdr_buffer   = params->hdr_buffer;
  srt.depth_buffer = params->depth_buffer;

  srt.z_near       = kZNear;
  srt.aperture     = settings.aperture;
  srt.focal_dist   = settings.focal_dist;
  srt.focal_range  = settings.focal_range;

  ctx->compute_bind_srt(srt);
  ctx->dispatch(ctx->m_Width / 8, ctx->m_Height / 8, 1);
}

struct DoFBlurParams
{
  RgTexture2D<float>    depth_buffer;
  RgTexture2D<float4>   hdr_buffer;
  RgTexture2D<float4>   coc_buffer;
  RgRWTexture2D<float4> blurred;

  ComputePSO            pso;
};

static void
render_handler_depth_of_field_bokeh_blur(RenderContext* ctx, const RenderSettings& settings, const void* data)
{
  if (settings.disable_dof)
  {
    return;
  }

  const DoFBlurParams* params = (const DoFBlurParams*)data;

  ctx->set_compute_pso(&params->pso);

  DoFBokehBlurSrt srt;
  srt.depth_buffer = params->depth_buffer;
  srt.coc_buffer   = params->coc_buffer;
  srt.hdr_buffer   = params->hdr_buffer;
  srt.blur_buffer  = params->blurred;

  srt.z_near       = kZNear;
  srt.blur_radius  = settings.dof_blur_radius;
  srt.sample_count = settings.dof_sample_count;

  ctx->compute_bind_srt(srt);
  ctx->dispatch(ctx->m_Width / kDoFResolutionScale / 8, ctx->m_Height / kDoFResolutionScale / 8, 1);
}

struct DoFCompositeParams
{
  RgTexture2D<float4>   coc_buffer;
  RgTexture2D<float4>   hdr_buffer;
  RgTexture2D<float4>   blur_buffer;

  RgRWTexture2D<float4> render_target;

  ComputePSO            pso;
};

static void
render_handler_depth_of_field_composite(RenderContext* ctx, const RenderSettings& settings, const void* data)
{
  const DoFCompositeParams* params = (const DoFCompositeParams*)data;

  if (settings.disable_dof)
  {
    TextureCopySrt srt;
    srt.src = params->hdr_buffer;
    srt.dst = params->render_target;

    ctx->compute_bind_srt(srt);
    ctx->set_compute_pso(&g_Renderer.texture_copy_pso);
    ctx->dispatch(ctx->m_Width / 8, ctx->m_Height / 8, 1);
    return;
  }

  ctx->set_compute_pso(&params->pso);

  DoFCompositeSrt srt;
  srt.coc_buffer       = params->coc_buffer;
  srt.hdr_buffer       = params->hdr_buffer;
  srt.blur_buffer      = params->blur_buffer;
  srt.render_target    = params->render_target;

  ctx->compute_bind_srt(srt);
  ctx->dispatch(ctx->m_Width / 8, ctx->m_Height / 8, 1);
}

RgHandle<GpuTexture> init_depth_of_field(
  AllocHeap heap,
  RgBuilder* builder,
  RgHandle<GpuTexture> depth_buffer,
  RgHandle<GpuTexture> taa_buffer
) {
  RgHandle<GpuTexture> coc_buffer        = rg_create_texture(builder, "CoC Buffer",             VAR_RES (builder, kDoFResolutionScale), kGpuFormatRGBA16Float);
  RgHandle<GpuTexture> blur_buffer       = rg_create_texture(builder, "Bokeh Blur Buffer",      VAR_RES (builder, kDoFResolutionScale), kGpuFormatRGBA16Float);
  RgHandle<GpuTexture> depth_of_field    = rg_create_texture(builder, "Depth Of Field Buffer",  FULL_RES(builder),                      kGpuFormatRGBA16Float);

  {
    CoCGenerateParams* params = HEAP_ALLOC(CoCGenerateParams, g_InitHeap, 1);

    RgPassBuilder*     pass   = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "Depth of Field CoC Generate", params, &render_handler_coc_generate);
    params->depth_buffer      = RgTexture2D<float>  (pass, depth_buffer);
    params->hdr_buffer        = RgTexture2D<float4> (pass, taa_buffer);
    params->coc_buffer        = RgRWTexture2D<float4>(pass, &coc_buffer);

    params->pso               = init_compute_pipeline(g_GpuDevice, get_engine_shader(kCS_DoFCoC), "Depth of Field CoC Generate");
  }

  {
    DoFBlurParams*     params = HEAP_ALLOC(DoFBlurParams, g_InitHeap, 1);

    RgPassBuilder*     pass   = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "Depth of Field Bokeh Blur", params, &render_handler_depth_of_field_bokeh_blur);
    params->depth_buffer      = RgTexture2D  <float> (pass, depth_buffer);
    params->coc_buffer        = RgTexture2D  <float4>(pass, coc_buffer);
    params->hdr_buffer        = RgTexture2D  <float4>(pass, taa_buffer);
    params->blurred           = RgRWTexture2D<float4>(pass, &blur_buffer);

    params->pso               = init_compute_pipeline(g_GpuDevice, get_engine_shader(kCS_DoFBokehBlur), "Depth of Field Bokeh Blur");
  }

  {
    DoFCompositeParams* params = HEAP_ALLOC(DoFCompositeParams, g_InitHeap, 1);

    RgPassBuilder*     pass   = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "Depth of Field Composite", params, &render_handler_depth_of_field_composite);
    params->hdr_buffer        = RgTexture2D  <float4>(pass, taa_buffer);
    params->coc_buffer        = RgTexture2D  <float4>(pass, coc_buffer);
    params->blur_buffer       = RgTexture2D  <float4>(pass, blur_buffer);
    params->render_target     = RgRWTexture2D<float4>(pass, &depth_of_field);

    params->pso               = init_compute_pipeline(g_GpuDevice, get_engine_shader(kCS_DoFComposite), "Depth of Field Composite");
  }

  return depth_of_field;
}
