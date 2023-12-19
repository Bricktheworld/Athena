#include "../root_signature.hlsli"
#include "../interlop.hlsli"
#include "../include/dof.hlsli"

ConstantBuffer<interlop::DofCocDilateComputeResources> compute_resources : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main( uint3 thread_id : SV_DispatchThreadID )
{
	Texture2D<half2>   coc_buffer    = ResourceDescriptorHeap[compute_resources.coc_buffer];
	RWTexture2D<half2> render_target = ResourceDescriptorHeap[compute_resources.render_target];

	float2 resolution;
	coc_buffer.GetDimensions(resolution.x, resolution.y);

	float2 uv_step = float2(1.0f, 1.0f) / resolution;
	float2 uv      = thread_id.xy       / resolution;

	half max_near_coc = 0.0h;
	for (int x = -kDilateSize; x <= kDilateSize; x++)
	{
		for (int y = -kDilateSize; y <= kDilateSize; y++)
		{
			float2 sample_uv  = uv + uv_step * float2((float)x, (float)y);
			half sample_coc = coc_buffer.Sample(g_ClampSampler, sample_uv).x;
			max_near_coc = max(max_near_coc, sample_coc);
		}
	}

	render_target[thread_id.xy] = half2(max_near_coc, coc_buffer.Sample(g_ClampSampler, uv).y);
}