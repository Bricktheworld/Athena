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
typedef float2 Vec2;
typedef float3 Vec3;
typedef float4 Vec4;
typedef uint u32;
typedef float f32;
struct GpuImage;

#define SRV(T) u32
#define CBV(T) u32
#define UAV(T) u32
#define SAMPLER u32

#endif


namespace interlop
{
	struct Vertex
	{
		Vec4 position;
		Vec4 normal;
		Vec4 uv;
//		Vec4 tangent;
//		Vec4 bitangent;
	};

	struct Scene
	{
		Mat4 proj;
		Mat4 view;
		Mat4 view_proj;
		Vec4 camera_world_pos;
	};

	struct Transform
	{
		Mat4 model;
		Mat4 model_inverse;
	};

	struct CubeRenderResources
	{
		SRV(Vec4)      position;
		CBV(Scene)     scene;
		CBV(Transform) transform;
	};

	struct MaterialRenderResources
	{
		SRV(Vertex)    vertices;
		CBV(Scene)     scene;
		CBV(Transform) transform;
	};

	struct FullscreenRenderResources
	{
		SRV(GpuImage)  texture;
	};

	struct PointLight
	{
		Vec4 position;
		Vec4 color;
		f32  radius;
		f32  intensity;
	};

	struct DirectionalLight
	{
		Vec4 direction;
		Vec4 color;
		f32  intensity;
	};

	struct StandardBRDFComputeResources
	{
		CBV(Scene)     scene;
		SRV(GpuImage)  gbuffer_material_ids;
		SRV(GpuImage)  gbuffer_world_pos;
		SRV(GpuImage)  gbuffer_diffuse_rgb_metallic_a;
		SRV(GpuImage)  gbuffer_normal_rgb_roughness_a;

		UAV(GpuImage)  render_target;
	};

	struct DofOptions
	{
		f32 z_near;
		f32 aperture;
		f32 focal_dist;
		f32 focal_range;
		// f32 focal_length;
		// f32 focusing_dist;
	};

	struct DofCocComputeResources
	{
		CBV(DofOptions) options;
		SRV(GpuImage)   color_buffer;
		SRV(GpuImage)   depth_buffer;

		UAV(GpuImage)   render_target;
	};

	struct DofCocDilateComputeResources
	{
		SRV(GpuImage)   coc_buffer;

		UAV(GpuImage)   render_target;
	};

	struct DofBlurHorizComputeResources
	{
		SRV(GpuImage)   color_buffer;
		SRV(GpuImage)   coc_buffer;

		UAV(GpuImage)   red_near_target;
		UAV(GpuImage)   green_near_target;
		UAV(GpuImage)   blue_near_target;

		UAV(GpuImage)   red_far_target;
		UAV(GpuImage)   green_far_target;
		UAV(GpuImage)   blue_far_target;
	};

	struct DofBlurVertComputeResources
	{
		SRV(GpuImage)   coc_buffer;

		SRV(GpuImage)   red_near_buffer;
		SRV(GpuImage)   green_near_buffer;
		SRV(GpuImage)   blue_near_buffer;

		SRV(GpuImage)   red_far_buffer;
		SRV(GpuImage)   green_far_buffer;
		SRV(GpuImage)   blue_far_buffer;

		UAV(GpuImage)   blurred_near_target;
		UAV(GpuImage)   blurred_far_target;
	};

	struct DofCompositeComputeResources
	{
		SRV(GpuImage)   coc_buffer;

		SRV(GpuImage)   color_buffer;
		SRV(GpuImage)   near_buffer;
		SRV(GpuImage)   far_buffer;

		UAV(GpuImage)   render_target;
	};

	struct DebugGBufferOptions
	{
		u32 gbuffer_target;
	};

	struct DebugGBufferResources
	{
		CBV(DebugGBufferOptions) options;
		SRV(GpuImage)            gbuffer_material_ids;
		SRV(GpuImage)            gbuffer_world_pos;
		SRV(GpuImage)            gbuffer_diffuse_rgb_metallic_a;
		SRV(GpuImage)            gbuffer_normal_rgb_roughness_a;
		SRV(GpuImage)            gbuffer_depth;

		UAV(GpuImage)            render_target;
	};

	struct DebugCoCResources
	{
		SRV(GpuImage) coc_buffer;

		UAV(GpuImage) render_target;
	};
}

#ifndef __cplusplus
namespace shaders
{
	struct BasicVSOut
	{
		float4 ndc_pos   : SV_Position;
		float4 world_pos : POSITIONT;
		float4 normal    : NORMAL0;
		float4 uv        : TEXCOORD0;
//		float4 tangent   : TANGENT0;
//		float4 bitangent : BITANGENT0;
	};
}
#endif

#ifdef __cplusplus
#undef SRV
#undef CBV
#undef UAV
#endif