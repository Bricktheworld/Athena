#include "../root_signature.hlsli"
#include "../interlop.hlsli"
#include "../include/dof.hlsli"

ConstantBuffer<interlop::DebugCoCResources> compute_resources : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main( uint3 thread_id : SV_DispatchThreadID )
{
	Texture2D<half2>    coc_buffer    = ResourceDescriptorHeap[compute_resources.coc_buffer];
	RWTexture2D<float4> render_target = ResourceDescriptorHeap[compute_resources.render_target];

	half2 coc = coc_buffer[thread_id.xy];

	render_target[thread_id.xy] = float4(0.0f, coc.y / kMaxCoC, 0.0f, 1.0f);
}