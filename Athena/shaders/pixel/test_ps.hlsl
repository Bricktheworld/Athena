struct PSInput
{
	float4 color : COLOR;
};

float4 main(PSInput IN) : SV_TARGET
{
	return float4(1.0, 1.0, 1.0, 1.0);
}