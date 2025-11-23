#pragma once

struct RtBuildTlasSrt
{
  u32 scene_obj_count;
  RWStructuredBufferPtr<D3D12RaytracingInstanceDesc> tlas_instance_descs;
};
