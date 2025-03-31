#include "../interlop.hlsli"
#include "../root_signature.hlsli"

struct VSDebugLineOutput
{
  float4 position : SV_Position;
  float4 color    : COLOR0;
};

ConstantBuffer<DebugLineDrawSrt> g_DebugDrawLineSrt : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
VSDebugLineOutput VS_DebugDrawLine(uint vert_id: SV_VertexID)
{
  VSDebugLineOutput ret;

  StructuredBuffer<DebugLinePoint> vertex_buffer = DEREF(g_DebugDrawLineSrt.debug_line_vert_buffer);

  DebugLinePoint vertex = vertex_buffer[vert_id % kDebugMaxVertices];

  // Positions are already world space, need to be in ndc
  float4 ws_pos  = float4(vertex.position, 1.0);
  float4 ndc_pos = mul(g_ViewportBuffer.view_proj, ws_pos);
  ret.position   = ndc_pos;
  ret.color      = float4(vertex.color, 1.0);

  return ret;
}


struct VSDebugSdfOutput
{
  float4 position    : SV_Position;
  float3 ray_dir     : RayDirection;
  uint   instance_id : InstanceID;
};

ConstantBuffer<DebugSdfDrawSrt> g_DebugDrawSdfSrt : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
VSDebugSdfOutput VS_DebugDrawSdf(uint vert_id: SV_VertexID, uint instance_id: SV_InstanceID)
{
  VSDebugSdfOutput ret;

  StructuredBuffer<DebugSdf> sdf_buffer = DEREF(g_DebugDrawSdfSrt.debug_sdf_buffer);

  static const float2 kVertexPositions[] =
  {
    float2(-1.0, -1.0), float2( 1.0, -1.0), float2(-1.0, 1.0),
    float2(-1.0,  1.0), float2( 1.0, -1.0), float2( 1.0, 1.0),
  };

  static const float2 kVertexUvs[] = 
  {
    float2(0.0, 1.0), float2(1.0, 1.0), float2(0.0, 0.0),
    float2(0.0, 0.0), float2(1.0, 1.0), float2(1.0, 0.0),
  };

  DebugSdf sdf   = sdf_buffer[instance_id % kDebugMaxSdfs];

  // Billboard the SDF towards the camera
  float4x4 camera_transform = transpose(g_ViewportBuffer.view);

  float3 right     = mul(camera_transform, float4(-1.3, 0.0, 0.0, 0.0)).xyz;
  float3 up        = mul(camera_transform, float4( 0.0, 1.3, 0.0, 0.0)).xyz;

  float2 ls_offset = kVertexPositions[vert_id] * sdf.radius;
  float3 ws_offset = ls_offset.x * right + ls_offset.y * up;
  float4 ws_pos    = float4(sdf.position + ws_offset, 1.0);
  float4 ndc_pos   = mul(g_ViewportBuffer.view_proj, ws_pos);

  ret.position     = ndc_pos;
  ret.instance_id  = instance_id;
  ret.ray_dir      = normalize(ws_pos.xyz - g_ViewportBuffer.camera_world_pos.xyz);

  return ret;
}
