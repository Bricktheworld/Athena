#include "../root_signature.hlsli"
#include "../interlop.hlsli"
#include "../include/dof.hlsli"


ConstantBuffer<interlop::DofBlurHorizComputeResources> compute_resources : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main( uint3 thread_id : SV_DispatchThreadID )
{
	Texture2D<float4>                    color_buffer  = ResourceDescriptorHeap[compute_resources.color_buffer];
	Texture2D<float>                     coc_buffer    = ResourceDescriptorHeap[compute_resources.coc_buffer];

	RWTexture2D<float4>                  red_target    = ResourceDescriptorHeap[compute_resources.red_target];
	RWTexture2D<float4>                  blue_target   = ResourceDescriptorHeap[compute_resources.blue_target];
	RWTexture2D<float4>                  green_target  = ResourceDescriptorHeap[compute_resources.green_target];

	float2 resolution;
	color_buffer.GetDimensions(resolution.x, resolution.y);

	float2 uv_step = float2(1.0f, 1.0f) / resolution;
	float2 uv      = thread_id.xy       / resolution;

	static const float kFilterRadius = 3.0f;

	float4 red_component   = float4(0.0f, 0.0f, 0.0f, 0.0f);
	float4 green_component = float4(0.0f, 0.0f, 0.0f, 0.0f);
	float4 blue_component  = float4(0.0f, 0.0f, 0.0f, 0.0f);
	for (int i = -kKernelRadius; i <= kKernelRadius; i++)
	{
		float2 sample_uv    = uv + uv_step * float2((float)i, 0.0f) * kFilterRadius;
		float3 sample_color = color_buffer.Sample(g_ClampSampler, sample_uv).rgb;
		red_component      += compute_c0_xy_c1_zw(sample_color.r, i);
		green_component    += compute_c0_xy_c1_zw(sample_color.g, i);
		blue_component     += compute_c0_xy_c1_zw(sample_color.b, i);
	}

	red_target  [thread_id.xy] = red_component;
	green_target[thread_id.xy] = green_component;
	blue_target [thread_id.xy] = blue_component;
}
