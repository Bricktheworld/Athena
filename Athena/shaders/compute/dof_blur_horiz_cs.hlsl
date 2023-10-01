#include "../root_signature.hlsli"
#include "../interlop.hlsli"
#include "../include/dof.hlsli"

ConstantBuffer<interlop::DofBlurHorizComputeResources> compute_resources : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main( uint3 thread_id : SV_DispatchThreadID )
{
	Texture2D<float4>  color_buffer      = ResourceDescriptorHeap[compute_resources.color_buffer];
	Texture2D<half2>   coc_buffer        = ResourceDescriptorHeap[compute_resources.coc_buffer];

	RWTexture2D<half4> red_near_target   = ResourceDescriptorHeap[compute_resources.red_near_target];
	RWTexture2D<half4> blue_near_target  = ResourceDescriptorHeap[compute_resources.blue_near_target];
	RWTexture2D<half4> green_near_target = ResourceDescriptorHeap[compute_resources.green_near_target];

	RWTexture2D<half4> red_far_target    = ResourceDescriptorHeap[compute_resources.red_far_target];
	RWTexture2D<half4> blue_far_target   = ResourceDescriptorHeap[compute_resources.blue_far_target];
	RWTexture2D<half4> green_far_target  = ResourceDescriptorHeap[compute_resources.green_far_target];

	float2 in_res;
	color_buffer.GetDimensions(in_res.x, in_res.y);

	float2 out_res;
	red_near_target.GetDimensions(out_res.x, out_res.y);

	float2 uv_step = float2(1.0f, 1.0f)   / in_res;
	float2 uv      = float2(thread_id.xy) / out_res;

	bool is_near = thread_id.z == 0;
	half filter_radius = is_near ? coc_buffer.Sample(g_ClampSampler, uv).x : coc_buffer.Sample(g_ClampSampler, uv).y;

	half4 red_component   = half4(0.0h, 0.0h, 0.0h, 0.0h);
	half4 green_component = half4(0.0h, 0.0h, 0.0h, 0.0h);
	half4 blue_component  = half4(0.0h, 0.0h, 0.0h, 0.0h);
	for (int i = -kKernelRadius; i <= kKernelRadius; i++)
	{
		float2 sample_uv    = uv + uv_step / 4.0f * float2((float)i, 0.0f) * filter_radius;
		float3 sample_color = color_buffer.Sample(g_ClampSampler, sample_uv).rgb;

		half2  sample_coc_pair = coc_buffer.Sample(g_ClampSampler, sample_uv);
		half   sample_coc   = is_near ? sample_coc_pair.x : sample_coc_pair.y;

		// TODO(Brandon): There's an annoying pop-in halo effect that occurs very abruptly when objects move from
		// the focus range into the far plane. This is caused by _very_ far pixels immediately going from not
		// sampling objects that are in focus to doing a full blur (since they are very far they have a large CoC).
		// The fix is to do some sort of multiplier that is dependent on the difference between CoCs. The idea being,
		// if a very far and large CoC is sampling one with a very small CoC, it shouldn't have as much of an impact.
		// Another way of doing this as described in the original paper is to simply multiply by the CoC,
		// but this results in a much more ugly darkening effect at the boundaries between near, focus, and far.
		// This is kinda a complex problem so I'm not entirely sure how to solve it. It's just not worth my time
		// right now to solve :P
		half   multiplier		= 1.0h; //clamp(sample_coc, 0.0h, 1.0h);
		if (!is_near && sample_coc <= 0.0h)
		{
			multiplier = 0.0h;
		}


		red_component      += compute_c0_xy_c1_zw(sample_color.r * multiplier, i);
		green_component    += compute_c0_xy_c1_zw(sample_color.g * multiplier, i);
		blue_component     += compute_c0_xy_c1_zw(sample_color.b * multiplier, i);
	}

	if (is_near)
	{
		red_near_target  [thread_id.xy] = red_component;
		green_near_target[thread_id.xy] = green_component;
		blue_near_target [thread_id.xy] = blue_component;
	}
	else
	{
		red_far_target  [thread_id.xy] = red_component;
		green_far_target[thread_id.xy] = green_component;
		blue_far_target [thread_id.xy] = blue_component;
	}
}
