#include "../root_signature.hlsli"

struct PSInput
{
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD0;
};

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
float4 main(PSInput IN) : SV_TARGET
{
	// StructuredBuffer<float3> positions = ResourceDescriptorHeap[
	return float4(IN.uv, 0.0, 1.0);
}