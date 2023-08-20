#ifndef __ROOT_SIGNATURE__
#define __ROOT_SIGNATURE__

#define BINDLESS_ROOT_SIGNATURE \
	"RootFlags(ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT | CBV_SRV_UAV_HEAP_DIRECTLY_INDEXED | SAMPLER_HEAP_DIRECTLY_INDEXED)," \
	"RootConstants(b0, num32BitConstants=64, visibility=SHADER_VISIBILITY_ALL)," \
	"StaticSampler(s0, filter = FILTER_MIN_MAG_MIP_LINEAR, addressU = TEXTURE_ADDRESS_CLAMP, addressV = TEXTURE_ADDRESS_CLAMP, addressW = TEXTURE_ADDRESS_CLAMP, mipLODBias = 0.0f, minLOD = 0.0f, maxLOD = 100.0f)"

SamplerState g_ClampSampler : register(s0);

#endif