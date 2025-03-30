#include "../interlop.hlsli"
#include "../root_signature.hlsli"

struct VSOutput
{
  float4 position : SV_Position;
  float4 color    : COLOR0;
};

ConstantBuffer<DebugLineDrawSrt> g_Srt : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
VSOutput VS_DebugDrawLine(uint vert_id: SV_VertexID)
{
  VSOutput ret;

  StructuredBuffer<DebugLinePoint> vertex_buffer = DEREF(g_Srt.debug_line_vert_buffer);

  DebugLinePoint vertex = vertex_buffer[vert_id % kDebugMaxVertices];

  // Positions are already world space, need to be in ndc
  float4 ws_pos  = float4(vertex.position, 1.0);
  float4 ndc_pos = mul(g_ViewportBuffer.view_proj, ws_pos);
  ret.position   = ndc_pos;
  ret.color      = float4(vertex.color, 1.0);

  return ret;
}