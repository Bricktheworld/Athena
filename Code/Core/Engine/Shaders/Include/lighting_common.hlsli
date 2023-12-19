#ifndef __LIGHTING_COMMON__
#define __LIGHTING_COMMON__

#include "../include/rt_common.hlsli"
#include "../include/math.hlsli"

float distribution_ggx(float3 normal, float3 halfway_vector, float roughness)
{
    float a      = roughness * roughness;
    float a2     = a * a;
    float NdotH  = max(dot(normal, halfway_vector), 0.0);
    float NdotH2 = NdotH * NdotH;

    float nom    = a2;
    float denom  = (NdotH2 * (a2 - 1.0) + 1.0);
    denom     = kPI * denom * denom;

    return nom / max(denom, 0.0000001);
}

float geometry_schlick_ggx(float NdotV, float roughness)
{
    float r    = (roughness + 1.0);
    float k    = (r * r) / 8.0;

    float nom   = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return nom / denom;
}

float geometry_smith(float3 normal, float3 view_direction, float3 light_direction, float roughness)
{
    float NdotV = max(dot(normal, view_direction), 0.0);
    float NdotL = max(dot(normal, light_direction), 0.0);
    float ggx2  = geometry_schlick_ggx(NdotV, roughness);
    float ggx1  = geometry_schlick_ggx(NdotL, roughness);

    return ggx1 * ggx2;
}

float3 fresnel_schlick(float cos_theta, float3 f0)
{
    return f0 + (float3(1.0, 1.0, 1.0) - f0) * pow(max(1.0 - cos_theta, 0.0), 5.0);
}

// Rendering Equation: ∫ fᵣ(x,ωᵢ,ωₒ,λ,t) Lᵢ(x,ωᵢ,ωₒ,λ,t) (ωᵢ⋅n̂) dωᵢ

float3 evaluate_lambertian(float3 diffuse)
{
  return diffuse / kPI;
}

float3 evaluate_directional_radiance(float3 light_diffuse, float light_intensity)
{
  return light_diffuse * light_intensity;
}

float  evaluate_cos_theta(float3 light_direction, float3 normal)
{
  light_direction = -normalize(light_direction);
  return max(dot(light_direction, normal), 0.0f);
}

float3 evaluate_directional_light(float3 light_direction,
																	float3 light_diffuse,
																	float  light_intensity,
																	float3 view_direction,
																	float3 normal,
																	float roughness,
																	float metallic,
																	float3 diffuse)
{
  light_direction = -normalize(light_direction);

  // The light direction from the fragment position
  float3 halfway_vector  = normalize(view_direction + light_direction);

  // Add the radiance
  float3 radiance        = light_diffuse * light_intensity;

  // The Fresnel-Schlick approximation expects a F0 parameter which is known as the surface reflection at zero incidence
  // or how much the surface reflects if looking directly at the surface.
  //
  // The F0 varies per material and is tinted on metals as we find in large material databases.
  // In the PBR metallic workflow we make the simplifying assumption that most dielectric surfaces look visually correct with a constant F0 of 0.04.
  float3 f0        = float3(0.04, 0.04, 0.04);
  f0               = lerp(f0, diffuse, metallic);

  // Cook torrance BRDF
  float  D         = distribution_ggx(normal, halfway_vector, roughness);
  float  G         = geometry_smith(normal, view_direction, light_direction, roughness);
  float3 F         = fresnel_schlick(clamp(dot(halfway_vector, view_direction), 0.0, 1.0), f0);

  float3 kS         = F;
  float3 kD         = float3(1.0, 1.0, 1.0) - kS;
  kD               *= 1.0 - metallic;

  float3 numerator  = D * G * F;
  float denominator = 4.0 * max(dot(normal, view_direction), 0.0) * max(dot(normal, light_direction), 0.0);
  float3 specular   = numerator / max(denominator, 0.001);

  // Get the cosine theta of the light against the normal
  float cos_theta      = max(dot(normal, light_direction), 0.0);

  return (mul(1/kPI, mul(kD, diffuse)) + specular) * radiance * cos_theta;
}


float light_visibility(float3 light_direction,
                       float3 ws_pos,
                       float3 normal,
                       float t_max,
                       float normal_bias)
{
	RayDesc ray;
	ray.Origin    = ws_pos + normal * normal_bias;
	ray.Direction = normalize(-light_direction);
	ray.TMin      = 0.01f;
	ray.TMax      = t_max;

	Payload payload = (Payload)0;
	TraceRay(g_AccelerationStructure,
           RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
           0xFF,
           0,
           0,
           0,
           ray,
           payload);

  return payload.t < 0.0f;
}

#endif