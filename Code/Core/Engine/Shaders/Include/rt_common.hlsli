#ifndef __RT_COMMON__
#define __RT_COMMON__
#include "../interlop.hlsli"

// TODO(Brandon): Eventually when we do full material's we will want to include more data than this...
struct Payload
{
  float  t;
  uint   hit_kind;
  float3 ws_pos;
  float3 normal;
  float2 uv;
};

#ifndef __cplusplus
Vertex interpolate_vertex(Vertex vertices[3], float3 barycentrics)
{
  Vertex ret = (Vertex)0;

  for (uint i = 0; i < 3; i++)
  {
    ret.position += vertices[i].position * barycentrics[i];
    ret.normal += vertices[i].normal * barycentrics[i];
    ret.uv += vertices[i].uv * barycentrics[i];
  }

  ret.normal = normalize(ret.normal);

  return ret;
}

void load_vertices(uint primitive_index, out Vertex vertices[3])
{
  uint3 indices = g_IndexBuffer.Load3(primitive_index * 3 * 4);
  for (uint i = 0; i < 3; i++)
  {
    vertices[i] = g_VertexBuffer[indices[i]];
  }
}

Vertex get_traced_vertex(uint primitive_idx, float2 in_barycentrics)
{
  Vertex vertices[3];
  load_vertices(primitive_idx, vertices);

  float3 barycentrics = float3((1.0f - in_barycentrics.x - in_barycentrics.y), in_barycentrics.x, in_barycentrics.y);
  return interpolate_vertex(vertices, barycentrics);
}

Vertex get_traced_vertex(BuiltInTriangleIntersectionAttributes attr)
{
  return get_traced_vertex(PrimitiveIndex(), attr.barycentrics);
}

#endif

#endif