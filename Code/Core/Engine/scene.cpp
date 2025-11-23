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

  GpuBvh             bvh;

  GpuRtTlas          tlas;
  // I allocate it here locally because the render graph doesn't really need to know about it
  GpuBuffer          tlas_scratch_buffer;

  // Updated by the render handlers
  u32                blas_instance_count;
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

  g_Scene->tlas                        = alloc_gpu_rt_tlas_no_heap(kMaxSceneObjs, "Scene TLAS");
  // GpuDescriptor descriptor             = alloc_table_descriptor(g_DescriptorCbvSrvUavPool, kGrvTemporalCount * kBackBufferCount + kRaytracingAccelerationStructureSlot);
  // init_bvh_srv(&descriptor, &g_Scene->tlas);

  GpuBufferDesc tlas_scratch_buffer_desc =
  {
    .size          = g_Scene->tlas.scratch_size,
    .flags         = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
  };
  g_Scene->tlas_scratch_buffer         = alloc_gpu_buffer_no_heap(g_GpuDevice, tlas_scratch_buffer_desc, kGpuHeapGpuOnly, "TLAS Scratch Buffer");
  g_Scene->blas_instance_count         = 0;
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
    obj->flags             = flags;
    obj->obj_to_world      = Mat4();
    obj->prev_obj_to_world = Mat4();
    obj->gpu_id            = 0xFFFFFFFF;
    obj->needs_gpu_upload  = false;

    if (obj->flags & kSceneObjRender)
    {
      Option<u32> gpu_id = bit_alloc(&g_Scene->gpu_scene_obj_allocator);
      ASSERT_MSG_FATAL(gpu_id, "Too many gpu scene objects allocated %u!", g_Scene->gpu_scene_obj_allocator.capacity);
      if (gpu_id)
      {
        obj->gpu_id           = unwrap(gpu_id);
        obj->needs_gpu_upload = true;
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
init_render_scene_obj(AssetId model, u32 subset, u32 flags)
{
  flags |= kSceneObjRender;
  SceneObjHandle ret = alloc_scene_obj(flags);

  SceneObj*      obj = get_scene_obj_common(ret);
  obj->model_asset = model;
  obj->subset_id   = subset;
  obj->needs_instance_data_gpu_upload = true;

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
  }

  return obj;
}

void
dynamic_scene_obj_attach_render_model(SceneObjHandle handle, AssetId model, u32 subset)
{
  auto res = get_mutable_scene_obj(handle);
  ASSERT_MSG_FATAL(res, "Scene object handle did not resolve!");
  if (!res)
  {
    return;
  }
  
  SceneObj* obj = unwrap(res);
  obj->model_asset = model;
  obj->subset_id   = subset;
  obj->needs_instance_data_gpu_upload = true;
}

// TODO(bshihabi): I don't love this, I wish there was a better way to manage the maximum number of uploads per frame
// so that we could handle high stream loads without having to constantly have this bit of memory
static constexpr u64 kUploadBufferSize = KiB(8);

struct SceneGpuUploadParams
{
  RgCpuUploadBuffer upload_buffer;
  RgCopyDst         material_buffer;
  RgCopyDst         scene_obj_buffer;
};

static void
render_handler_scene_gpu_upload(RenderContext* ctx, const RenderSettings&, const void* data)
{
  SceneGpuUploadParams* params   = (SceneGpuUploadParams*)data;

  u64 offset = 0;

  auto fill_scene_obj_gpu = [](SceneObjGpu* dst, SceneObj* src)
  {
    ASSERT_MSG_FATAL(src->gpu_id < kMaxSceneObjs, "Invalid GPU ID 0x%x!", src->gpu_id);

    dst->obj_to_world      = src->obj_to_world;
    dst->prev_obj_to_world = src->prev_obj_to_world;
    dst->mat_id            = src->mat_id;

      // This actually is a nice safeguard because we would never render a 0 index count object anyway even if we did make a draw call for it
    dst->index_count       = 0;
    dst->start_index       = 0;
    dst->start_vertex      = 0;
    if (src->needs_instance_data_gpu_upload && src->model_asset != kNullAssetId)
    {
      Result<const ModelMetadata*, AssetState> res = get_model_asset(src->model_asset);
      // If the asset is actually loaded
      if (res)
      {
        const ModelMetadata* model = res.value();

        u32   subset_id            = src->subset_id;
        ASSERT_MSG_FATAL(subset_id < model->subsets.size, "Invalid subset ID on scene object %u", src->subset_id);
        // If the subset ID is just straight up invalid, we're not gonna bother trying to do anything meaningful, just leave it as not drawing
        if (subset_id < model->subsets.size)
        {
          dst->index_count  = model->subsets[subset_id].index_count;
          dst->start_index  = model->subsets[subset_id].index_start;
          dst->start_vertex = model->subsets[subset_id].vertex_start;
          dst->blas_addr    = model->subset_rt_blases[subset_id].buffer.gpu_addr;

          src->index_count  = dst->index_count;
          src->start_index  = dst->start_index;
        }

        src->needs_instance_data_gpu_upload = false;
      }
    }

    src->needs_gpu_upload     = false;
  };

  for (u32 iscene_obj = 0; iscene_obj < kMaxDynamicSceneObjs; iscene_obj++)
  {
    if (offset >= kUploadBufferSize)
    {
      dbgln("Warning: uploaded buffer maxed at 0x%x for frame %u Gpu Scene upload. Consider bumping `kUploadBufferSize`", kUploadBufferSize, g_FrameId, kUploadBufferSize);
      break;
    }

    if (!bit_is_allocated(g_Scene->dynamic_scene_obj_allocator, iscene_obj))
    {
      continue;
    }

    SceneObj* obj = g_Scene->dynamic_scene_objs + iscene_obj;
    if (obj->needs_gpu_upload)
    {
      SceneObjGpu obj_gpu;
      fill_scene_obj_gpu(&obj_gpu, obj);

      ctx->write_cpu_upload_buffer(params->upload_buffer, &obj_gpu, sizeof(obj_gpu), offset);
      ctx->copy_buffer(params->scene_obj_buffer, sizeof(SceneObjGpu) * obj->gpu_id, params->upload_buffer, offset, sizeof(obj_gpu));

      offset += sizeof(SceneObjGpu);
    }
  }

  for (u32 iscene_obj = 0; iscene_obj < kMaxStaticSceneObjs; iscene_obj++)
  {
    if (offset >= kUploadBufferSize)
    {
      dbgln("Warning: uploaded buffer maxed at 0x%x for frame %u Gpu Scene upload. Consider bumping `kUploadBufferSize`", kUploadBufferSize, g_FrameId, kUploadBufferSize);
      break;
    }

    if (!bit_is_allocated(g_Scene->static_scene_obj_allocator, iscene_obj))
    {
      continue;
    }

    SceneObj* obj = g_Scene->static_scene_objs + iscene_obj;
    if (obj->needs_gpu_upload)
    {
      SceneObjGpu obj_gpu;
      fill_scene_obj_gpu(&obj_gpu, obj);

      ctx->write_cpu_upload_buffer(params->upload_buffer, &obj_gpu, sizeof(obj_gpu), offset);
      ctx->copy_buffer(params->scene_obj_buffer, sizeof(SceneObjGpu) * obj->gpu_id, params->upload_buffer, offset, sizeof(obj_gpu));

      offset += sizeof(SceneObjGpu);
    }
  }
}

struct SceneTlasFillInstancesParams
{
  ComputePSO pso;
  RgRWStructuredBufferCounted<D3D12RaytracingInstanceDesc> instances;
};

static void
render_handler_fill_tlas_instances(RenderContext* ctx, const RenderSettings&, const void* data)
{
  SceneTlasFillInstancesParams* params = (SceneTlasFillInstancesParams*)data;

  ctx->clear_uav_u32(params->instances.m_Counter, 0, 1);

  RtBuildTlasSrt srt;
  srt.scene_obj_count      = kMaxSceneObjs;
  srt.tlas_instance_descs  = params->instances;

  ctx->compute_bind_srt(srt);
  ctx->set_compute_pso(&params->pso);
  ctx->dispatch(ALIGN_POW2(kMaxSceneObjs, 64) / 64, 1, 1);
}

struct BuildTlasParams
{
  RgStructuredBuffer<D3D12RaytracingInstanceDesc> instances;
};

static void
render_handler_build_tlas(RenderContext* ctx, const RenderSettings&, const void* data)
{
  BuildTlasParams* params = (BuildTlasParams*)data;

  u32 instance_count = g_Scene->gpu_scene_obj_allocator.allocated_count;

  ctx->build_tlas(&g_Scene->tlas, &g_Scene->tlas_scratch_buffer, 0, params->instances, instance_count, 0);
}


SceneGpuResources
init_scene_gpu_upload_pass(AllocHeap heap, RgBuilder* builder)
{
  SceneGpuResources ret;
  // TODO(bshihabi): This should really be a ring buffer...
  ret.upload_buffer          = rg_create_upload_buffer(builder, "Upload Buffer",       kGpuHeapSysRAMCpuToGpu, kUploadBufferSize);

  ret.material_buffer        = rg_create_buffer(builder,        "Material Buffer",     sizeof(MaterialGpu) * kMaxSceneObjs);
  ret.scene_obj_buffer       = rg_create_buffer(builder,        "Scene Object Buffer", sizeof(SceneObjGpu) * kMaxSceneObjs);

  RgHandleCountedBuffer tlas_instances = rg_create_counted_buffer(builder, "TLAS instance Buffer", sizeof(D3D12RaytracingInstanceDesc) * kMaxSceneObjs);
  RgHandle<GpuRtTlas>   tlas           = rg_create_tlas(builder, "TLAS", kMaxSceneObjs);

  {
    SceneGpuUploadParams* params = HEAP_ALLOC(SceneGpuUploadParams, g_InitHeap, 1);
    RgPassBuilder*        pass   = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "Gpu Scene Upload", params, &render_handler_scene_gpu_upload);
    params->upload_buffer        = RgCpuUploadBuffer(ret.upload_buffer);
    params->scene_obj_buffer     = RgCopyDst(pass, &ret.scene_obj_buffer);
    params->material_buffer      = RgCopyDst(pass, &ret.material_buffer);
  }

  // Fill TLAS instance data
  {
    SceneTlasFillInstancesParams* params = HEAP_ALLOC(SceneTlasFillInstancesParams, g_InitHeap, 1);
    RgPassBuilder*                pass   = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "Fill TLAS instance data", params, &render_handler_fill_tlas_instances);
    params->pso                          = init_compute_pipeline(g_GpuDevice, get_engine_shader(kCS_RtTlasFillInstances), "CS_RtTlasFillInstances");
    params->instances                    = RgRWStructuredBufferCounted<D3D12RaytracingInstanceDesc>(pass, &tlas_instances);

    // Bind the GRVs now that we're done modifying them
    RgStructuredBuffer<SceneObjGpu>(pass, ret.scene_obj_buffer, 0, kSceneObjBufferSlot);
    RgStructuredBuffer<MaterialGpu>(pass, ret.material_buffer,  0, kMaterialBufferSlot);
  }

  // Build the TLAS
  {
    BuildTlasParams*  params = HEAP_ALLOC(BuildTlasParams, g_InitHeap, 1);
    RgPassBuilder*    pass   = add_render_pass(heap, builder, kCmdQueueTypeGraphics, "Build TLAS", params,  &render_handler_build_tlas);
    params->instances        = RgStructuredBuffer<D3D12RaytracingInstanceDesc>(pass, tlas_instances.buffer);
  }


  return ret;
}

// TODO(bshihabi): We need to completely rework this...
void
build_acceleration_structures()
{
  g_Scene->bvh = init_gpu_bvh(
    g_GpuDevice,
    g_UnifiedGeometryBuffer.vertex_buffer,
    0, // (u32)g_UnifiedGeometryBuffer.vertex_buffer_pos / sizeof(VertexAsset),
    sizeof(Vertex),
    g_UnifiedGeometryBuffer.index_buffer,
    0, // (u32)g_UnifiedGeometryBuffer.index_buffer_pos / sizeof(u16),
    "Scene Acceleration Structure"
  );

  // TODO(bshihabi): This shouldn't live here...
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
