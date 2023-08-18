#include "../root_signature.hlsli"
#include "../interlop.hlsli"

ConstantBuffer<interlop::StandardBRDFComputeResources> render_resources : register(b0);

static const float kPI = 3.14159265359;

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

float3 calc_directional_light(float3 view_direction, float3 normal, float roughness, float metallic, float3 f0, float3 diffuse)
{
    // The light direction from the fragment position
    float3 light_direction = normalize(-float3(-1, -1, 0));
    float3 halfway_vector  = normalize(view_direction + light_direction);

    // Add the radiance
		float3 directional_light_diffuse = float3(1.0, 1.0, 1.0);
		float directional_light_intensity = 3.0f;
    float3 radiance         = mul(directional_light_intensity, directional_light_diffuse);

    // Cook torrance BRDF
    float  D         = distribution_ggx(normal, halfway_vector, roughness);
    float  G         = geometry_smith(normal, view_direction, light_direction, roughness);
    float3 F         = fresnel_schlick(clamp(dot(halfway_vector, view_direction), 0.0, 1.0), f0);

    float3 kS         = F;
    float3 kD         = float3(1.0, 1.0, 1.0) - kS;
    kD             *= 1.0 - metallic;

    float3 numerator       = mul(D * G, F);
    float denominator    = 4.0 * max(dot(normal, view_direction), 0.0) * max(dot(normal, light_direction), 0.0);
    float3 specular        = numerator / max(denominator, 0.001);

    // Get the cosine theta of the light against the normal
    float cos_theta      = max(dot(normal, light_direction), 0.0);

    return (mul(1/kPI, mul(kD, diffuse)) + specular) * radiance * cos_theta;
}

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main( uint3 thread_id : SV_DispatchThreadID )
{

	Texture2D<uint>                 gbuffer_material_ids     = ResourceDescriptorHeap[render_resources.gbuffer_material_ids];
	Texture2D<float4>               gbuffer_positions        = ResourceDescriptorHeap[render_resources.gbuffer_world_pos];
	Texture2D<unorm float4>         gbuffer_diffuse_metallic = ResourceDescriptorHeap[render_resources.gbuffer_diffuse_rgb_metallic_a];
	Texture2D<unorm float4>         gbuffer_normal_roughness = ResourceDescriptorHeap[render_resources.gbuffer_normal_rgb_roughness_a];

	RWTexture2D<unorm float4>       render_target            = ResourceDescriptorHeap[render_resources.render_target];
	ConstantBuffer<interlop::Scene> scene                    = ResourceDescriptorHeap[render_resources.scene];

	      uint   material_id = gbuffer_material_ids    [thread_id.xy];
	      float3 world_pos   = gbuffer_positions       [thread_id.xy].xyz;
	unorm float3 diffuse     = gbuffer_diffuse_metallic[thread_id.xy].rgb;
	unorm float  metallic    = gbuffer_diffuse_metallic[thread_id.xy].a;
	unorm float3 normal      = gbuffer_normal_roughness[thread_id.xy].rgb;
	unorm float  roughness   = gbuffer_normal_roughness[thread_id.xy].a;

	if (material_id == 0)
	{
		render_target[thread_id.xy] = float4(0.0, 0.0, 0.0, 1.0);
		return;
	}

	float3 view_direction = normalize(scene.camera_world_pos.xyz - world_pos);


	// The Fresnel-Schlick approximation expects a F0 parameter which is known as the surface reflection at zero incidence
	// or how much the surface reflects if looking directly at the surface.
	//
	// The F0 varies per material and is tinted on metals as we find in large material databases.
	// In the PBR metallic workflow we make the simplifying assumption that most dielectric surfaces look visually correct with a constant F0 of 0.04.
	float3 f0 = float3(0.04, 0.04, 0.04);
	f0        = lerp(f0, diffuse, metallic);

	float3 output_luminance = calc_directional_light(view_direction, normal, roughness, metallic, f0, diffuse);

	render_target[thread_id.xy] = float4(output_luminance, 1.0);
}