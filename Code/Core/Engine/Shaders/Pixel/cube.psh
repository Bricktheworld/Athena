#include "../root_signature.hlsli"

struct PSInput
{
  float4 position : SV_POSITION;
};

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
float4 main(PSInput position) : SV_TARGET
{
  return float4(0.0f, 0.0f, 1.0f, 1.0f);
}