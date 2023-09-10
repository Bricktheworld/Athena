#ifndef __RT_COMMON__
#define __RT_COMMON__
#include "../interlop.hlsli"

// TODO(Brandon): Eventually when we do full material's we will want to include more data than this...
struct Payload
{
	      float  t;
				uint   hit_kind;
	      float3 ws_pos;
	unorm float3 normal;
	unorm	float2 uv;
};

interlop::Vertex interpolate_vertex(interlop::Vertex vertices[3], float3 barycentrics)
{
	interlop::Vertex ret = (interlop::Vertex)0;

	for (uint i = 0; i < 3; i++)
	{
		ret.position += vertices[i].position * barycentrics[i];
		ret.normal += vertices[i].normal * barycentrics[i];
		ret.uv += vertices[i].uv * barycentrics[i];
	}

	ret.normal = normalize(ret.normal);

	return ret;
}

void load_vertices(uint primitive_index, out interlop::Vertex vertices[3])
{
	uint3 indices = g_IndexBuffer.Load3(PrimitiveIndex() * 3 * 4);
	for (uint i = 0; i < 3; i++)
	{
		vertices[i] = g_VertexBuffer[indices[i]];
	}
}

interlop::Vertex get_vertex(BuiltInTriangleIntersectionAttributes attr)
{
	interlop::Vertex vertices[3];
	load_vertices(PrimitiveIndex(), vertices);

	float3 barycentrics = float3((1.0f - attr.barycentrics.x - attr.barycentrics.y), attr.barycentrics.x, attr.barycentrics.y);
	return interpolate_vertex(vertices, barycentrics);
}

#endif