#include "Core/Foundation/pool_allocator.h"
#include "Core/Foundation/bit_allocator.h"

#include "Core/Engine/scene.h"
#include "Core/Engine/asset_streaming.h"
#include "Core/Engine/memory.h"
#include "Core/Engine/constants.h"
#include "Core/Engine/Render/renderer.h"

#include "Core/Engine/Shaders/Include/rt_tlas_common.hlsli"

struct Scene
{
  SceneObj*          scene_objs         = nullptr;
  SceneObj*          dynamic_scene_objs = nullptr;
  SceneObj*          static_scene_objs  = nullptr;

  BitAllocator       dynamic_scene_obj_allocator;
  BitAllocator       static_scene_obj_allocator;
  BitAllocator       gpu_scene_obj_allocator;

  Camera             camera;
  DirectionalLight   directional_light;
};

static Scene* g_Scene = nullptr;

void
init_scene()
{
  g_Scene = HEAP_ALLOC(Scene, g_InitHeap, 1);
  g_Scene->scene_objs         = HEAP_ALLOC(SceneObj, g_InitHeap, kMaxSceneObjs);
  zero_memory(g_Scene->scene_objs, kMaxSceneObjs * sizeof(SceneObj));

  g_Scene->dynamic_scene_objs          = g_Scene->scene_objs;
  g_Scene->static_scene_objs           = g_Scene->scene_objs + kMaxDynamicSceneObjs;

  g_Scene->dynamic_scene_obj_allocator = init_bit_allocator(g_InitHeap, kMaxDynamicSceneObjs);
  g_Scene->static_scene_obj_allocator  = init_bit_allocator(g_InitHeap, kMaxStaticSceneObjs);
  g_Scene->gpu_scene_obj_allocator     = init_bit_allocator(g_InitHeap, kMaxSceneObjs);
}

SceneObjHandle
alloc_scene_obj(u32 flags)
{
  u32            scene_obj_id = 0;
  SceneObj*      obj          = nullptr;
  if (flags & kSceneObjDynamic)
  {
    Option<u32> id = bit_alloc(&g_Scene->dynamic_scene_obj_allocator);
    ASSERT_MSG_FATAL(id, "Too many dynamic scene objects allocated %u! Bump kMaxDynamicSceneObjs", kMaxDynamicSceneObjs);

    if (!id)
    {
      return kNullSceneObj;
    }

    scene_obj_id   = unwrap(id);
    obj            = g_Scene->dynamic_scene_objs + scene_obj_id;
  }
  else
  {
    Option<u32> id = bit_alloc(&g_Scene->static_scene_obj_allocator);
    ASSERT_MSG_FATAL(id, "Too many static scene objects allocated %u! Bump kMaxStaticSceneObjs", kMaxStaticSceneObjs);

    if (!id)
    {
      return kNullSceneObj;
    }

    scene_obj_id   = unwrap(id);
    obj            = g_Scene->static_scene_objs  + scene_obj_id;
  }

  if (obj)
  {
    obj->generation++;
    obj->flags                = flags;
    obj->obj_to_world         = Mat4();
    obj->prev_obj_to_world    = Mat4();
    obj->gpu_id               = 0xFFFFFFFF;
    obj->needs_gpu_upload     = false;
    obj->needs_rt_upload      = false;
    obj->needs_instance_data_gpu_upload = false;

    if (obj->flags & kSceneObjRender)
    {
      Option<u32> gpu_id = bit_alloc(&g_Scene->gpu_scene_obj_allocator);
      ASSERT_MSG_FATAL(gpu_id, "Too many gpu scene objects allocated %u!", g_Scene->gpu_scene_obj_allocator.capacity);
      if (gpu_id)
      {
        obj->gpu_id           = unwrap(gpu_id);
        obj->needs_gpu_upload = true;
        obj->needs_rt_upload  = true;
        obj->needs_instance_data_gpu_upload = false;
      }
    }

    return { 
      .id         = scene_obj_id,
      .generation = obj->generation,
      .flags      = obj->flags
    };
  }

  return kNullSceneObj;
}

static SceneObj*
get_scene_obj_common(SceneObjHandle handle)
{
  SceneObj* scene_obj = nullptr;
  if (handle.flags & kSceneObjDynamic)
  {
    scene_obj = g_Scene->dynamic_scene_objs + handle.id;
  }
  else
  {
    scene_obj = g_Scene->static_scene_objs  + handle.id;
  }

  // Generation doesn't match
  if (scene_obj && scene_obj->generation != handle.generation)
  {
    return nullptr;
  }

  return scene_obj;
}

SceneObjHandle 
init_render_scene_obj(ModelHandle model, u32 subset, u32 flags)
{
  flags |= kSceneObjRender;
  SceneObjHandle ret = alloc_scene_obj(flags);

  SceneObj*      obj = get_scene_obj_common(ret);
  obj->model         = model;
  obj->subset_id     = subset;
  obj->mat_id        = model->materials[subset]->gpu_id;
  obj->needs_instance_data_gpu_upload = true;
  obj->needs_gpu_upload               = true;
  obj->needs_rt_upload                = true;

  return ret;
}

Option<const SceneObj*> 
get_scene_obj(SceneObjHandle handle)
{
  SceneObj* obj = get_scene_obj_common(handle);
  if (obj == nullptr)
  {
    return None;
  }

  return obj;
}

Option<SceneObj*>
get_mutable_scene_obj(SceneObjHandle handle)
{
  SceneObj* obj = get_scene_obj_common(handle);

  bool is_dynamic = obj->flags & kSceneObjDynamic;

  ASSERT_MSG_FATAL(is_dynamic, "Attempting to mutate a static scene object is not allowed (use kSceneObjDynamic when allocating the scene obj)");
  if (!is_dynamic)
  {
    return None;
  }

  if (obj == nullptr)
  {
    return None;
  }

  if (obj->flags & kSceneObjRender)
  {
    obj->needs_gpu_upload = true;
    obj->needs_rt_upload  = true;
  }

  return obj;
}

void
dynamic_scene_obj_attach_render_model(SceneObjHandle handle, ModelHandle model, u32 subset)
{
  auto res = get_mutable_scene_obj(handle);
  ASSERT_MSG_FATAL(res, "Scene object handle did not resolve!");
  if (!res)
  {
    return;
  }
  
  SceneObj* obj = unwrap(res);
  obj->model       = model;
  obj->subset_id   = subset;
  obj->needs_instance_data_gpu_upload = true;
  obj->needs_gpu_upload               = true;
  obj->needs_rt_upload                = true;
}

static u32
pick_subset_lod(const Vec3& ws_camera, const Mat4& proj, const ModelSubset* subset)
{
  u32 ret         = (u32)subset->lods.size - 1;

  if (g_Renderer.settings.forced_model_lod >= 0)
  {
    return MIN((u32)g_Renderer.settings.forced_model_lod, ret);
  }
  // TODO(bshihabi): We should use the transform here for this
  f32 camera_dist = MAX(length(ws_camera - subset->center) - subset->radius, 0.0f);
  f32 camera_proj = proj.entries[1][1];

  while (ret != 0)
  {
    f32 projected_error = subset->lods[ret].error / MAX(camera_dist, kZNear) * camera_proj;
    if (projected_error <= 1e-3f)
    {
      break;
    }
    ret--;
  }

  return ret;
}

void 
render_handler_scene_upload(const RenderEntry* entries, u32 count)
{
  // TODO(bshihabi): We should handle this stuff earlier before the render handler I think, that would make more sense
  // and then we can just send all of the entries at once to the handler
  UNREFERENCED_PARAMETER(entries);
  UNREFERENCED_PARAMETER(count);

  const StructuredBuffer<SceneObjGpu>& scene_obj_buffer = g_RenderHandlerState.buffers.scene_obj_buffer;
  const StructuredBuffer<RtObjGpu>&    rt_obj_buffer    = g_RenderHandlerState.buffers.rt_obj_buffer;
  const GpuBuffer&                     upload_buffer           = g_RenderHandlerState.buffers.upload_buffer.buffer;

  auto fill_scene_obj_gpu = [](SceneObjGpu* dst, SceneObj* src) -> bool
  {
    ASSERT_MSG_FATAL(src->gpu_id < kMaxSceneObjs, "Invalid GPU ID 0x%x!", src->gpu_id);

    if (!src->model || !src->model.is_loaded())
    {
      return false;
    }

    ModelHandle model         = src->model;
    u32         subset_id     = src->subset_id;
    
    // If the subset ID is just straight up invalid, we're not gonna bother trying to do anything meaningful, just leave it as not drawing
    if (subset_id >= src->model->subsets.size)
    {
      // This actually is a nice safeguard because we would never render a 0 index count object anyway even if we did make a draw call for it
      dst->index_count  = 0;
      dst->start_index  = 0;
      dst->start_vertex = 0;

      return false;
    }

    if (src->needs_instance_data_gpu_upload)
    {
      ModelHandle model         = src->model;
      u32         subset_id     = src->subset_id;
      ASSERT_MSG_FATAL(subset_id < model->subsets.size, "Invalid subset ID on scene object %u", src->subset_id);

      // Add in the transformation to uncompress vertices correctly
      src->obj_to_world        *= model->subsets[subset_id].radius;
      src->obj_to_world.cols[3] = Vec4(model->subsets[subset_id].center, 1.0f);
      src->prev_obj_to_world    = src->obj_to_world;

      src->needs_instance_data_gpu_upload = false;
    }

    const ViewCtx* view        = &g_RenderHandlerState.main_view;
    src->lod_idx               = pick_subset_lod(view->camera.world_pos, view->proj, &model->subsets[subset_id]);
    const ModelSubsetLod& lod  = model->subsets[subset_id].lods[src->lod_idx];

    dst->obj_to_world          = src->obj_to_world;
    dst->prev_obj_to_world     = src->prev_obj_to_world;
    dst->mat_id                = src->mat_id;
    dst->index_count           = lod.index_count;
    dst->start_index           = lod.index_start;
    dst->start_vertex          = lod.vertex_start;
                                   
    src->index_count           = dst->index_count;
    src->start_index           = dst->start_index;

    src->needs_gpu_upload     = false;

    return true;
  };

  auto fill_rt_obj_gpu = [](RtObjGpu* dst, SceneObj* src)
  {
    // This actually is a nice safeguard because we would never render a 0 index count object anyway even if we did make a draw call for it
    dst->obj_to_world         = src->obj_to_world; 
    dst->mat_id               = 0;
    dst->index_count          = 0;
    dst->start_index          = 0;
    dst->start_vertex         = 0;
    dst->blas_addr            = 0;

    ModelHandle model         = src->model;
    u32         subset_id     = src->subset_id;


    if (subset_id < src->model->subset_rt_blases.size)
    {
      const ModelSubset* subset = &model->subsets[subset_id];
      const ModelSubsetLod* lod = &subset->lods[subset->rt_blas_lod];
                              
      dst->mat_id               = subset->mat_gpu_id;
      dst->index_count          = lod->index_count;
      dst->start_index          = lod->index_start;
      dst->start_vertex         = lod->vertex_start;
      dst->blas_addr            = model->subset_rt_blases[subset_id].buffer.gpu_addr;

      src->needs_rt_upload = false;
    }
  };

  for (u32 iscene_obj = 0; iscene_obj < kMaxDynamicSceneObjs; iscene_obj++)
  {
    if (!bit_is_allocated(g_Scene->dynamic_scene_obj_allocator, iscene_obj))
    {
      continue;
    }

    SceneObj* obj = g_Scene->dynamic_scene_objs + iscene_obj;
    if (obj->needs_gpu_upload)
    {
      SceneObjGpu obj_gpu;
      if (fill_scene_obj_gpu(&obj_gpu, obj))
      {
        GpuStagingAllocation upload_alloc = gpu_alloc_staging_bytes(&g_RenderHandlerState.cmd_list, sizeof(obj_gpu));
        memcpy(upload_alloc.cpu_dst, &obj_gpu, sizeof(obj_gpu));
        gpu_copy_buffer(&g_RenderHandlerState.cmd_list, scene_obj_buffer.buffer, sizeof(SceneObjGpu) * obj->gpu_id, upload_buffer, upload_alloc.gpu_offset, sizeof(obj_gpu));
      }

    }

    if (obj->needs_rt_upload)
    {
      RtObjGpu obj_gpu;
      fill_rt_obj_gpu(&obj_gpu, obj);

      GpuStagingAllocation upload_alloc = gpu_alloc_staging_bytes(&g_RenderHandlerState.cmd_list, sizeof(obj_gpu));
      memcpy(upload_alloc.cpu_dst, &obj_gpu, sizeof(obj_gpu));
      gpu_copy_buffer(&g_RenderHandlerState.cmd_list, rt_obj_buffer.buffer, sizeof(RtObjGpu) * obj->gpu_id, upload_buffer, upload_alloc.gpu_offset, sizeof(obj_gpu));
    }
  }

  for (u32 iscene_obj = 0; iscene_obj < kMaxStaticSceneObjs; iscene_obj++)
  {
    if (!bit_is_allocated(g_Scene->static_scene_obj_allocator, iscene_obj))
    {
      continue;
    }


    SceneObj* obj = g_Scene->static_scene_objs + iscene_obj;

    const ViewCtx* view = &g_RenderHandlerState.main_view;
    u32 best_lod_idx = pick_subset_lod(view->camera.world_pos, view->proj, &obj->model->subsets[obj->subset_id]);
    if (best_lod_idx != obj->lod_idx)
    {
      obj->needs_gpu_upload     = true;
    }

    if (obj->needs_gpu_upload || obj->needs_instance_data_gpu_upload)
    {
      SceneObjGpu obj_gpu;
      if (fill_scene_obj_gpu(&obj_gpu, obj))
      {
        GpuStagingAllocation upload_alloc = gpu_alloc_staging_bytes(&g_RenderHandlerState.cmd_list, sizeof(obj_gpu));
        memcpy(upload_alloc.cpu_dst, &obj_gpu, sizeof(obj_gpu));
        gpu_copy_buffer(&g_RenderHandlerState.cmd_list, scene_obj_buffer.buffer, sizeof(SceneObjGpu) * obj->gpu_id, upload_buffer, upload_alloc.gpu_offset, sizeof(obj_gpu));
      }
    }

    if (obj->needs_rt_upload)
    {
      RtObjGpu obj_gpu;
      fill_rt_obj_gpu(&obj_gpu, obj);

      GpuStagingAllocation upload_alloc = gpu_alloc_staging_bytes(&g_RenderHandlerState.cmd_list, sizeof(obj_gpu));
      memcpy(upload_alloc.cpu_dst, &obj_gpu, sizeof(obj_gpu));
      gpu_copy_buffer(&g_RenderHandlerState.cmd_list, rt_obj_buffer.buffer, sizeof(RtObjGpu) * obj->gpu_id, upload_buffer, upload_alloc.gpu_offset, sizeof(obj_gpu));
    }
  }
}

void
render_handler_build_tlas(const RenderEntry*, u32)
{
  u32 instance_count = g_RenderHandlerState.settings.disable_ray_tracing ? 0 : g_Scene->gpu_scene_obj_allocator.allocated_count;

  // TODO(bshihabi): We really should have a nicer way of handling this
  static u32 s_PrevInstanceCount = UINT32_MAX;

  const GpuRtTlas& tlas                = g_RenderHandlerState.buffers.rt_tlas;
  const auto&      tlas_instance_descs = g_RenderHandlerState.buffers.rt_tlas_instances;
  const GpuBuffer& scratch             = g_RenderHandlerState.buffers.tlas_scratch;

  CmdList*         cmd                 = &g_RenderHandlerState.cmd_list;

  gpu_clear_buffer_u32(cmd, {tlas_instance_descs.counter_uav.index}, 1, 0, 0);
  gpu_memory_barrier(cmd);

  RtBuildTlasSrt srt;
  srt.scene_obj_count     = kMaxSceneObjs;
  srt.tlas_instance_descs = tlas_instance_descs;
  gpu_bind_compute_pso(cmd, kCS_RtTlasFillInstances);
  gpu_bind_srt(cmd, srt);
  gpu_dispatch(cmd, ALIGN_POW2(kMaxSceneObjs, 64) / 64, 1, 1);

  gpu_memory_barrier(cmd);

  u32 build_flags = (instance_count == s_PrevInstanceCount) ? kGpuRtasBuildIncremental : 0;
  build_rt_tlas(cmd, tlas, tlas_instance_descs.buffer, instance_count, scratch, 0, build_flags);
  if (build_flags == 0)
  {
    dbgln("Non-incremental build RT TLAS: %u", instance_count);
  }

  gpu_memory_barrier(cmd);

  s_PrevInstanceCount = instance_count;
}

Camera*
get_scene_camera()
{
  return &g_Scene->camera;
}

DirectionalLight*
get_scene_directional_light()
{
  return &g_Scene->directional_light;
}

const SceneObj*
get_all_scene_objs()
{
  return g_Scene->scene_objs;
}

u32
get_gpu_scene_obj_count()
{
  return g_Scene->gpu_scene_obj_allocator.allocated_count;
}

BoundingSphere
get_bounding_sphere(const SceneObj* obj)
{
  BoundingSphere ret;
  Vec4 translation = get_translation(obj->obj_to_world);
  ret.center = Vec3(translation.x, translation.y, translation.z);
  ret.radius = get_uniform_scale(obj->obj_to_world);

  return ret;
}