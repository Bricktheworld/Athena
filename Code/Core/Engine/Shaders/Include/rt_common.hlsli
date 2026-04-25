#ifndef __RT_COMMON__
#define __RT_COMMON__
#include "../interlop.hlsli"
#include "../Include/math.hlsli"
#include "../Include/vertex_common.hlsli"

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
VertexUncompressed interpolate_vertex(Vertex vertices[3], float3 barycentrics)
{
  VertexUncompressed ret = (VertexUncompressed)0;

  for (uint i = 0; i < 3; i++)
  {
    VertexUncompressed v = decompress_vertex(vertices[i]);
    ret.position   += v.position * barycentrics[i];
    ret.normal     += v.normal   * barycentrics[i];
    ret.uv         += v.uv       * barycentrics[i];
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

VertexUncompressed get_traced_vertex(uint instance_id, uint primitive_idx, float2 in_barycentrics)
{
  SceneObjGpu scene_obj = g_SceneObjs[instance_id];
  Vertex vertices[3];
  load_vertices(scene_obj.start_index, scene_obj.start_vertex, primitive_idx * 3, vertices);

  float3 barycentrics = float3((1.0f - in_barycentrics.x - in_barycentrics.y), in_barycentrics.x, in_barycentrics.y);
  return interpolate_vertex(vertices, barycentrics);
}

VertexUncompressed get_traced_vertex(BuiltInTriangleIntersectionAttributes attr)
{
  return get_traced_vertex(InstanceID(), PrimitiveIndex(), attr.barycentrics);
}

template <uint flags>
VertexUncompressed get_traced_vertex(RayQuery<flags> query)
{
  return get_traced_vertex(query.CommittedInstanceID(), query.CommittedPrimitiveIndex(), query.CommittedTriangleBarycentrics());
}

#endif

#endif