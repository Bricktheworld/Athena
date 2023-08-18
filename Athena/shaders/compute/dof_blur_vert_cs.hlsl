#include "../root_signature.hlsli"
#include "../interlop.hlsli"
#include "../include/dof.hlsli"

ConstantBuffer<interlop::DofBlurVertComputeResources> compute_resources : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main( uint3 thread_id : SV_DispatchThreadID )
{
	Texture2D<float>  coc_buffer    = ResourceDescriptorHeap[compute_resources.coc_buffer];

	Texture2D<float4> red_buffer    = ResourceDescriptorHeap[compute_resources.red_buffer];
	Texture2D<float4> blue_buffer   = ResourceDescriptorHeap[compute_resources.blue_buffer];
	Texture2D<float4> green_buffer  = ResourceDescriptorHeap[compute_resources.green_buffer];

	RWTexture2D<float4> blurred_target = ResourceDescriptorHeap[compute_resources.blurred_target];

	float2 resolution;
	blurred_target.GetDimensions(resolution.x, resolution.y);

	float2 uv_step = float2(1.0f, 1.0f) / resolution;
	float2 uv      = thread_id.xy       / resolution;

	static const float kFilterRadius = 3.0f;

	float4 red_component   = float4(0.0f, 0.0f, 0.0f, 0.0f);
	float4 green_component = float4(0.0f, 0.0f, 0.0f, 0.0f);
	float4 blue_component  = float4(0.0f, 0.0f, 0.0f, 0.0f);
	for (int i = -kKernelRadius; i <= kKernelRadius; i++)
	{
		float2 sample_uv  = uv + uv_step * float2(0.0f, (float)i) * kFilterRadius;

		float4 sample_red = red_buffer.Sample(g_ClampSampler, sample_uv);
		float4 sample_green = green_buffer.Sample(g_ClampSampler, sample_uv);
		float4 sample_blue = blue_buffer.Sample(g_ClampSampler, sample_uv);

		float2 c0 = kKernel0_RealX_ImY_RealZ_ImW[i + kKernelRadius].xy;
		float2 c1 = kKernel1_RealX_ImY_RealZ_ImW[i + kKernelRadius].xy;

		red_component.xy += mult_complex(sample_red.xy, c0);
		red_component.zw += mult_complex(sample_red.zw, c1);

		green_component.xy += mult_complex(sample_green.xy, c0);
		green_component.zw += mult_complex(sample_green.zw, c1);

		blue_component.xy += mult_complex(sample_blue.xy, c0);
		blue_component.zw += mult_complex(sample_blue.zw, c1);
	}

	float4 output_color;
	output_color.r = dot(red_component.xy, kKernel0Weights_RealX_ImY) + dot(red_component.zw, kKernel1Weights_RealX_ImY);
	output_color.g = dot(green_component.xy, kKernel0Weights_RealX_ImY) + dot(green_component.zw, kKernel1Weights_RealX_ImY);
	output_color.b = dot(blue_component.xy, kKernel0Weights_RealX_ImY) + dot(blue_component.zw, kKernel1Weights_RealX_ImY);
	output_color.a = 1.0f;
	blurred_target[thread_id.xy] = output_color;
}
