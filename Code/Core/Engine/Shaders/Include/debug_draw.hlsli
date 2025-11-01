#ifndef __DEBUG_DRAW__
#define __DEBUG_DRAW__
#include "../interlop.hlsli"
#include "../root_signature.hlsli"
#include "../Include/math.hlsli"

#if !defined(__cplusplus)

bool mouse_intersect_sphere(float3 center_ws, float radius)
{
  float3 mouse_pos_ws     = screen_to_world(float3(g_RenderSettings.mouse_pos, 1.0f), 1.0f).xyz;
  float3 ray_origin_ws    = g_ViewportBuffer.camera_world_pos.xyz;
  float3 ray_dir          = normalize(mouse_pos_ws - ray_origin_ws);

  float3 center_ls        = center_ws - ray_origin_ws;
  float3 n_parallel       = dot(center_ls, ray_dir) * ray_dir;
  float3 n_perpendicular  = center_ls - n_parallel;

  float  radius_squared   = radius * radius;
  float  perp_len_squared = dot(n_perpendicular, n_perpendicular);
  float  discriminant     = radius_squared - perp_len_squared;
  return discriminant >= 0.0f;
}

void debug_draw_line(float3 start, float3 end, float3 color)
{
  uint vertex_buffer_offset = 0;
  InterlockedAdd(g_DebugArgsBuffer[0].vertex_count_per_instance, 2, vertex_buffer_offset);

  if (vertex_buffer_offset >= kDebugMaxVertices)
  {
    return;
  }

  g_DebugLineVertexBuffer[vertex_buffer_offset + 0].position = start;
  g_DebugLineVertexBuffer[vertex_buffer_offset + 1].position = end;

  g_DebugLineVertexBuffer[vertex_buffer_offset + 0].color    = color;
  g_DebugLineVertexBuffer[vertex_buffer_offset + 1].color    = color;
}

void debug_draw_sphere(float3 center, float radius, float3 color)
{
  uint instance_offset = 0;
  InterlockedAdd(g_DebugArgsBuffer[1].instance_count, 1, instance_offset);

  if (instance_offset >= kDebugMaxSdfs)
  {
    return;
  }

  DebugSdf sdf;
  sdf.position  = center;
  sdf.radius    = radius;
  sdf.color     = color;
  sdf.type      = kSdfTypeSphere;
  sdf.luminance = SH::L1_F16_RGB::Zero();

  g_DebugSdfBuffer[instance_offset] = sdf;
}

void debug_draw_spherical_harmonic(float3 center, float radius, SH::L1_F16_RGB luminance)
{
  uint instance_offset = 0;
  InterlockedAdd(g_DebugArgsBuffer[1].instance_count, 1, instance_offset);

  if (instance_offset >= kDebugMaxSdfs)
  {
    return;
  }

  DebugSdf sdf;
  sdf.position  = center;
  sdf.radius    = radius;
  sdf.color     = 1.0f;
  sdf.type      = kSdfTypeSphericalHarmonic;
  sdf.luminance = luminance;

  g_DebugSdfBuffer[instance_offset] = sdf;
}
#endif

#endif