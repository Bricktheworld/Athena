#include "../interlop.hlsli"
#include "../root_signature.hlsli"
#include "../include/rt_common.hlsli"
#include "../include/lighting_common.hlsli"
#include "../include/ddgi_common.hlsli"

GlobalRootSignature kBindlessRootSignature =
{
	BINDLESS_ROOT_SIGNATURE
};

TriangleHitGroup kHitGroup = 
{
	"", // Any hit
	"", // Closest hit
};

RaytracingShaderConfig kShaderConfig = 
{
	sizeof(Payload), // Max payload size
	8,  // Max attribute size: sizeof(unorm float2) -> Barycentric
};

RaytracingPipelineConfig kPipelineConfig = 
{
	1, // Max trace recursion depth
};

ConstantBuffer<interlop::StandardBrdfRTResources> rt_resources : register(b0);

[shader("raygeneration")]
void ray_gen()
{
	uint2 launch_index      = DispatchRaysIndex().xy;
	uint2 launch_dimensions = DispatchRaysDimensions().xy;

	Payload payload;

	Texture2D<uint>                       gbuffer_material_ids     = ResourceDescriptorHeap[rt_resources.gbuffer_material_ids];
	Texture2D<float4>                     gbuffer_positions        = ResourceDescriptorHeap[rt_resources.gbuffer_world_pos];
	Texture2D<unorm float4>               gbuffer_diffuse_metallic = ResourceDescriptorHeap[rt_resources.gbuffer_diffuse_rgb_metallic_a];
	Texture2D<float4>                     gbuffer_normal_roughness = ResourceDescriptorHeap[rt_resources.gbuffer_normal_rgb_roughness_a];

	ConstantBuffer<interlop::Scene>       scene                    = ResourceDescriptorHeap[rt_resources.scene];
	ConstantBuffer<interlop::DDGIVolDesc> vol_desc                 = ResourceDescriptorHeap[rt_resources.vol_desc];
  Texture2DArray<float4>                probe_irradiance         = ResourceDescriptorHeap[rt_resources.probe_irradiance];
  Texture2DArray<float2>                probe_distance           = ResourceDescriptorHeap[rt_resources.probe_distance];
	RWTexture2D<float3>                   render_target            = ResourceDescriptorHeap[rt_resources.render_target];

	interlop::DirectionalLight            directional_light = scene.directional_light;


  uint   material_id  = gbuffer_material_ids    [launch_index];
  float3 ws_pos       = gbuffer_positions       [launch_index].xyz;
  float3 normal       = gbuffer_normal_roughness[launch_index].xyz;

	if (material_id == 0)
	{
		render_target[launch_index] = float3(0.0f, 0.0f, 0.0f);
		return;
	}

	float  shadow_atten    = light_visibility(directional_light.direction.xyz,
                                                    ws_pos,
                                                    normal,
                                                    1e27f,
                                                    0.001f);

	float3 view_direction  = normalize(scene.camera_world_pos.xyz - ws_pos);

  float3 direct_lighting = evaluate_directional_light(directional_light.direction.xyz,
                                                      directional_light.diffuse.rgb, 
                                                      directional_light.intensity,
                                                      view_direction,
                                                      normal,
                                                      0.2f,
                                                      0.0f,
                                                      1.0f) * saturate(shadow_atten);

  float3 cam_dir         = normalize(ws_pos - scene.camera_world_pos.xyz);
  float3 surface_bias    = get_surface_bias(normal, cam_dir, vol_desc);
  float3 indirect        = get_vol_irradiance(ws_pos, surface_bias, normal, vol_desc, probe_irradiance, probe_distance);

  float3 irradiance      = direct_lighting + saturate(indirect);
  float3 lambertian      = evaluate_lambertian(1.0f);

	float3 color           = payload.t < 0.0f ? float3(0.0f, 0.0f, 0.0f) : lambertian * irradiance;

	render_target[launch_index] = color;
}

[shader("miss")]
void miss(inout Payload payload)
{
	payload.t = -1.0f;
}