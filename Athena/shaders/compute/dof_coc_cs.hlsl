#include "../root_signature.hlsli"
#include "../interlop.hlsli"
#include "../include/dof.hlsli"

ConstantBuffer<interlop::DofCocComputeResources> compute_resources : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main( uint3 thread_id : SV_DispatchThreadID )
{
	Texture2D<half4>                     color_buffer  = ResourceDescriptorHeap[compute_resources.color_buffer];
	Texture2D<float>                     depth_buffer  = ResourceDescriptorHeap[compute_resources.depth_buffer];

	RWTexture2D<half2>                   render_target = ResourceDescriptorHeap[compute_resources.render_target];

	ConstantBuffer<interlop::DofOptions> options       = ResourceDescriptorHeap[compute_resources.options];

	float z_near       = options.z_near;
	float aperture     = options.aperture;
	float focal_dist   = options.focal_dist;
	float focal_range  = options.focal_range;
	float z            = z_near / depth_buffer[thread_id.xy];

	if (z > focal_dist)
	{
		z = focal_dist + max(0, z - focal_dist - focal_range);
	}

	float coc = (1.0f - focal_dist / z) * 0.7f * aperture;

	render_target[thread_id.xy] = half2(clamp(-coc, 0.0h, kMaxCoC), clamp(coc, 0.0h, kMaxCoC));
}