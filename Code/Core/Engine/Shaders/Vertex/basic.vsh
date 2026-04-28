#include "../root_signature.hlsli"
#include "../interlop.hlsli"

#include "../Include/math.hlsli"
#include "../Include/vertex_common.hlsli"
#include "../Include/gbuffer_common.hlsli"

BasicVSOut VSEntryCommon(uint gpu_id, uint vert_id)
{
  BasicVSOut ret;

  SceneObjGpu scene_obj = g_SceneObjs[gpu_id];

  Vertex compressed_vertex = g_VertexBuffer[scene_obj.start_vertex + vert_id];
  VertexUncompressed vertex = decompress_vertex(compressed_vertex);

  static const float4x4 kIdentity =
  {
    1, 0, 0, 0,
    0, 1, 0, 0,
    0, 0, 1, 0,
    0, 0, 0, 1
  };

  float3 obj_pos = vertex.position.xyz;
  ret.world_pos  = mul(scene_obj.obj_to_world, float4(obj_pos, 1.0f));
  ret.ndc_pos    = mul(g_ViewportBuffer.view_proj, ret.world_pos);

  ret.curr_pos  = ret.ndc_pos;
  ret.prev_pos  = mul(g_ViewportBuffer.prev_view_proj, ret.world_pos);

  ret.ndc_pos  += float4(g_ViewportBuffer.taa_jitter * ret.ndc_pos.w, 0.0f, 0.0f);

  float3x3 normal_matrix = (float3x3)transpose(kIdentity);
  float3   normal        = normalize(mul(normal_matrix, vertex.normal.xyz));

  // tangent = normalize(tangent - mul(dot(tangent, normal), normal));

  // float3 bitangent = cross(normal, tangent);
  // float3x3 tbn_matrix = float3x3(tangent, bitangent, normal);

  ret.normal    = normal;
  ret.uv        = vertex.uv;

  ret.obj_id    = gpu_id;
  ret.mat_id    = scene_obj.mat_id;
  return ret;
}


ConstantBuffer<GBufferIndirectSrt> g_GBufferIndirectSrt : register(b0);
[RootSignature(BINDLESS_ROOT_SIGNATURE)]
BasicVSOut VS_MultiDrawIndirectIndexed(uint vert_id: SV_VertexID)
{
  StructuredBuffer<u32> scene_obj_gpu_ids = DEREF(g_GBufferIndirectSrt.scene_obj_gpu_ids);
  u32 gpu_id = scene_obj_gpu_ids[g_MultiDrawIndirect.draw_id];

  return VSEntryCommon(gpu_id, vert_id);
}
