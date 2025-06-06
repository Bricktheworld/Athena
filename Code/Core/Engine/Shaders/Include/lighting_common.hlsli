#ifndef __LIGHTING_COMMON__
#define __LIGHTING_COMMON__


#include "../include/rt_common.hlsli"
#include "../include/math.hlsli"


// One thing to note, lights are "pre-exposed" on the CPU
// before any of these computations are ever reached. This can
// make things confusing but it makes the most sense so as to not completely
// destroy precision.

// Sterdian angle
struct SterdianAngle
{
  float m_Value;
};

// Meters squared
struct MeterArea
{
  float m_Value;
};

// Lux (illuminance, lm/m^2)
template <typename T>
struct Lux_T
{
  T m_Value;

  Lux_T<T> attenuated(float atten)
  {
    Lux_T<T> ret;
    ret.m_Value = m_Value * atten;
    return ret;
  }
};

// Luminance (lm/sr/m^2)
template <typename T>
struct Nits_T
{
  T m_Value;

  // Luminance -> Illuminance
  // Assumes that in a hemisphere, luminance is 0 everywhere except at ω = L
  // (not suitable for area lights, only point lights)
  Lux_T<T> operator*(float NdotL)
  {
    // Let Lᵢ(ωᵢ) be the luminance in a incoming direction of light ωᵢ
    // 
    // dωᵢ has units dsr (aka differential sterdian) so if you integrate
    // ∫dωᵢ that gives you units of sr
    // 
    // Since Lᵢ(ωᵢ) is 0 everywhere except ωᵢ = L, if you do an integral
    //
    // ∫ Lᵢ(ωᵢ)(ωᵢ⋅n̂)dωᵢ = Lᵢ(L)(n⋅L̂) ∫dωᵢ
    // 
    // This gives you units of 
    //    Lᵢ(L) = nits = lm/sr/m^2
    //    (n⋅L̂) = unitless
    //    ∫dωᵢ = sr
    // so Lᵢ(L)(n⋅L̂) ∫dωᵢ = nits * sr = lm/sr/m^2 * sr = lm/m^2

    Lux_T<T> ret;
    ret.m_Value = m_Value * NdotL;
    return ret;
  }
};

// Luminous Intensity (lm/sr)
template <typename T>
struct Candela_T
{
  T m_Value;

  Nits_T<T> operator/(MeterArea area)
  {
    Nits_T<T> ret;
    ret.m_Value = m_Value / area.m_Value;
    return ret;
  }
};

// Lumens (luminous power)
template <typename T>
struct Lumens_T
{
  T m_Value;

  Candela_T<T> operator/(SterdianAngle angle)
  {
    Candela_T<T> ret;
    ret.m_Value = m_Value / angle.m_Value;
    return ret;
  }

  Lux_T<T> operator/(MeterArea area)
  {
    Lux_T<T> ret;
    ret.m_Value = m_Value / area.m_Value;
    return ret;
  }
};

// BRDF
struct BRDF
{
  float3 m_Fr;
  float3 m_Fd;

  float3 get_brdf()
  {
    return m_Fr * m_Fd;
  }
};

// BTDF
struct BTDF
{
  float3 m_Value;
};

using Lux3 = Lux_T<float3>;
using Nits3 = Nits_T<float3>;
using Candela3 = Candela_T<float3>;
using Lumens3 = Lumens_T<float3>;

// Bidirectional scattering distribution function
// Ratio of illuminance to luminance percievable at a viewpoint
// (1/sr)
struct BSDF
{
  BRDF  m_BRDF;
  BTDF  m_BTDF;
  float m_NdotL;
  float m_NdotH;
  float m_NdotV;
  float m_LdotH;

  // Illuminance -> Luminance
  Nits3 operator*(Lux3 illuminance)
  {
    float3 bsdf             = m_BRDF.get_brdf() * m_BTDF.m_Value;

    // Amount of illuminance that is perpendicular to surface
    float3 illuminance_perp = (illuminance.m_Value * m_NdotL);
    Nits3 ret;

    // cd   = lm/sr
    // lux  = lm/m^2
    // nits = cd/m^2
    //
    //   => lux/sr = lm/(sr*m^2) = cd/m^2 = nits
    //   => nits = cd/m^2 = lux * 1/sr
    ret.m_Value = bsdf * illuminance_perp;
    return ret;
  }
};

float distribution_ggx(float NdotH, float roughness)
{
    float a      = roughness * roughness;
    float a2     = a * a;
    float NdotH2 = NdotH * NdotH;

    float f      = (NdotH * a2 - NdotH) * NdotH + 1.0f;
    return a2 / (kPI * f * f);
}

float geometry_smith(float NdotV, float NdotL, float roughness)
{
    float a    = roughness * roughness;
    float a2   = a * a;
    float GGXL = NdotV * sqrt((-NdotL * a2 + NdotL) * NdotL + a2);
    float GGXV = NdotL * sqrt((-NdotV * a2 + NdotV) * NdotV + a2);
    return 0.5f / (GGXV + GGXL);
}

float3 fresnel_schlick(float HdotV, float3 f0)
{
    return f0 + (float3(1.0, 1.0, 1.0) - f0) * pow(1.0 - HdotV, 5.0);
}

BSDF cook_torance_bsdf(
  float3 light_direction,
  float3 view_direction,
  float3 normal,
  float roughness,
  float metallic,
  float3 diffuse
) {
  float3 N = normal;
  float3 V = view_direction;
  float3 L = -normalize(light_direction);

  float3 H = normalize(view_direction + light_direction);

  BSDF ret;
  ret.m_NdotV = abs(dot(N, V)) + 1e-5f;
  ret.m_NdotL = clamp(dot(N, L), 0.0f, 1.0f);
  ret.m_NdotH = clamp(dot(N, H), 0.0f, 1.0f);
  ret.m_LdotH = clamp(dot(L, H), 0.0f, 1.0f);

  // The Fresnel-Schlick approximation expects a F0 parameter which is known as the surface reflection at zero incidence
  // or how much the surface reflects if looking directly at the surface.
  //
  // The F0 varies per material and is tinted on metals as we find in large material databases.
  // In the PBR metallic workflow we make the simplifying assumption that most dielectric surfaces look visually correct with a constant F0 of 0.04.
  float3 f0          = float3(0.04, 0.04, 0.04);
  f0                 = lerp(f0, diffuse, metallic);

  // Cook torrance BRDF
  float  D           = distribution_ggx(ret.m_NdotH, roughness);
  float3 F           = fresnel_schlick(ret.m_LdotH, f0);
  float  G           = geometry_smith(ret.m_NdotV, ret.m_NdotL, roughness);

  // Specular
  ret.m_BRDF.m_Fr    = (D * G) * F;
  // Diffuse
  ret.m_BRDF.m_Fd    = diffuse / kPI;

  ret.m_BTDF.m_Value = 1.0f;

  return ret;
}

BSDF lambertian_diffuse_bsdf(float3 normal, float3 light_direction, float3 diffuse)
{
  float3 N = normal;
  float3 L = -normalize(light_direction);

  BSDF ret;
  ret.m_NdotV = 0.0f;
  ret.m_NdotL = clamp(dot(N, L), 0.0f, 1.0f);
  ret.m_NdotH = 0.0f;
  ret.m_LdotH = 0.0f;

  // Entirely diffuse, no specular
  ret.m_BRDF.m_Fr = 0.0;
  ret.m_BRDF.m_Fd = diffuse / kPI;

  ret.m_BTDF.m_Value = 1.0f;

  return ret;
}

float3 evaluate_directional_light(
  float3 light_direction,
  float  illuminance,
  float3 view_direction,
  float3 normal,
  float roughness,
  float metallic,
  float3 diffuse
) {
  return 0.0;
}


float light_visibility(
  float3 light_direction,
  float3 ws_pos,
  float3 normal,
  float t_max,
  float normal_bias
) {
  RayDesc ray;
  ray.Origin    = ws_pos + normal * normal_bias;
  ray.Direction = normalize(-light_direction);
  ray.TMin      = 0.01f;
  ray.TMax      = t_max;

  Payload payload = (Payload)0;
  TraceRay(
    g_AccelerationStructure,
    RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
    0xFF,
    0,
    0,
    0,
    ray,
    payload
  );

  return payload.t < 0.0f;
}

#endif