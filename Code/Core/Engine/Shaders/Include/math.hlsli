#ifndef __MATH__
#define __MATH__
#include "../root_signature.hlsli"

static const float  kPI  = 3.14159265359;
static const float  k2PI = 6.2831853071795864f;

float4 quaternion_conjugate(float4 q)
{
  return float4(-q.xyz, q.w);
}

float3 quaternion_rotate(float3 v, float4 q)
{
  float3 b = q.xyz;
  float b2 = dot(b, b);
  return (v * (q.w * q.w - b2) + b * (dot(v, b) * 2.f) + cross(b, v) * (q.w * 2.f));
}

float4 screen_to_world(float3 screen, float2 screen_size)
{
  float2 normalized_screen = screen.xy / screen_size * 2.0f - float2(1.0f, 1.0f);
  normalized_screen.y     *= -1.0f;

  float4 clip              = float4(normalized_screen - g_ViewportBuffer.taa_jitter, screen.z, 1.0f);

  float4 world             = mul(g_ViewportBuffer.inverse_view_proj, clip);
  world                   /= world.w;

  return world;
}

#endif