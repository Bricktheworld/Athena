#include "../root_signature.hlsli"
#include "../interlop.hlsli"
#include "../include/dof.hlsli"

ConstantBuffer<interlop::DofCompositeComputeResources> compute_resources : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main( uint3 thread_id : SV_DispatchThreadID )
{
	Texture2D<half2>    coc_buffer    = ResourceDescriptorHeap[compute_resources.coc_buffer];

	Texture2D<half4>    color_buffer = ResourceDescriptorHeap[compute_resources.color_buffer];
	Texture2D<half4>    near_buffer  = ResourceDescriptorHeap[compute_resources.near_buffer];
	Texture2D<half4>    far_buffer   = ResourceDescriptorHeap[compute_resources.far_buffer];

	RWTexture2D<float4> render_target = ResourceDescriptorHeap[compute_resources.render_target];

	half2 coc   = coc_buffer[thread_id.xy];
	half3 color = color_buffer[thread_id.xy].rgb;
	half3 near  = near_buffer[thread_id.xy].rgb;
	half3 far   = far_buffer[thread_id.xy].rgb;

	half3 far_blend = lerp(color, far, clamp(coc.y, 0.0h, 1.0h));
	render_target[thread_id.xy] = half4(lerp(far_blend, near, clamp(coc.x, 0.0h, 1.0h)), 1.0h);
}