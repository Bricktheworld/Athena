#include "../root_signature.hlsli"
#include "../interlop.hlsli"

struct DeferredPSOut
{
	      uint   material_id      : SV_Target0;
	      float4 world_pos        : SV_Target1;
	unorm float4 diffuse_metallic  : SV_Target2;
	unorm float4 normal_roughness : SV_Target3;
};

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
DeferredPSOut main(shaders::BasicVSOut ps_in)
{
	DeferredPSOut ret;
	ret.material_id      = 1;
	ret.world_pos        = float4(ps_in.world_pos.xyz, 1.0f);
	ret.diffuse_metallic  = float4(1.0f, 1.0f, 1.0f, 0.5f);
	ret.normal_roughness = float4(ps_in.normal.xyz, 0.1f);
	return ret;
}