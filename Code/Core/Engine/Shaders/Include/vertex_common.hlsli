#ifndef __VERTEX_COMMON__
#define __VERTEX_COMMON__
#include "../interlop.hlsli"

#ifndef __cplusplus
VertexUncompressed decompress_vertex(Vertex vertex)
{
  VertexUncompressed ret;
  ret.position = snorm16_to_f32(vertex.position).xyz;
  ret.normal   = vertex.normal.xyz;
  ret.uv       = snorm16_to_f32(vertex.uv) * asfloat16(vertex.position.w);
  return ret;
}
#endif

#endif