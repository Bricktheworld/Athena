#pragma once
#include "Core/Foundation/assets.h"
#include "Core/Engine/Render/render_graph.h"
#include "Core/Engine/asset_streaming.h"

struct Camera
{
  Vec3 world_pos = Vec3(0, 0, -1);
  f32  pitch     = 0;
  f32  yaw       = 0;
};

struct SceneGpuResources
{
  // Uploaded data to GPU Buffers
  RgHandle<GpuBuffer> material_buffer;
  RgHandle<GpuBuffer> scene_obj_buffer;

  // TODO(bshihabi): Make this a ring-buffer type structure
  RgHandle<GpuBuffer> upload_buffer;
};

enum SceneObjFlags : u32
{
  kSceneObjDynamic = 0x1 << 0,
  kSceneObjRender  = 0x2 << 0,
};

struct SceneObj
{
  Mat4        obj_to_world;
  Mat4        prev_obj_to_world;

  u32         generation   = 0;
  u32         flags        = 0;

  // TODO(bshihabi): Maybe we should pull this out into a separate "render" component
  // 
  ModelHandle model;
  u32         subset_id    = 0;
  u32         mat_id       = 0;
  u32         gpu_id       = 0;

  // TODO(bshihabi): rip these out when the shader can do all of the multi draw indirect automatically
  u32         start_index  = 0;
  u32         index_count  = 0;

  u64         needs_gpu_upload:               1 = 0;
  u64         needs_blas_build:               1 = 0;
  u64         needs_instance_data_gpu_upload: 1 = 0;
};

struct alignas(u32x4) SceneObjHandle
{
  const u32 id         = 0;
  const u32 generation = 0;
  const u32 flags      = 0;
  const u32 __pad__;
};

static constexpr SceneObjHandle kNullSceneObj = { 0xFFFFFFFF, 0xFFFFFFFF, 0x0 };

void                    init_scene();
SceneGpuResources       init_scene_gpu_upload_pass(AllocHeap heap, RgBuilder* builder);

SceneObjHandle          alloc_scene_obj(u32 flags);
SceneObjHandle          init_render_scene_obj(ModelHandle model, u32 subset, u32 flags = 0);

// Does not GPU upload the scene object (don't modify it)
Option<const SceneObj*> get_scene_obj(SceneObjHandle handle);
// Will mark the scene object for GPU upload if needed
Option<SceneObj*>       get_mutable_scene_obj(SceneObjHandle handle);
void                    dynamic_scene_obj_attach_render_model(SceneObjHandle handle, ModelHandle model, u32 subset);

Camera*                 get_scene_camera();
DirectionalLight*       get_scene_directional_light();

// TODO(bshihabi): I don't love this, I wish there was a nicer way to "iterate through all scene objects with given flags"
const SceneObj*         get_all_scene_objs();

