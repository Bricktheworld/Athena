#include "../root_signature.hlsli"
#include "../interlop.hlsli"
#include "../include/dof.hlsli"

ConstantBuffer<interlop::DofCompositeComputeResources> compute_resources : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main( uint3 thread_id : SV_DispatchThreadID )
{
	Texture2D<half4>    color_buffer = ResourceDescriptorHeap[compute_resources.color_buffer];

	Texture2D<half2>    coc_buffer   = ResourceDescriptorHeap[compute_resources.coc_buffer];
	Texture2D<half3>    near_buffer  = ResourceDescriptorHeap[compute_resources.near_buffer];
	Texture2D<half3>    far_buffer   = ResourceDescriptorHeap[compute_resources.far_buffer];

	RWTexture2D<float3> render_target = ResourceDescriptorHeap[compute_resources.render_target];

  float width, height;
  render_target.GetDimensions(width, height);
  float2 uv = float2(thread_id.xy) / float2(width, height);

  // NOTE(Brandon): The color buffer is expected to be the same dimensions as the render target
	half3 color = color_buffer[thread_id.xy].rgb;

	half2 coc   = coc_buffer[thread_id.xy];
	half3 near  = near_buffer.Sample(g_ClampSampler, uv).rgb;
	half3 far   = far_buffer.Sample(g_ClampSampler, uv).rgb;

	half3 far_blend = lerp(color, far, clamp(coc.y, 0.0h, 1.0h));
	render_target[thread_id.xy] = half3(lerp(far_blend, near, clamp(coc.x, 0.0h, 1.0h)));
}