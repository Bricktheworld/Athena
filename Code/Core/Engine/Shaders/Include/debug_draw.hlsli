#ifndef __DEBUG_DRAW__
#define __DEBUG_DRAW__
#include "../interlop.hlsli"
#include "../root_signature.hlsli"

struct DebugLinePoint
{
	float3 position;
//	float4 color;
};

RWStructuredBuffer<DebugLinePoint> g_DebugLineVertexBuffer : register(u126)
RWByteAddressBuffer                g_DebugLineArgsBufer    : register(u127)


void debug_draw_line(float3 start, float3 end, float4 color)
{
	uint vertex_buffer_offset = 0;
	g_DebugLineArgsBuffer.InterlockedAdd(0, 2, vertex_buffer_offset);

	if (vertex_buffer_offset >= kDebugMaxVertices)
	{
		return;
	}

	g_DebugLineVertexBuffer[vertex_buffer_offset + 0].position = start;
	g_DebugLineVertexBuffer[vertex_buffer_offset + 1].position = end;
}

#endif