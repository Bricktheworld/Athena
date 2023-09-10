#include "../root_signature.hlsli"
#include "../interlop.hlsli"

struct VSOutput
{
  float4 position : SV_POSITION;
};

ConstantBuffer<interlop::CubeRenderResources> render_resources : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
VSOutput main(uint vert_id: SV_VertexID)
{
  VSOutput ret;

  StructuredBuffer<float4> position_buf = ResourceDescriptorHeap[render_resources.position];
  ConstantBuffer<interlop::Scene> scene_buf = ResourceDescriptorHeap[render_resources.scene];
  ConstantBuffer<interlop::Transform> transform_buf = ResourceDescriptorHeap[render_resources.transform];

  ret.position = mul(scene_buf.view_proj, position_buf[vert_id]);
  // ret.position = position_buf[vert_id];
  return ret;
}