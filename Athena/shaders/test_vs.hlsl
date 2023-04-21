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
	float4x4 projection;
	float4x4 view;
	float4x4 model;
};

ConstantBuffer<Transform> c_transform : register(b0, space0);

VSOutput main(VSInput IN)
{
	VSOutput OUT;

	float4x4 mvp = mul(c_transform.projection, mul(c_transform.view, c_transform.model));
	OUT.position = mul(mvp, IN.position);
	OUT.color = IN.color;

	return OUT;
}