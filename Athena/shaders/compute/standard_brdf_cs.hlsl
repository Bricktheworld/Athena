#include "../root_signature.hlsli"
#include "../interlop.hlsli"
//#include "../include/lighting_common.hlsli"

ConstantBuffer<interlop::StandardBRDFComputeResources> render_resources : register(b0);

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


//	float3 output_luminance = calc_directional_light(view_direction, normal, roughness, metallic, f0, diffuse);
  float3 output_luminance = float3(1.0f, 1.0f, 1.0f);

	render_target[thread_id.xy] = float4(output_luminance, 1.0);
}