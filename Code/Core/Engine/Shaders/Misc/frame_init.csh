#include "../root_signature.hlsli"
#include "../interlop.hlsli"

ConstantBuffer<MaterialUploadSrt> g_Srt : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(64, 1, 1)]
void CS_MaterialUpload(uint thread_id : SV_DispatchThreadID)
{
  if (thread_id >= g_Srt.count)
  {
    return;
  }

  RWStructuredBuffer<MaterialGpu>     material_gpu_buffer  = DEREF(g_Srt.material_gpu_buffer);

  StructuredBuffer<MaterialUploadCmd> upload_material_cmds = DEREF(g_Srt.material_uploads);

  MaterialUploadCmd cmd = upload_material_cmds[thread_id];

  u32 gpu_id = cmd.mat_gpu_id;

  material_gpu_buffer[gpu_id] = cmd.material;
}