#include "../root_signature.hlsli"
#include "../interlop.hlsli"

ConstantBuffer<interlop::DownsampleComputeResources> compute_resources : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void main( uint2 thread_id : SV_DispatchThreadID )
{
}
