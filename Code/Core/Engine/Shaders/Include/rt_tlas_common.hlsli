#pragma once

struct SceneObjGpuBlas
{
  u32 gpu_id;
  u32 __pad__;

  u64 addr;
};

struct RtBuildTlasSrt
{
  u32 instance_count;
  StructuredBufferPtr<SceneObjGpuBlas>               scene_obj_gpu_blases;
  RWStructuredBufferPtr<D3D12RaytracingInstanceDesc> tlas_instance_descs;
};
