#include "../root_signature.hlsli"

struct VSOutput
{
  float4 position : SV_POSITION;
  float2 uv : TEXCOORD0;
};

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
VSOutput main(uint vert_id: SV_VertexID)
{
  VSOutput ret;

  ret.uv = float2((vert_id << 1) & 2, vert_id & 2);
  ret.position = float4(ret.uv.x * 2.0f - 1.0f, ret.uv.y * -2.0f + 1.0f, 0.0f, 1.0f);

  return ret;
}