#include "../root_signature.hlsli"
#include "../interlop.hlsli"

ConstantBuffer<interlop::DebugGBufferResources> render_resources : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main( uint3 thread_id : SV_DispatchThreadID )
{

	Texture2D<uint>                 gbuffer_material_ids     = ResourceDescriptorHeap[render_resources.gbuffer_material_ids];
	Texture2D<float4>               gbuffer_positions        = ResourceDescriptorHeap[render_resources.gbuffer_world_pos];
	Texture2D<unorm float4>         gbuffer_diffuse_metallic = ResourceDescriptorHeap[render_resources.gbuffer_diffuse_rgb_metallic_a];
	Texture2D<unorm float4>         gbuffer_normal_roughness = ResourceDescriptorHeap[render_resources.gbuffer_normal_rgb_roughness_a];
	Texture2D<float>                gbuffer_depth            = ResourceDescriptorHeap[render_resources.gbuffer_depth];

	RWTexture2D<unorm float4>       render_target            = ResourceDescriptorHeap[render_resources.render_target];

	ConstantBuffer<interlop::DebugGBufferOptions> options    = ResourceDescriptorHeap[render_resources.options];

	      uint   material_id = gbuffer_material_ids    [thread_id.xy];
	      float3 world_pos   = gbuffer_positions       [thread_id.xy].xyz;
	unorm float3 diffuse     = gbuffer_diffuse_metallic[thread_id.xy].rgb;
	unorm float  metallic    = gbuffer_diffuse_metallic[thread_id.xy].a;
	unorm float3 normal      = gbuffer_normal_roughness[thread_id.xy].rgb;
	unorm float  roughness   = gbuffer_normal_roughness[thread_id.xy].a;
	      float  depth       = 0.1f / gbuffer_depth[thread_id.xy] * 0.0005f;

	float4 output = float4(0.0, 0.0, 0.0, 1.0);
	switch (options.gbuffer_target)
	{
		case 0: output = float4((float)material_id, 0.0, 0.0, 1.0); break;
		case 2: output = float4(diffuse, 1.0);   break;
		case 3: output = float4(metallic, metallic, metallic, 1.0);   break;
		case 4: output = float4(normal, 1.0);   break;
		case 5: output = float4(roughness, roughness, roughness, 1.0);   break;
		case 6: output = float4(depth, depth, depth, 1.0f); break;
		case 1:
		default: output = float4(world_pos, 1.0); break;
	}

	render_target[thread_id.xy] = output;
}