#include "../root_signature.hlsli"
#include "../interlop.hlsli"
#include "../Include/rt_tlas_common.hlsli"

ConstantBuffer<RtBuildTlasSrt> g_Srt : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(64, 1, 1)]
void CS_RtTlasFillInstances(uint thread_id : SV_DispatchThreadID)
{
  StructuredBuffer<SceneObjGpuBlas>               scene_obj_blases = DEREF(g_Srt.scene_obj_gpu_blases);
  RWStructuredBuffer<D3D12RaytracingInstanceDesc> instances        = DEREF(g_Srt.tlas_instance_descs);

  if (thread_id >= g_Srt.instance_count)
  {
    return;
  }

  SceneObjGpuBlas blas     = scene_obj_blases[thread_id];

  if (blas.addr == 0)
  {
    return;
  }

  u32             gpu_id    = blas.gpu_id;
  SceneObjGpu     obj       = g_SceneObjs[gpu_id];

  D3D12RaytracingInstanceDesc instance_desc;

  instance_desc.transform_x                        = obj.obj_to_world[0];
  instance_desc.transform_y                        = obj.obj_to_world[1];
  instance_desc.transform_z                        = obj.obj_to_world[2];

  instance_desc.instance_id                        = gpu_id;
  instance_desc.instance_mask                      = 0xFF;
  instance_desc.instance_contribution_to_hit_group = 0x0;
  instance_desc.flags                              = 0x0;
  instance_desc.blas_addr                          = blas.addr;

  instances[thread_id] = instance_desc;
}

