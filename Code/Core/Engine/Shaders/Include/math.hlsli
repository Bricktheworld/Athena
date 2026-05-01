#ifndef __MATH__
#define __MATH__
#include "../root_signature.hlsli"

#ifndef __cplusplus
static const float kPI  = 3.14159265359;
static const float k2PI = 6.2831853071795864f;

static const float kGoldenAngle = 2.4f;

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

float luma_rec709(float3 color)
{
  return 0.2126f * color.r + 0.7152f * color.g + 0.0722f * color.b;
}

template<int N>
vector<f32, N> snorm16_to_f32(vector<s16, N> packed)
{
  return max((vector<f32, N>)packed / 32767.0f, -1.0f);
}

template<int N>
vector<s16, N>  f32_to_snorm16(vector<f32, N> v)
{
  return (vector<s16, N>)clamp(select(v >= 0.0f, (v * 32767.0f + 0.5f), (v * 32767.0f - 0.5f)), -32768.0f, 32767.0f);
}

template<int N>
vector<f32, N> unorm16_to_f32(vector<u16, N> packed)
{
  return max((vector<f32, N>)packed / 65535.0f, 0.0f);
}

template<int N>
vector<u16, N> f32_to_unorm16(vector<f32, N> v)
{
  return (vector<u16, N>)clamp(v * 65535.0f + 0.5f, 0.0f, 65535.0f);
}

#endif

#endif