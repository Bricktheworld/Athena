#ifndef __RT_COMMON__
#define __RT_COMMON__
#include "../interlop.hlsli"
#include "../Include/math.hlsli"

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
    float3 position = snorm16_to_f32_x4(vertices[i].position).xyz;
    ret.position   += f32_to_snorm16_x4(float4(position * barycentrics[i], 1.0f));
    ret.normal     += vertices[i].normal * barycentrics[i];
    ret.uv         += vertices[i].uv * barycentrics[i];
  }

  ret.normal = normalize(ret.normal);

  return ret;
}

void load_vertices(uint start_index, uint start_vertex, uint triangle_idx, out Vertex vertices[3])
{
  for (uint i = 0; i < 3; i++)
  {
    uint idx    = g_IndexBuffer [start_index + triangle_idx + i];
    vertices[i] = g_VertexBuffer[start_vertex + idx];
  }
}

Vertex get_traced_vertex(uint instance_id, uint primitive_idx, float2 in_barycentrics)
{
  SceneObjGpu scene_obj = g_SceneObjs[instance_id];
  Vertex vertices[3];
  load_vertices(scene_obj.start_index, scene_obj.start_vertex, primitive_idx * 3, vertices);

  float3 barycentrics = float3((1.0f - in_barycentrics.x - in_barycentrics.y), in_barycentrics.x, in_barycentrics.y);
  return interpolate_vertex(vertices, barycentrics);
}

Vertex get_traced_vertex(BuiltInTriangleIntersectionAttributes attr)
{
  return get_traced_vertex(InstanceID(), PrimitiveIndex(), attr.barycentrics);
}

template <uint flags>
Vertex get_traced_vertex(RayQuery<flags> query)
{
  return get_traced_vertex(query.CommittedInstanceID(), query.CommittedPrimitiveIndex(), query.CommittedTriangleBarycentrics());
}

#endif

#endif