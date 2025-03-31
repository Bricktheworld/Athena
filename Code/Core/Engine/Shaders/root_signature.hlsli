#ifndef __ROOT_SIGNATURE__
#define __ROOT_SIGNATURE__

#include "interlop.hlsli"

#define kAccelerationStructureSlot 1
#define kIndexBufferSlot           2
#define kVertexBufferSlot          3
#define kViewportBufferSlot        4
#define kSceneObjBufferSlot        5
#define kMaterialBufferSlot        6

#define kDebugArgsBufferSlot       7
#define kDebugVertexBufferSlot     8
#define kDebugSdfBufferSlot        9

#define kDebugMaxVertices       8192
#define kDebugMaxSdfs           8192

#define kBindlessParamsSlot        b0

#ifndef __cplusplus

#define BINDLESS_ROOT_SIGNATURE \
  "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED)," \
  "RootConstants(b0, num32BitConstants=27, visibility=SHADER_VISIBILITY_ALL)," \
  "StaticSampler(s0, filter = FILTER_MIN_MAG_LINEAR_MIP_POINT, addressU = TEXTURE_ADDRESS_WRAP,  addressV = TEXTURE_ADDRESS_WRAP,  addressW = TEXTURE_ADDRESS_WRAP,  mipLODBias = 0.0f, minLOD = 0.0f, maxLOD = 100.0f),"\
  "StaticSampler(s1, filter = FILTER_MIN_MAG_LINEAR_MIP_POINT, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, addressW = TEXTURE_ADDRESS_CLAMP, mipLODBias = 0.0f, minLOD = 0.0f, maxLOD = 100.0f),"\
  "SRV(t0),"\
  "SRV(t1),"\
  "SRV(t2),"\
  "CBV(b1),"\
  "SRV(t3),"\
  "SRV(t4),"\
  "UAV(u125),"\
  "UAV(u126),"\
  "UAV(u127),"\
  "DescriptorTable(CBV(b120, numDescriptors=8), SRV(t128, numDescriptors=128), UAV(u128, numDescriptors=128))"

SamplerState                              g_BilinearSamplerWrap   : register(s0);
SamplerState                              g_BilinearSamplerClamp  : register(s1);
RaytracingAccelerationStructure           g_AccelerationStructure : register(t0);
ByteAddressBuffer                         g_IndexBuffer           : register(t1);
StructuredBuffer<Vertex>                  g_VertexBuffer          : register(t2);
ConstantBuffer<Viewport>                  g_ViewportBuffer        : register(b1);
StructuredBuffer<SceneObjGpu>             g_SceneObjs             : register(t3);
StructuredBuffer<MaterialGpu>             g_Materials             : register(t4);
RWStructuredBuffer<MultiDrawIndirectArgs> g_DebugArgsBuffer       : register(u125);
RWStructuredBuffer<DebugLinePoint>        g_DebugLineVertexBuffer : register(u126);
RWStructuredBuffer<DebugSdf>              g_DebugSdfBuffer        : register(u127);

#endif

#endif