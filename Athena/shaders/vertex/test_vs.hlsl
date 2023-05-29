#include "../root_signature.hlsli"

struct VSInput
{
	float4 position : POSITION;
	float4 color : COLOR;
};

struct VSOutput
{
	float4 color : COLOR;
	float4 position : SV_POSITION;
};

struct Transform
{
	float4x4 mvp;
};

ConstantBuffer<Transform> c_transform : register(b0, space0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
VSOutput main(VSInput IN)
{
	VSOutput OUT;

//	float4x4 mvp = mul(c_transform.projection, mul(c_transform.view, c_transform.model));
	OUT.position = mul(c_transform.mvp, IN.position);
	OUT.color = IN.color;

	return OUT;
}