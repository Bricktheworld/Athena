#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "renderer.h"
#include "shaders/interlop.hlsli"
#include "vendor/ufbx/ufbx.h"
#include "vendor/imgui/imgui.h"
#include "vendor/imgui/imgui_impl_win32.h"
#include "vendor/imgui/imgui_impl_dx12.h"

using namespace gfx;
using namespace gfx::render;

ShaderManager
init_shader_manager(const gfx::GraphicsDevice* device)
{
  ShaderManager ret = {0};
  for (u32 i = 0; i < kShaderCount; i++)
  {
    const wchar_t* path = kShaderPaths[i];
    ret.shaders[i] = load_shader_from_file(device, path);
  }

  return ret;
}

void
destroy_shader_manager(ShaderManager* shader_manager)
{
  for (u32 i = 0; i < kShaderCount; i++)
  {
    destroy_shader(&shader_manager->shaders[i]);
  }

  zero_memory(shader_manager, sizeof(ShaderManager));
}

Renderer
init_renderer(MEMORY_ARENA_PARAM, const GraphicsDevice* device, const SwapChain* swap_chain, const ShaderManager& shader_manager, HWND window)
{
  Renderer ret = {0};
  ret.transient_resource_cache = init_transient_resource_cache(MEMORY_ARENA_FWD, device);

  GraphicsPipelineDesc fullscreen_pipeline_desc = 
  {
    .vertex_shader  = shader_manager.shaders[kVsFullscreen],
    .pixel_shader   = shader_manager.shaders[kPsFullscreen],
    .rtv_formats    = Span{swap_chain->format},
  };

  ret.fullscreen_pipeline = init_graphics_pipeline(device, fullscreen_pipeline_desc, "Fullscreen");

  GraphicsPipelineDesc post_pipeline_desc = 
  {
    .vertex_shader  = shader_manager.shaders[kVsFullscreen],
    .pixel_shader   = shader_manager.shaders[kPsPostProcessing],
    .rtv_formats    = Span{swap_chain->format},
  };
  ret.post_processing_pipeline = init_graphics_pipeline(device, post_pipeline_desc, "Post Processing");

  ret.standard_brdf_pipeline = init_compute_pipeline(device, shader_manager.shaders[kCsStandardBrdf], "Standard BRDF");
  ret.dof_coc_pipeline       = init_compute_pipeline(device, shader_manager.shaders[kCsDofCoC], "CoC");
  ret.dof_coc_dilate_pipeline = init_compute_pipeline(device, shader_manager.shaders[kCsDofCoCDilate], "CoC Dilate");
  ret.dof_blur_horiz_pipeline = init_compute_pipeline(device, shader_manager.shaders[kCsDofBlurHoriz], "DoF Blur Horiz");
  ret.dof_blur_vert_pipeline  = init_compute_pipeline(device, shader_manager.shaders[kCsDofBlurVert], "DoF Blur Vert");
  ret.dof_composite_pipeline  = init_compute_pipeline(device, shader_manager.shaders[kCsDofComposite], "DoF Composite");
  ret.debug_gbuffer_pipeline = init_compute_pipeline(device, shader_manager.shaders[kCsDebugGBuffer], "Debug GBuffer");
  ret.basic_rt_pipeline       = init_ray_tracing_pipeline(device, shader_manager.shaders[kRtBasic], "Basic RT");
  ret.basic_rt_shader_table  = init_shader_table(device, ret.basic_rt_pipeline, "Basic RT Shader Table");
  ret.standard_brdf_rt_pipeline = init_ray_tracing_pipeline(device, shader_manager.shaders[kRtStandardBrdf], "Standard Brdf RT");;
  ret.standard_brdf_rt_shader_table = init_shader_table(device, ret.standard_brdf_rt_pipeline, "Standard Brdf RT Shader Table");

  ret.probe_trace_rt_pipeline = init_ray_tracing_pipeline(device, shader_manager.shaders[kRtProbeTrace], "Probe Trace RT");;
  ret.probe_trace_rt_shader_table = init_shader_table(device, ret.probe_trace_rt_pipeline, "Probe Trace RT Shader Table");
  ret.probe_blending_cs_pipeline = init_compute_pipeline(device, shader_manager.shaders[kCsProbeBlending], "Probe Blending");
  ret.probe_distance_blending_cs_pipeline = init_compute_pipeline(device, shader_manager.shaders[kCsProbeDistanceBlending], "Probe Distance Blending");
  ret.debug_probe_cs_pipeline = init_compute_pipeline(device, shader_manager.shaders[kCsDebugProbe], "Probe Debug");

  ret.imgui_descriptor_heap = init_descriptor_linear_allocator(device, 1, kDescriptorHeapTypeCbvSrvUav);
  init_imgui_ctx(device, swap_chain, window, &ret.imgui_descriptor_heap);

#if 1
  ret.ddgi_vol_desc.origin = Vec4(0.0f, 4.5f, -0.3f, 0.0f);
  ret.ddgi_vol_desc.probe_spacing = Vec4(1.4f, 2.0f, 1.7f, 0.0f);
  ret.ddgi_vol_desc.probe_count_x = 19;
  ret.ddgi_vol_desc.probe_count_y = 5;
  ret.ddgi_vol_desc.probe_count_z = 8;
#else
  ret.ddgi_vol_desc.origin = Vec4(0.0f, 4.5f, 0.0f, 0.0f);
  ret.ddgi_vol_desc.probe_spacing = Vec4(1.0f, 2.0f, 1.0f, 0.0f);
  ret.ddgi_vol_desc.probe_count_x = 19;
  ret.ddgi_vol_desc.probe_count_y = 5;
  ret.ddgi_vol_desc.probe_count_z = 19;
#endif
  ret.ddgi_vol_desc.probe_num_rays = 128;
  ret.ddgi_vol_desc.probe_hysteresis = 0.97f;
  ret.ddgi_vol_desc.probe_max_ray_distance = 20.0f;

  GpuImageDesc probe_ray_data_desc = {0};
  probe_ray_data_desc.width             = ret.ddgi_vol_desc.probe_num_rays;
  probe_ray_data_desc.height            = ret.ddgi_vol_desc.probe_count_x * ret.ddgi_vol_desc.probe_count_z;
  probe_ray_data_desc.array_size        = ret.ddgi_vol_desc.probe_count_y;
  probe_ray_data_desc.format            = DXGI_FORMAT_R32G32B32A32_FLOAT;
  probe_ray_data_desc.flags             = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  probe_ray_data_desc.initial_state     = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  probe_ray_data_desc.color_clear_value = 0.0f;

  ret.probe_ray_data = alloc_gpu_image_2D_no_heap(device, probe_ray_data_desc, "Probe Ray Data");

  GpuImageDesc probe_irradiance_desc = {0};
  probe_irradiance_desc.width             = ret.ddgi_vol_desc.probe_count_x * kProbeNumIrradianceTexels;
  probe_irradiance_desc.height            = ret.ddgi_vol_desc.probe_count_z * kProbeNumIrradianceTexels;
  probe_irradiance_desc.array_size        = ret.ddgi_vol_desc.probe_count_y;
  probe_irradiance_desc.format            = DXGI_FORMAT_R32G32B32A32_FLOAT;
  probe_irradiance_desc.flags             = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  probe_irradiance_desc.initial_state     = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  probe_irradiance_desc.color_clear_value = 0.0f;
  ret.probe_irradiance = alloc_gpu_image_2D_no_heap(device, probe_irradiance_desc, "Probe Irradiance");

  GpuImageDesc probe_distance_desc = {0};
  probe_distance_desc.width             = ret.ddgi_vol_desc.probe_count_x * kProbeNumDistanceTexels;
  probe_distance_desc.height            = ret.ddgi_vol_desc.probe_count_z * kProbeNumDistanceTexels;
  probe_distance_desc.array_size        = ret.ddgi_vol_desc.probe_count_y;
  probe_distance_desc.format            = DXGI_FORMAT_R16_FLOAT;
  probe_distance_desc.flags             = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  probe_distance_desc.initial_state     = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  probe_distance_desc.color_clear_value = 0.0f;
  ret.probe_distance = alloc_gpu_image_2D_no_heap(device, probe_distance_desc, "Probe Distance");

  GpuImageDesc probe_offset_desc = {0};
  probe_offset_desc.width             = ret.ddgi_vol_desc.probe_count_x;
  probe_offset_desc.height            = ret.ddgi_vol_desc.probe_count_z;
  probe_offset_desc.array_size        = ret.ddgi_vol_desc.probe_count_y;
  probe_offset_desc.format            = DXGI_FORMAT_R11G11B10_FLOAT;
  probe_offset_desc.flags             = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  probe_offset_desc.initial_state     = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
  probe_offset_desc.color_clear_value = 0.0f;
  ret.probe_offset = alloc_gpu_image_2D_no_heap(device, probe_offset_desc, "Probe Offset");

  return ret;
}

void
destroy_renderer(Renderer* renderer)
{
  destroy_imgui_ctx();
//  destroy_compute_pipeline(&renderer->debug_gbuffer_pipeline);
  destroy_compute_pipeline(&renderer->standard_brdf_pipeline);
  destroy_graphics_pipeline(&renderer->fullscreen_pipeline);
  destroy_transient_resource_cache(&renderer->transient_resource_cache);
  zero_memory(renderer, sizeof(Renderer));
}

void
build_acceleration_structures(gfx::GraphicsDevice* device, Scene* scene)
{
  scene->bvh = init_acceleration_structure(device,
                                           scene->vertex_uber_buffer,
                                           scene->vertex_uber_buffer_offset,
                                           sizeof(interlop::Vertex),
                                           scene->index_uber_buffer,
                                           scene->index_uber_buffer_offset,
                                           "Scene Acceleration Structure");
}


void
begin_renderer_recording(MEMORY_ARENA_PARAM, Renderer* renderer)
{
  renderer->meshes = init_array<Mesh>(MEMORY_ARENA_FWD, 128);
}

void
submit_mesh(Renderer* renderer, Mesh mesh)
{
  *array_add(&renderer->meshes) = mesh;
}

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

static void
draw_debug()
{
}

void
execute_render(MEMORY_ARENA_PARAM,
               Renderer* renderer,
               const gfx::GraphicsDevice* device,
               gfx::SwapChain* swap_chain,
               Camera* camera,
               const gfx::GpuBuffer& vertex_buffer,
               const gfx::GpuBuffer& index_buffer,
               const gfx::GpuBvh& bvh,
               const RenderOptions& render_options,
               const interlop::DirectionalLight& directional_light)
{
  RenderGraph graph = init_render_graph(MEMORY_ARENA_FWD);

  Handle<GpuImage> render_buffers[RenderBuffers::kCount];

  for (u32 i = 0; i < RenderBuffers::kCount; i++)
  {
    RenderBufferDesc desc = kRenderBufferDescs[i];
    GpuImageDesc image_desc = {0};
    image_desc.width  = swap_chain->width  >> (u8)desc.res;
    image_desc.height = swap_chain->height >> (u8)desc.res;
    image_desc.format = desc.format;
    if (is_depth_format(desc.format))
    {
      image_desc.depth_clear_value = desc.depth_clear_value;
      image_desc.stencil_clear_value = desc.stencil_clear_value;
    }
    else
    {
      image_desc.color_clear_value = desc.color_clear_value;
    }
    render_buffers[i] = create_image(&graph, desc.debug_name, image_desc);
  }
#define RENDER_BUFFER(buf_id) render_buffers[RenderBuffers::##buf_id]

  interlop::Scene scene;
  scene.proj = perspective_infinite_reverse_lh(kPI / 4.0f, f32(swap_chain->width) / f32(swap_chain->height), kZNear);
  Mat4 view = view_from_camera(camera);
  scene.view_proj = scene.proj * view;
  scene.inverse_view_proj = inverse_mat4(scene.view_proj);
  scene.camera_world_pos = camera->world_pos;
  scene.directional_light = directional_light;

  Handle<GpuBuffer> scene_buffer = create_buffer(&graph, "Scene Buffer", scene);

  // Render GBuffers
  RenderPass* geometry_pass = add_render_pass(MEMORY_ARENA_FWD, &graph, kCmdQueueTypeGraphics, "Geometry Pass");

  for (u32 i = RenderBuffers::kGBufferMaterialId; i <= RenderBuffers::kGBufferNormalRGBRoughnessA; i++)
  {
    cmd_clear_render_target_view(geometry_pass, &render_buffers[i], Vec4(0.0f, 0.0f, 0.0f, 0.0f));
  }
  cmd_clear_depth_stencil_view(geometry_pass, &RENDER_BUFFER(kGBufferDepth), D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0);
  cmd_om_set_render_targets(geometry_pass, Span(&render_buffers[RenderBuffers::kGBufferMaterialId], kGBufferRTCount), RENDER_BUFFER(kGBufferDepth));

  interlop::Transform transform;
  transform.model = Mat4::columns(Vec4(1, 0, 0, 0),
                                  Vec4(0, 1, 0, 0),
                                  Vec4(0, 0, 1, 0),
                                  Vec4(0, 0, 10, 1));
  transform.model_inverse = transform_inverse_no_scale(transform.model);
  Handle<GpuBuffer> transform_buffer = create_buffer(&graph, "Transform Buffer", transform);


  Handle<GpuBuffer> graph_vertex_buffer = import_buffer(&graph, &vertex_buffer);
  cmd_ia_set_index_buffer(geometry_pass, &index_buffer);
  for (Mesh mesh : renderer->meshes)
  {
    cmd_set_graphics_pso(geometry_pass, &mesh.gbuffer_pso);
    cmd_graphics_bind_shader_resources<interlop::MaterialRenderResources>(geometry_pass, {.vertices = graph_vertex_buffer, .scene = scene_buffer, .transform = transform_buffer});
    cmd_draw_indexed_instanced(geometry_pass, mesh.index_count, 1, mesh.index_buffer_offset, 0, 0);
  }

  u32 fullres_dispatch_x = (swap_chain->width + 7)  / 8;
  u32 fullres_dispatch_y = (swap_chain->height + 7) / 8;

  u32 halfres_dispatch_x = ((swap_chain->width  >> 1) + 7) / 8;
  u32 halfres_dispatch_y = ((swap_chain->height >> 1) + 7) / 8;

  u32 quarterres_dispatch_x = ((swap_chain->width  >> 2) + 7) / 8;
  u32 quarterres_dispatch_y = ((swap_chain->height >> 2) + 7) / 8;

  u32 eighthres_dispatch_x = ((swap_chain->width  >> 3) + 7) / 8;
  u32 eighthres_dispatch_y = ((swap_chain->height >> 3) + 7) / 8;

  interlop::DDGIVolDesc* vol_desc = &renderer->ddgi_vol_desc;
  vol_desc->probe_ray_rotation = generate_random_rotation();
  Handle<GpuBuffer> vol_desc_buffer = create_buffer(&graph, "DDGI Volume Description Buffer", *vol_desc);
  Handle<GpuImage> probe_ray_data  = import_image(&graph, &renderer->probe_ray_data);
  Handle<GpuImage> probe_irradiance = import_image(&graph, &renderer->probe_irradiance);
  Handle<GpuImage> probe_distance   = import_image(&graph, &renderer->probe_distance);
  {
    RenderPass* probe_tracing_pass = add_render_pass(MEMORY_ARENA_FWD, &graph, kCmdQueueTypeGraphics, "Probe Tracing");

    interlop::ProbeTraceRTResources probe_trace_resources = 
    {
      .vol_desc = vol_desc_buffer,
      .scene = scene_buffer,
      .probe_irradiance = probe_irradiance,
      .probe_distance = probe_distance,
      .out_ray_data = probe_ray_data,
    };

    cmd_ray_tracing_bind_shader_resources(probe_tracing_pass, probe_trace_resources);
    cmd_set_ray_tracing_pso(probe_tracing_pass, &renderer->probe_trace_rt_pipeline);
    cmd_dispatch_rays(probe_tracing_pass,
                      &bvh,
                      &index_buffer,
                      &vertex_buffer,
                      renderer->probe_trace_rt_shader_table,
                      vol_desc->probe_num_rays,
                      vol_desc->probe_count_x * vol_desc->probe_count_z,
                      vol_desc->probe_count_y);
  }

  {
    RenderPass* probe_blending_pass = add_render_pass(MEMORY_ARENA_FWD, &graph, kCmdQueueTypeGraphics, "Probe Blending");
    interlop::ProbeBlendingCSResources probe_blend_resources = 
    {
      .vol_desc = vol_desc_buffer,
      .ray_data = probe_ray_data,
      .irradiance = probe_irradiance,
    };
    cmd_compute_bind_shader_resources(probe_blending_pass, probe_blend_resources);
    cmd_set_compute_pso(probe_blending_pass, &renderer->probe_blending_cs_pipeline);
    cmd_dispatch(probe_blending_pass, vol_desc->probe_count_x,
                                      vol_desc->probe_count_z,
                                      vol_desc->probe_count_y); 
  }

  {
    RenderPass* probe_distance_blending_pass = add_render_pass(MEMORY_ARENA_FWD, &graph, kCmdQueueTypeGraphics, "Probe Distance Blending");
    interlop::ProbeDistanceBlendingCSResources probe_blend_resources = 
    {
      .vol_desc = vol_desc_buffer,
      .ray_data = probe_ray_data,
      .distance = probe_distance,
    };
    cmd_compute_bind_shader_resources(probe_distance_blending_pass, probe_blend_resources);
    cmd_set_compute_pso(probe_distance_blending_pass, &renderer->probe_distance_blending_cs_pipeline);
    cmd_dispatch(probe_distance_blending_pass, vol_desc->probe_count_x,
                                               vol_desc->probe_count_z,
                                               vol_desc->probe_count_y); 
  }

  {
    RenderPass* standard_brdf_rt_pass = add_render_pass(MEMORY_ARENA_FWD, &graph, kCmdQueueTypeGraphics, "Standard Brdf RT");
    interlop::StandardBrdfRTResources standard_brdf_rt_resources = 
    {
      .scene                          = scene_buffer,
      .gbuffer_material_ids           = RENDER_BUFFER(kGBufferMaterialId),
      .gbuffer_world_pos              = RENDER_BUFFER(kGBufferWorldPos),
      .gbuffer_diffuse_rgb_metallic_a = RENDER_BUFFER(kGBufferDiffuseRGBMetallicA),
      .gbuffer_normal_rgb_roughness_a = RENDER_BUFFER(kGBufferNormalRGBRoughnessA),

      .vol_desc                       = vol_desc_buffer,
      .probe_irradiance               = probe_irradiance,
      .probe_distance                 = probe_distance,

      .render_target                  = RENDER_BUFFER(kHDRLighting),
    };
    cmd_ray_tracing_bind_shader_resources(standard_brdf_rt_pass, standard_brdf_rt_resources);
    cmd_set_ray_tracing_pso(standard_brdf_rt_pass, &renderer->standard_brdf_rt_pipeline);
    cmd_dispatch_rays(standard_brdf_rt_pass,
                      &bvh,
                      &index_buffer,
                      &vertex_buffer,
                      renderer->standard_brdf_rt_shader_table,
                      swap_chain->width,
                      swap_chain->height,
                      1);
  }

  {
    // Depth of field
    interlop::DofOptions dof_options = 
    {
      .z_near = kZNear,
      .aperture = render_options.aperture,
      .focal_dist = render_options.focal_dist,
      .focal_range = render_options.focal_range,
    };
  
    Handle<GpuBuffer> dof_options_buffer = create_buffer(&graph, "Depth of Field Options", dof_options);
  
    RenderPass* coc_pass = add_render_pass(MEMORY_ARENA_FWD, &graph, kCmdQueueTypeGraphics, "CoC Generation");
  
    interlop::DofCocComputeResources dof_coc_resources =
    {
      .options = dof_options_buffer,
      .color_buffer = RENDER_BUFFER(kHDRLighting),
      .depth_buffer = RENDER_BUFFER(kGBufferDepth),
      .render_target = RENDER_BUFFER(kDoFCoC),
    };
    cmd_set_compute_pso(coc_pass, &renderer->dof_coc_pipeline);
    cmd_compute_bind_shader_resources(coc_pass, dof_coc_resources);
    cmd_dispatch(coc_pass, fullres_dispatch_x, fullres_dispatch_y, 1);

#if 0
    RenderPass* coc_dilate_pass = add_render_pass(MEMORY_ARENA_FWD, &graph, kCmdQueueTypeGraphics, "CoC Dilate");
  
    interlop::DofCocDilateComputeResources dof_coc_dilate_resources =
    {
      .coc_buffer    = RENDER_BUFFER(kDoFCoC),
      .render_target = RENDER_BUFFER(kDoFDilatedCoC),
    };
    cmd_set_compute_pso(coc_dilate_pass, &renderer->dof_coc_dilate_pipeline);
    cmd_compute_bind_shader_resources(coc_dilate_pass, dof_coc_dilate_resources);
    cmd_dispatch(coc_dilate_pass, fullres_dispatch_x, fullres_dispatch_y, 1);
#endif

    RenderPass* horiz_pass = add_render_pass(MEMORY_ARENA_FWD, &graph, kCmdQueueTypeGraphics, "Dof Blur Horizontal");
    interlop::DofBlurHorizComputeResources dof_blur_horiz_resources = 
    {
      .color_buffer      = RENDER_BUFFER(kHDRLighting),
      .coc_buffer        = RENDER_BUFFER(kDoFCoC),

      .red_near_target   = RENDER_BUFFER(kDoFRedNear),
      .green_near_target = RENDER_BUFFER(kDoFGreenNear),
      .blue_near_target  = RENDER_BUFFER(kDoFBlueNear),

      .red_far_target    = RENDER_BUFFER(kDoFRedFar),
      .green_far_target  = RENDER_BUFFER(kDoFGreenFar),
      .blue_far_target   = RENDER_BUFFER(kDoFBlueFar),
    };
    cmd_set_compute_pso(horiz_pass, &renderer->dof_blur_horiz_pipeline);
    cmd_compute_bind_shader_resources(horiz_pass, dof_blur_horiz_resources);
    cmd_dispatch(horiz_pass, quarterres_dispatch_x, quarterres_dispatch_y, 2);

    RenderPass* vert_pass = add_render_pass(MEMORY_ARENA_FWD, &graph, kCmdQueueTypeGraphics, "Dof Blur Vertical");
    interlop::DofBlurVertComputeResources dof_blur_vert_resources = 
    {
      .coc_buffer        = RENDER_BUFFER(kDoFCoC),

      .red_near_buffer   = RENDER_BUFFER(kDoFRedNear),
      .green_near_buffer = RENDER_BUFFER(kDoFGreenNear),
      .blue_near_buffer  = RENDER_BUFFER(kDoFBlueNear),

      .red_far_buffer    = RENDER_BUFFER(kDoFRedFar),
      .green_far_buffer  = RENDER_BUFFER(kDoFGreenFar),
      .blue_far_buffer   = RENDER_BUFFER(kDoFBlueFar),

      .blurred_near_target = RENDER_BUFFER(kDoFBlurredNear),
      .blurred_far_target = RENDER_BUFFER(kDoFBlurredFar),
    };
    cmd_set_compute_pso(vert_pass, &renderer->dof_blur_vert_pipeline);
    cmd_compute_bind_shader_resources(vert_pass, dof_blur_vert_resources);
    cmd_dispatch(vert_pass, quarterres_dispatch_x, quarterres_dispatch_y, 2);

    RenderPass* composite_pass = add_render_pass(MEMORY_ARENA_FWD, &graph, kCmdQueueTypeGraphics, "Dof Composite");
    interlop::DofCompositeComputeResources dof_composite_resources = 
    {
      .coc_buffer   = RENDER_BUFFER(kDoFCoC),

      .color_buffer = RENDER_BUFFER(kHDRLighting),
      .near_buffer  = RENDER_BUFFER(kDoFBlurredNear),
      .far_buffer   = RENDER_BUFFER(kDoFBlurredFar),

      .render_target = RENDER_BUFFER(kDoFComposite),
    };
    cmd_set_compute_pso(composite_pass, &renderer->dof_composite_pipeline);
    cmd_compute_bind_shader_resources(composite_pass, dof_composite_resources);
    cmd_dispatch(composite_pass, fullres_dispatch_x, fullres_dispatch_y, 1);

  }

#if 0
  Handle<GpuImage> debug_buffer = create_image(&graph, "Debug Buffer", color_buffer_desc);

  // Debug passes will output to the output buffer separately and overwrite anything in it.
  bool using_debug = false;
  RenderPass* debug_pass = add_render_pass(MEMORY_ARENA_FWD, &graph, kCmdQueueTypeGraphics, "Debug Pass");
  switch(render_options.debug_view)
  {
    case kDebugViewGBufferMaterialID:
    case kDebugViewGBufferWorldPosition:
    case kDebugViewGBufferDiffuse:
    case kDebugViewGBufferMetallic:
    case kDebugViewGBufferNormal:
    case kDebugViewGBufferRoughness:
    case kDebugViewGBufferDepth:
    {
      interlop::DebugGBufferOptions options;
      options.gbuffer_target = u32(render_options.debug_view - kDebugViewGBufferMaterialID);
      Handle<GpuBuffer> gbuffer_options = create_buffer(&graph, "Debug GBuffer Options", options);
      interlop::DebugGBufferResources debug_gbuffer_resources =
      {
        .options                        = gbuffer_options,
        .gbuffer_material_ids           = gbuffers[kGBufferMaterialId],
        .gbuffer_world_pos              = gbuffers[kGBufferWorldPos],
        .gbuffer_diffuse_rgb_metallic_a = gbuffers[kGBufferDiffuseRGBMetallicA],
        .gbuffer_normal_rgb_roughness_a = gbuffers[kGBufferNormalRGBRoughnessA],
        .gbuffer_depth                  = gbuffer_depth,
        .render_target                  = debug_buffer,
      };
      cmd_set_compute_pso(debug_pass, &renderer->debug_gbuffer_pipeline);
      cmd_compute_bind_shader_resources(debug_pass, debug_gbuffer_resources);
      cmd_dispatch(debug_pass, fullres_dispatch_x, fullres_dispatch_y, 1);
      using_debug = true;
    } break;
    default: break;
  }
#endif

  // Render fullscreen triangle
  const GpuImage* back_buffer = swap_chain_acquire(swap_chain);
  Handle<GpuImage> graph_back_buffer = import_back_buffer(&graph, back_buffer);

  RenderPass* output_pass = add_render_pass(MEMORY_ARENA_FWD, &graph, kCmdQueueTypeGraphics, "Output Pass");
  cmd_clear_render_target_view(output_pass, &graph_back_buffer, Vec4(0.0f, 0.0f, 0.0f, 1.0f));
  cmd_om_set_render_targets(output_pass, {graph_back_buffer}, None);
  cmd_set_graphics_pso(output_pass, &renderer->post_processing_pipeline);

  cmd_graphics_bind_shader_resources<interlop::PostProcessingRenderResources>(output_pass, {.texture = RENDER_BUFFER(kDoFComposite) });
  cmd_draw_instanced(output_pass, 3, 1, 0, 0);
  cmd_draw_imgui_on_top(output_pass, &renderer->imgui_descriptor_heap);

  // Hand off to the GPU
  execute_render_graph(MEMORY_ARENA_FWD, device, &graph, &renderer->transient_resource_cache, swap_chain->back_buffer_index);

  swap_chain_submit(swap_chain, device, back_buffer);

  // Clear the render entries
  clear_array(&renderer->meshes);
}

Scene
init_scene(MEMORY_ARENA_PARAM, const gfx::GraphicsDevice* device)
{
  Scene ret = {0};
  ret.scene_objects = init_array<SceneObject>(MEMORY_ARENA_FWD, 128);
  ret.point_lights  = init_array<interlop::PointLight>(MEMORY_ARENA_FWD, 128);
  ret.scene_object_heap = sub_alloc_memory_arena(MEMORY_ARENA_FWD, MiB(8));
  GpuBufferDesc vertex_uber_desc = {0};
  vertex_uber_desc.size = MiB(512);

  ret.vertex_uber_buffer = alloc_gpu_buffer_no_heap(device, vertex_uber_desc, kGpuHeapTypeLocal, "Vertex Buffer");

  GpuBufferDesc index_uber_desc = {0};
  index_uber_desc.size = MiB(512);

  ret.index_uber_buffer = alloc_gpu_buffer_no_heap(device, index_uber_desc, kGpuHeapTypeLocal, "Index Buffer");

  ret.directional_light.direction = Vec4(-1.0f, -1.0f, 0.0f, 0.0f);
  ret.directional_light.diffuse   = Vec4(1.0f, 1.0f, 1.0f, 0.0f);
  ret.directional_light.intensity = 5.0f;
  return ret;
}

static UploadContext g_upload_context;

void
init_global_upload_context(MEMORY_ARENA_PARAM, const gfx::GraphicsDevice* device)
{
  GpuBufferDesc staging_desc = {0};
  staging_desc.size = MiB(32);

  g_upload_context.staging_buffer = alloc_gpu_buffer_no_heap(device, staging_desc, kGpuHeapTypeUpload, "Staging Buffer");
  g_upload_context.staging_offset = 0;
  g_upload_context.cmd_list_allocator = init_cmd_list_allocator(MEMORY_ARENA_FWD, device, &device->copy_queue, 16);
  g_upload_context.cmd_list = alloc_cmd_list(&g_upload_context.cmd_list_allocator);
  g_upload_context.device = device;
  g_upload_context.cpu_upload_arena = sub_alloc_memory_arena(MEMORY_ARENA_FWD, MiB(512));
}

void
destroy_global_upload_context()
{
//  ACQUIRE(&g_upload_context, UploadContext* upload_ctx)
//  {
//    free_gpu_ring_buffer(&upload_ctx->ring_buffer);
//  };
}

static void
print_error(const ufbx_error *error, const char *description)
{
  char buffer[1024];
  ufbx_format_error(buffer, sizeof(buffer), error);
  fprintf(stderr, "%s\n%s\n", description, buffer);
}

static void
flush_upload_staging()
{
  if (g_upload_context.staging_offset == 0)
    return;

  FenceValue value = submit_cmd_lists(&g_upload_context.cmd_list_allocator, {g_upload_context.cmd_list});
  g_upload_context.cmd_list = alloc_cmd_list(&g_upload_context.cmd_list_allocator);

  block_for_fence_value(&g_upload_context.cmd_list_allocator.fence, value);

  g_upload_context.staging_offset = 0;
}

static void
upload_gpu_data(GpuBuffer* dst_gpu, u64 dst_offset, const void* src, u64 size)
{
  if (g_upload_context.staging_buffer.desc.size - g_upload_context.staging_offset < size)
  {
    flush_upload_staging();
  }
  void* dst = (void*)(u64(unwrap(g_upload_context.staging_buffer.mapped)) + g_upload_context.staging_offset);
  memcpy(dst, src, size);

  g_upload_context.cmd_list.d3d12_list->CopyBufferRegion(dst_gpu->d3d12_buffer,
                                                         dst_offset,
                                                         g_upload_context.staging_buffer.d3d12_buffer,
                                                         g_upload_context.staging_offset,
                                                         size);
  g_upload_context.staging_offset += size;
}

static u32
alloc_into_vertex_uber(Scene* scene, u32 vertex_count)
{
  u32 ret = scene->vertex_uber_buffer_offset;
  ASSERT((ret + vertex_count) * sizeof(interlop::Vertex) <= scene->vertex_uber_buffer.desc.size);

  scene->vertex_uber_buffer_offset += vertex_count;

  return ret;
}

static u32
alloc_into_index_uber(Scene* scene, u32 index_count)
{
  u32 ret = scene->index_uber_buffer_offset;
  ASSERT((ret + index_count) * sizeof(u32) <= scene->index_uber_buffer.desc.size);

  scene->index_uber_buffer_offset += index_count;

  return ret;
}


static void 
mesh_import_scene(const aiScene* assimp_scene, Array<Mesh>* out,  Scene* scene)
{
  for (u32 imesh = 0; imesh < assimp_scene->mNumMeshes; imesh++)
  {
    reset_memory_arena(&g_upload_context.cpu_upload_arena);

    Mesh* out_mesh = array_add(out);

    const aiMesh* assimp_mesh = assimp_scene->mMeshes[imesh];

    u32 num_vertices = assimp_mesh->mNumVertices;
    u32 num_indices = assimp_mesh->mNumFaces * 3;
    auto* vertices = push_memory_arena<interlop::Vertex>(&g_upload_context.cpu_upload_arena, num_vertices);

    const aiVector3D kAssimpZero3D(0.0f, 0.0f, 0.0f);

    for (u32 ivertex = 0; ivertex < assimp_mesh->mNumVertices; ivertex++)
    {
      const aiVector3D* a_pos     = &assimp_mesh->mVertices[ivertex];
      const aiVector3D* a_normal  = &assimp_mesh->mNormals[ivertex];
      const aiVector3D* a_uv      = assimp_mesh->HasTextureCoords(0) ? &assimp_mesh->mTextureCoords[0][ivertex] : &kAssimpZero3D;
//      const aiVector3D* a_tangent = &assimp_mesh->mTangents[i];
//      const aiVector3D* a_tangent = &assimp_mesh->mTangents[i];

      vertices[ivertex].position = Vec4(a_pos->x, a_pos->y, a_pos->z, 1.0f);
      vertices[ivertex].normal   = Vec4(a_normal->x, a_normal->y, a_normal->z, 1.0f);
      vertices[ivertex].uv       = Vec4(a_uv->x, a_uv->y, 0.0f, 0.0f);
    }

    u32 vertex_buffer_offset = alloc_into_vertex_uber(scene, num_vertices);

    u32* indices = push_memory_arena<u32>(&g_upload_context.cpu_upload_arena, num_indices);

    u32 iindex = 0;
    for (u32 iface = 0; iface < assimp_mesh->mNumFaces; iface++)
    {
      const aiFace* a_face = &assimp_mesh->mFaces[iface];
      if (a_face->mNumIndices != 3)
      {
        dbgln("Skipping face with %u indices", a_face->mNumIndices);
        continue;
      }

      ASSERT(a_face->mNumIndices == 3);
      // TODO(Brandon): This is fucking stupid. Why doesn't d3d12 use BaseVertexLocation to offset these?? I have no fucking clue...
      indices[iindex + 0] = a_face->mIndices[0] + vertex_buffer_offset;
      indices[iindex + 1] = a_face->mIndices[1] + vertex_buffer_offset;
      indices[iindex + 2] = a_face->mIndices[2] + vertex_buffer_offset;
      iindex += 3;
    }

    num_indices = iindex;


    out_mesh->index_count = num_indices;
    out_mesh->index_buffer_offset = alloc_into_index_uber(scene, num_indices);
    upload_gpu_data(&scene->vertex_uber_buffer,
                    vertex_buffer_offset * sizeof(interlop::Vertex),
                    vertices,
                    num_vertices * sizeof(interlop::Vertex));
    upload_gpu_data(&scene->index_uber_buffer,
                    out_mesh->index_buffer_offset * sizeof(u32),
                    indices,
                    num_indices * sizeof(u32));
  }
  flush_upload_staging();
}

static Array<Mesh>
load_mesh_from_file(MEMORY_ARENA_PARAM,
                    Scene* scene,
                    const ShaderManager& shader_manager,
                    const char* mesh_path,
                    ShaderIndex vertex_shader,
                    ShaderIndex material_shader)
{
  Array<Mesh> ret = {0};

  Assimp::Importer importer;
  dbgln("Assimp reading file...");
  const aiScene* assimp_scene = importer.ReadFile(mesh_path, aiProcess_CalcTangentSpace | aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_SortByPType | aiProcess_PreTransformVertices );
  dbgln("Assimp done reading.");

  ASSERT(assimp_scene != nullptr);

  ret = init_array<Mesh>(MEMORY_ARENA_FWD, assimp_scene->mNumMeshes);

  mesh_import_scene(assimp_scene, &ret, scene);

  GraphicsPipelineDesc graphics_pipeline_desc = 
  {
    .vertex_shader = shader_manager.shaders[vertex_shader],
    .pixel_shader = shader_manager.shaders[material_shader],
    .rtv_formats = kGBufferRenderTargetFormats,
    .dsv_format = kRenderBufferDescs[RenderBuffers::kGBufferDepth].format,
    .comparison_func = kDepthComparison,
    .stencil_enable = false,
  };
  GraphicsPSO pso = init_graphics_pipeline(g_upload_context.device, graphics_pipeline_desc, "Mesh PSO");
  for (Mesh& mesh : ret)
  {
    mesh.vertex_shader = vertex_shader;
    mesh.material_shader = material_shader;
    mesh.gbuffer_pso = pso;
  }

  importer.FreeScene();

  return ret;
}

SceneObject*
add_scene_object(Scene* scene,
                 const ShaderManager& shader_manager,
                 const char* mesh,
                 ShaderIndex vertex_shader,
                 ShaderIndex material_shader)
{
  SceneObject* ret = array_add(&scene->scene_objects);
  ret->flags = kSceneObjectMesh;
  ret->meshes = load_mesh_from_file(&scene->scene_object_heap, scene, shader_manager, mesh, vertex_shader, material_shader);

  return ret;
}

interlop::PointLight* add_point_light(Scene* scene)
{
  interlop::PointLight* ret = array_add(&scene->point_lights);
  ret->position = Vec4(0, 0, 0, 1);
  ret->color = Vec4(1, 1, 1, 1);
  ret->radius = 10;
  ret->intensity = 10;

  return ret;
}

void
submit_scene(const Scene& scene, Renderer* renderer)
{
  for (const SceneObject& obj : scene.scene_objects)
  {
//    u8 flags = obj.flags;
//    if (flags & kSceneObjectPendingLoad)
//    {
//      if (!job_has_completed(obj.loading_signal))
//        continue;
//
//      flags &= ~kSceneObjectPendingLoad;
//    }

    for (const Mesh& mesh : obj.meshes)
    {
      submit_mesh(renderer, mesh);
    }
  }
}
