#pragma once
#include "../interlop.hlsli"
#include "../Include/ddgi_common.hlsli"

struct DebugNormalSrt
{
  Texture2DPtr<float4>   gbuffer_normal_roughness;
  RWTexture2DPtr<float4> dst;
};

struct DebugDepthSrt
{
  Texture2DPtr<float>    gbuffer_depth;
  RWTexture2DPtr<float4> dst;
};

struct DebugGiVarianceSrt
{
  Texture2DPtr<float4>                gbuffer_normal_roughness;
  Texture2DPtr<float>                 gbuffer_depth;
  Texture2DPtr<Vec4f16>               lighting;
  // Diffuse GI resources
  StructuredBufferPtr<DiffuseGiProbe> diffuse_gi_probes;
  Texture2DArrayPtr<u32>              diffuse_gi_page_table;
  RWTexture2DPtr<float4>              dst;
};

struct DebugBVHSrt
{
  RWTexture2DPtr<float4> dst;
};
