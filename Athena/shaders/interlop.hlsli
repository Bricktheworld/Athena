#pragma once


#ifdef __cplusplus
#define CONSTANT_BUFFER alignas(256) 

#define CBV(T) gfx::render::Cbv<T>
#define SRV(T) gfx::render::Srv<T>
#define UAV(T) gfx::render::Uav<T>
#define SAMPLER gfx::render::Sampler
using GpuImage = gfx::GpuImage;

#else
#define CONSTANT_BUFFER
typedef float4x4 Mat4;
typedef float4 Vec4;
typedef uint u32;
struct GpuImage;

#define SRV(T) u32
#define CBV(T) u32
#define UAV(T) u32
#define SAMPLER u32

#endif


namespace interlop
{
	struct SceneBuffer
	{
		Mat4 proj;
		Mat4 view;
		Mat4 view_proj;
	};

	struct TransformBuffer
	{
		Mat4 model;
	};

	struct CubeRenderResources
	{
		SRV(Vec4)            position;
		CBV(SceneBuffer)     scene;
		CBV(TransformBuffer) transform;
	};

	struct FullscreenRenderResources
	{
		SRV(GpuImage) input;
		SAMPLER input_sampler;
	};
}

#ifdef __cplusplus
#undef SRV
#undef CBV
#undef UAV
#endif