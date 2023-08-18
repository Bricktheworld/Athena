#include "../root_signature.hlsli"
#include "../interlop.hlsli"

ConstantBuffer<interlop::DofCocComputeResources> compute_resources : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main( uint3 thread_id : SV_DispatchThreadID )
{
	Texture2D<float4>                    color_buffer  = ResourceDescriptorHeap[compute_resources.color_buffer];
	Texture2D<float>                     depth_buffer  = ResourceDescriptorHeap[compute_resources.depth_buffer];

	RWTexture2D<float4>                  render_target = ResourceDescriptorHeap[compute_resources.render_target];

	ConstantBuffer<interlop::DofOptions> options       = ResourceDescriptorHeap[compute_resources.options];

	float z_near       = options.z_near;
	float aperture     = options.aperture;
	//	float focusing_dist = options.focusing_dist;
	//	float focal_length = options.focal_length;
	//	float focal_length = options.focal_length;
	float focal_dist   = options.focal_dist;
	float focal_range  = options.focal_range;
	float z            = z_near / depth_buffer[thread_id.xy];

	if (z > focal_dist)
	{
		z       = focal_dist + max(0, z - focal_dist - focal_range);
	}

	float coc = (1.0f - focal_dist / z) * 0.7f * 5.4f;

//	float magnification = abs(focal_length / (focusing_dist - focal_length));
//	float circle_of_confusion = aperture * magnification * abs(1.0f - focusing_dist / z);

//	float f = options.focal_length;
//	float v = 1.0f/(1.0f/f - 1.0f/depth);
//
//	float v0 = 
//	float circle_of_confusion = options.aperture * (options.focal_length / (options.))

	render_target[thread_id.xy] = float4(-coc / 4.0f, coc / 4.0f, 0.0f, 1.0f);
}