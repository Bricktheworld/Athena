#pragma once
#include "../interlop.hlsli"

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

struct DebugBVHSrt
{
  RWTexture2DPtr<float4> dst;
};
