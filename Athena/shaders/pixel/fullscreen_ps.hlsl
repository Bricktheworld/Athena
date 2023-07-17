#include "../root_signature.hlsli"
#include "../interlop.hlsli"

struct PSInput
{
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD0;
};

ConstantBuffer<interlop::FullscreenRenderResources> render_resources : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
float4 main(PSInput IN) : SV_TARGET
{
	Texture2D<float4> input = ResourceDescriptorHeap[render_resources.input];
	SamplerState sampler = ResourceDescriptorHeap[render_resources.input_sampler];
	return input.Sample(sampler, IN.uv);

//	return input.Sample(g_ClampSampler, IN.uv);
}