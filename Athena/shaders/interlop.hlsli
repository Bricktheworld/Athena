#pragma once

#ifdef __cplusplus
#define uint u32
#define float4x4 Mat4
#define CONSTANT_BUFFER alignas(256) 
#else
#define CONSTANT_BUFFER
#endif


namespace interlop
{
	struct CubeRenderResources
	{
		uint position_idx;
		uint scene_idx;
		uint transform_idx;
	};

	struct CONSTANT_BUFFER SceneBuffer
	{
		float4x4 view_proj;
		float4x4 proj;
		float4x4 view;
	};

	struct CONSTANT_BUFFER TransformBuffer
	{
		float4x4 model;
	};
}