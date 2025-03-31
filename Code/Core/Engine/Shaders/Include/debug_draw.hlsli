#ifndef __DEBUG_DRAW__
#define __DEBUG_DRAW__
#include "../interlop.hlsli"
#include "../root_signature.hlsli"

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
  sdf.position = center;
  sdf.radius   = radius;
  sdf.color    = color;
  sdf.type     = kSdfTypeSphere;

  g_DebugSdfBuffer[instance_offset] = sdf;
}

#endif