#ifndef __ROOT_SIGNATURE__
#define __ROOT_SIGNATURE__

#include "interlop.hlsli"

// Root 

// GRV temporals
#define kViewportBufferSlot 0
#define kRenderSettingsSlot 1

// GRV globals
#define kSceneObjBufferSlot 0
#define kMaterialBufferSlot 1
#define kRaytracingAccelerationStructureSlot 2
#define kRtObjBufferSlot 3
#define kBlueNoiseUnormSlot 4
#define kBlueNoiseVec2UnormSlot 5
#define kBlueNoiseVec3UnormSlot 6

#define kDebugArgsBufferSlot 32
#define kDebugVertexBufferSlot 33
#define kDebugSdfBufferSlot 34

#define kIndexBufferSlot           2
#define kVertexBufferSlot          3

#define kGrvTemporalTableSlot     11
#define kGrvTableSlot             12

#define kDebugMaxVertices       (1 << 16)
#define kDebugMaxSdfs           (1 << 16)

#define kBindlessParamsSlot        b0

#define kIndirectCommandIndexSlot 10

#define kGrvCbvCount 16
#define kGrvSrvCount 32
#define kGrvUavCount 16
#define kGrvTemporalCount (kGrvCbvCount)
#define kGrvCount (kGrvSrvCount + kGrvUavCount)

#ifndef __cplusplus

#define BINDLESS_ROOT_SIGNATURE \
  "RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED)," \
  "RootConstants(b0, num32BitConstants=25, visibility=SHADER_VISIBILITY_ALL)," \
  "StaticSampler(s0, filter = FILTER_MIN_MAG_LINEAR_MIP_POINT, addressU = TEXTURE_ADDRESS_WRAP,  addressV = TEXTURE_ADDRESS_WRAP,  addressW = TEXTURE_ADDRESS_WRAP,  mipLODBias = 0.0f, minLOD = 0.0f, maxLOD = 100.0f),"\
  "StaticSampler(s1, filter = FILTER_MIN_MAG_LINEAR_MIP_POINT, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, addressW = TEXTURE_ADDRESS_CLAMP, mipLODBias = 0.0f, minLOD = 0.0f, maxLOD = 100.0f),"\
  "StaticSampler(s2, filter = FILTER_MINIMUM_MIN_MAG_LINEAR_MIP_POINT, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, addressW = TEXTURE_ADDRESS_CLAMP, mipLODBias = 0.0f, minLOD = 0.0f, maxLOD = 100.0f),"\
  "StaticSampler(s3, filter = FILTER_MIN_MAG_MIP_POINT, addressU = TEXTURE_ADDRESS_WRAP,  addressV = TEXTURE_ADDRESS_WRAP,  addressW = TEXTURE_ADDRESS_WRAP,  mipLODBias = 0.0f, minLOD = 0.0f, maxLOD = 100.0f),"\
  "SRV(t0),"\
  "SRV(t1),"\
  "SRV(t2),"\
  "CBV(b2),"\
  "SRV(t3),"\
  "SRV(t4),"\
  "UAV(u125),"\
  "UAV(u126),"\
  "UAV(u127),"\
  "RootConstants(b3, num32BitConstants=1,  visibility=SHADER_VISIBILITY_ALL)," \
  "DescriptorTable(CBV(b112, numDescriptors=16, flags = DESCRIPTORS_VOLATILE)),"\
  "DescriptorTable(SRV(t128, numDescriptors=32, flags = DESCRIPTORS_VOLATILE), UAV(u128, numDescriptors=16, flags = DESCRIPTORS_VOLATILE))"

  // These will always be static
SamplerState                              g_BilinearSamplerWrap   : register(s0);
SamplerState                              g_BilinearSamplerClamp  : register(s1);
SamplerState                              g_MinSamplerClamp       : register(s2);
SamplerState                              g_PointSamplerWrap      : register(s3);

// Can't move these to GRVs for now because of stupid reasons
StructuredBuffer<u16>                     g_IndexBuffer           : register(t1);
StructuredBuffer<Vertex>                  g_VertexBuffer          : register(t2);

struct MultiDrawIndirectDrawId
{
  uint draw_id;
};

/////// Multi Draw Indirect //////
ConstantBuffer<MultiDrawIndirectDrawId>   g_MultiDrawIndirect     : register(b3);

////////////// GRVs //////////////

StructuredBuffer<SceneObjGpu>             g_SceneObjs             : register(t128);
StructuredBuffer<MaterialGpu>             g_Materials             : register(t129);
RaytracingAccelerationStructure           g_AccelerationStructure : register(t130);
StructuredBuffer<RtObjGpu>                g_RtObjs                : register(t131);
Texture2DArray<float>                     g_BlueNoiseUnorm        : register(t132);
Texture2DArray<float2>                    g_BlueNoiseVec2Unorm    : register(t133);
Texture2DArray<float3>                    g_BlueNoiseUnitVec3     : register(t134);

//////////// Temporal ////////////
ConstantBuffer<ViewportGpu>               g_ViewportBuffer        : register(b112);
ConstantBuffer<RenderSettingsGpu>         g_RenderSettings        : register(b113);

RWStructuredBuffer<MultiDrawIndirectArgs> g_DebugArgsBuffer       : register(u128);
RWStructuredBuffer<DebugLinePoint>        g_DebugLineVertexBuffer : register(u129);
RWStructuredBuffer<DebugSdf>              g_DebugSdfBuffer        : register(u130);

#endif

#endif