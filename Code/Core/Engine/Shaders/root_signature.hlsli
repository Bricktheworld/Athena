#ifndef __ROOT_SIGNATURE__
#define __ROOT_SIGNATURE__

#include "interlop.hlsli"

#define BINDLESS_ROOT_SIGNATURE \
  "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | SAMPLER_HEAP_DIRECTLY_INDEXED)," \
  "RootConstants(b0, num32BitConstants=54, visibility=SHADER_VISIBILITY_ALL)," \
  "StaticSampler(s0, filter = FILTER_MIN_MAG_LINEAR_MIP_POINT, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, addressW = TEXTURE_ADDRESS_CLAMP, mipLODBias = 0.0f, minLOD = 0.0f, maxLOD = 100.0f),"\
  "SRV(t0),"\
  "SRV(t1),"\
  "SRV(t2),"\
  "UAV(u126),"\
  "UAV(u127)"

SamplerState g_ClampSampler : register(s0);

#define kAccelerationStructureSlot 0
#define kIndexBufferSlot           1
#define kVertexBufferSlot          2
#define kDebugVertexBufferSlot     3
#define kDebugArgsBufferSlot       4


#define kDebugMaxVertices       8192

// For ray-tracing
RaytracingAccelerationStructure    g_AccelerationStructure : register(t0);
ByteAddressBuffer                  g_IndexBuffer           : register(t1);
StructuredBuffer<interlop::Vertex> g_VertexBuffer          : register(t2);

#endif