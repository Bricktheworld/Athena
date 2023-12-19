#include "../root_signature.hlsli"
#include "../interlop.hlsli"

ConstantBuffer<interlop::ProbeDebugCSResources> render_resources : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main( uint3 thread_id : SV_DispatchThreadID )
{
	Texture2DArray<float4>                      tex_2d_array  = ResourceDescriptorHeap[render_resources.tex_2d_array];

	RWTexture2D<float4>                         render_target = ResourceDescriptorHeap[render_resources.render_target];
//	ConstantBuffer<interlop::ProbeDebugOptions> options       = ResourceDescriptorHeap[render_resources.options];

	uint2 resolution;
  uint array_size;
	tex_2d_array.GetDimensions(resolution.x, resolution.y, array_size);
  
  if (thread_id.y >= resolution.y)
    return;

 	render_target[thread_id.xy] = tex_2d_array[uint3(thread_id.x % resolution.x, thread_id.y, thread_id.x / resolution.x)];
}