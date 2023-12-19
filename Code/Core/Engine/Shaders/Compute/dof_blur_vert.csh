#include "../root_signature.hlsli"
#include "../interlop.hlsli"
#include "../include/dof.hlsli"

ConstantBuffer<interlop::DofBlurVertComputeResources> compute_resources : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main( uint3 thread_id : SV_DispatchThreadID )
{
	Texture2D<half2>    coc_buffer          = ResourceDescriptorHeap[compute_resources.coc_buffer];

	Texture2D<half4>    red_near_buffer     = ResourceDescriptorHeap[compute_resources.red_near_buffer];
	Texture2D<half4>    blue_near_buffer    = ResourceDescriptorHeap[compute_resources.blue_near_buffer];
	Texture2D<half4>    green_near_buffer   = ResourceDescriptorHeap[compute_resources.green_near_buffer];

	Texture2D<half4>    red_far_buffer      = ResourceDescriptorHeap[compute_resources.red_far_buffer];
	Texture2D<half4>    blue_far_buffer     = ResourceDescriptorHeap[compute_resources.blue_far_buffer];
	Texture2D<half4>    green_far_buffer    = ResourceDescriptorHeap[compute_resources.green_far_buffer];

	RWTexture2D<float3> blurred_near_target = ResourceDescriptorHeap[compute_resources.blurred_near_target];
	RWTexture2D<float3> blurred_far_target  = ResourceDescriptorHeap[compute_resources.blurred_far_target];

	float2 coc_res;
	coc_buffer.GetDimensions(coc_res.x, coc_res.y);

	float2 out_res;
	blurred_near_target.GetDimensions(out_res.x, out_res.y);

	float2 uv_step = float2(1.0f, 1.0f)   / out_res;
	float2 uv      = float2(thread_id.xy) / out_res;
 
	bool is_near = thread_id.z == 0;
	half filter_radius = is_near ? coc_buffer.Sample(g_ClampSampler, uv).x : coc_buffer.Sample(g_ClampSampler, uv).y;

	half4 red_component   = half4(0.0h, 0.0h, 0.0h, 0.0h);
	half4 green_component = half4(0.0h, 0.0h, 0.0h, 0.0h);
	half4 blue_component  = half4(0.0h, 0.0h, 0.0h, 0.0h);
	for (int i = -kKernelRadius; i <= kKernelRadius; i++)
	{
		float2 sample_uv  = uv + uv_step / 4.0f * float2(0.0f, (float)i) * filter_radius;

		half4 sample_red, sample_green, sample_blue;
		if (is_near)
		{
			sample_red   = red_near_buffer.Sample(g_ClampSampler, sample_uv);
			sample_green = green_near_buffer.Sample(g_ClampSampler, sample_uv);
			sample_blue  = blue_near_buffer.Sample(g_ClampSampler, sample_uv);
		}
		else
		{
			sample_red   = red_far_buffer.Sample(g_ClampSampler, sample_uv);
			sample_green = green_far_buffer.Sample(g_ClampSampler, sample_uv);
			sample_blue  = blue_far_buffer.Sample(g_ClampSampler, sample_uv);
		}

		float2 c0 = kKernel0_RealX_ImY_RealZ_ImW[i + kKernelRadius].xy;
		float2 c1 = kKernel1_RealX_ImY_RealZ_ImW[i + kKernelRadius].xy;

		red_component.xy += mult_complex(sample_red.xy, c0);
		red_component.zw += mult_complex(sample_red.zw, c1);

		green_component.xy += mult_complex(sample_green.xy, c0);
		green_component.zw += mult_complex(sample_green.zw, c1);

		blue_component.xy += mult_complex(sample_blue.xy, c0);
		blue_component.zw += mult_complex(sample_blue.zw, c1);
	}

	float3 output_color;
	output_color.r = dot(red_component.xy, kKernel0Weights_RealX_ImY) + dot(red_component.zw, kKernel1Weights_RealX_ImY);
	output_color.g = dot(green_component.xy, kKernel0Weights_RealX_ImY) + dot(green_component.zw, kKernel1Weights_RealX_ImY);
	output_color.b = dot(blue_component.xy, kKernel0Weights_RealX_ImY) + dot(blue_component.zw, kKernel1Weights_RealX_ImY);
	if (is_near)
	{
		blurred_near_target[thread_id.xy] = output_color;
	}
	else
	{
		blurred_far_target[thread_id.xy] = output_color;
	}
}
