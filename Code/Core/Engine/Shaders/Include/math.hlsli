#ifndef __MATH__
#define __MATH__

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

#endif