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

cbuffer Constants : register(b0)
{
	float4x4 c_projection;
	float4x4 c_view;
	float4x4 c_model;
};

VSOutput main(VSInput IN)
{
	VSOutput OUT;

	float4x4 mvp = mul(c_projection, mul(c_view, c_model));
	OUT.position = mul(mvp, IN.position);
	OUT.color = IN.color;

	return OUT;
}