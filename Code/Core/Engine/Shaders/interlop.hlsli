#ifndef __INTERLOP_HLSLI__
#define __INTERLOP_HLSLI__

#ifdef __cplusplus
#define CONSTANT_BUFFER alignas(256) 

#define CBV(T) Cbv<T>
#define SRV(T) Srv<T>
#define UAV(T) Uav<T>
using GpuTexture = GpuTexture;
using GpuBvh   = GpuBvh;

#else
#define CONSTANT_BUFFER
typedef float4x4 Mat4;
typedef float2 Vec2;
typedef float3 Vec3;
typedef float4 Vec4;
typedef float4 Quat;
typedef uint u32;
typedef float f32;
struct GpuTexture;
struct GpuBvh;

#define SRV(T) u32
#define CBV(T) u32
#define UAV(T) u32

#endif


namespace interlop
{
  struct Vertex
  {
    Vec3 position; // Position MUST be at the START of the struct in order for BVHs to be built
    Vec3 normal;
    Vec2 uv;
  };

  struct DirectionalLight
  {
     Vec4 direction;
     Vec4 diffuse;
     f32 intensity;
  };

  struct Scene
  {
    Mat4 proj;
    Mat4 view_proj;
    Mat4 prev_view_proj;
    Mat4 inverse_view_proj;
    Vec4 camera_world_pos;
    Vec2 taa_jitter;
    DirectionalLight directional_light;
    u32  disable_taa;
  };

  struct Transform
  {
    Mat4 model;
    Mat4 model_inverse;
//    Mat4 prev_model;
  };

  struct CubeRenderResources
  {
    SRV(Vec4)      position;
    CBV(Scene)     scene;
    CBV(Transform) transform;
  };

  struct MaterialRenderResources
  {
    CBV(Transform) transform;
  };

  struct FullscreenRenderResources
  {
    SRV(GpuTexture) texture;
  };

  struct PostProcessingRenderResources
  {
    SRV(GpuTexture) texture;
  };

  struct PointLight
  {
    Vec4 position;
    Vec4 color;
    f32  radius;
    f32  intensity;
  };

  struct DownsampleComputeResources
  {
    SRV(GpuTexture) src;
    UAV(GpuTexture) dst;
  };

  struct DofOptions
  {
    f32 z_near;
    f32 aperture;
    f32 focal_dist;
    f32 focal_range;
  };

  struct DofCocComputeResources
  {
    CBV(DofOptions) options;
    SRV(GpuTexture) color_buffer;
    SRV(GpuTexture) depth_buffer;

    UAV(GpuTexture) render_target;
  };

  struct DofCocDilateComputeResources
  {
    SRV(GpuTexture) coc_buffer;

    UAV(GpuTexture) render_target;
  };

  struct DofBlurHorizComputeResources
  {
    SRV(GpuTexture) color_buffer;
    SRV(GpuTexture) coc_buffer;

    UAV(GpuTexture) red_near_target;
    UAV(GpuTexture) green_near_target;
    UAV(GpuTexture) blue_near_target;

    UAV(GpuTexture) red_far_target;
    UAV(GpuTexture) green_far_target;
    UAV(GpuTexture) blue_far_target;
  };

  struct DofBlurVertComputeResources
  {
    SRV(GpuTexture) coc_buffer;

    SRV(GpuTexture) red_near_buffer;
    SRV(GpuTexture) green_near_buffer;
    SRV(GpuTexture) blue_near_buffer;

    SRV(GpuTexture) red_far_buffer;
    SRV(GpuTexture) green_far_buffer;
    SRV(GpuTexture) blue_far_buffer;

    UAV(GpuTexture) blurred_near_target;
    UAV(GpuTexture) blurred_far_target;
  };

  struct DofCompositeComputeResources
  {
    SRV(GpuTexture) coc_buffer;

    SRV(GpuTexture) color_buffer;
    SRV(GpuTexture) near_buffer;
    SRV(GpuTexture) far_buffer;

    UAV(GpuTexture) render_target;
  };

  struct DebugGBufferOptions
  {
    u32 gbuffer_target;
  };

  struct DebugGBufferResources
  {
    CBV(DebugGBufferOptions) options;
    SRV(GpuTexture)          gbuffer_material_ids;
    SRV(GpuTexture)          gbuffer_world_pos;
    SRV(GpuTexture)          gbuffer_diffuse_rgb_metallic_a;
    SRV(GpuTexture)          gbuffer_normal_rgb_roughness_a;
    SRV(GpuTexture)          gbuffer_depth;

    UAV(GpuTexture)          render_target;
  };

  struct DebugCoCResources
  {
    SRV(GpuTexture) coc_buffer;

    UAV(GpuTexture) render_target;
  };

  struct DebugVisualizerResources
  {
    SRV(GpuTexture) input;
    UAV(GpuTexture) output;
  };

#define kProbeNumIrradianceInteriorTexels 6
#define kProbeNumIrradianceTexels 8
#define kProbeNumDistanceInteriorTexels 14
#define kProbeNumDistanceTexels 16

	struct DDGIVolDesc
	{
		Vec4 origin;
		Vec4 probe_spacing;

		Mat4 probe_ray_rotation;

		u32  probe_count_x;
		u32  probe_count_y;
		u32  probe_count_z;

		u32  probe_num_rays;

		f32  probe_hysteresis;
		f32  probe_max_ray_distance;
	};

  struct BasicRTResources
  {
    CBV(DDGIVolDesc) vol_desc;
		CBV(Scene)       scene;
    SRV(GpuTexture)  probe_irradiance;
    SRV(GpuTexture)  probe_distance;

		UAV(GpuTexture)  render_target;
  };

  struct ProbeTraceRTResources
  {
		CBV(DDGIVolDesc) vol_desc;
    SRV(GpuTexture)  probe_irradiance;
    SRV(GpuTexture)  probe_distance;

		UAV(GpuTexture)  ray_data;
  };

  struct ProbeBlendingCSResources
  {
		CBV(DDGIVolDesc) vol_desc;
    SRV(GpuTexture)  ray_data;
    UAV(GpuTexture)  irradiance;
  };

  struct ProbeDistanceBlendingCSResources
  {
		CBV(DDGIVolDesc) vol_desc;
    SRV(GpuTexture)  ray_data;
    UAV(GpuTexture)  distance;
  };

  struct ProbeDebugOptions
  {
    u32 layer;
  };

  struct ProbeDebugCSResources
  {
//    CBV(ProbeDebugOptions) options;
    SRV(GpuTexture)  tex_2d_array;
    UAV(GpuTexture)  render_target;
  };

  struct StandardBrdfRTResources
  {
    SRV(GpuTexture)  gbuffer_material_ids;
    SRV(GpuTexture)  gbuffer_diffuse_rgb_metallic_a;
    SRV(GpuTexture)  gbuffer_normal_rgb_roughness_a;
    SRV(GpuTexture)  gbuffer_depth;

		CBV(DDGIVolDesc) ddgi_vol_desc;
    SRV(GpuTexture)  ddgi_probe_irradiance;
    SRV(GpuTexture)  ddgi_probe_distance;

    UAV(GpuTexture)  render_target;
  };

  struct TAAResources
  {
    SRV(GpuTexture) prev_hdr;
    SRV(GpuTexture) curr_hdr;

    SRV(GpuTexture) prev_velocity;
    SRV(GpuTexture) curr_velocity;

    UAV(GpuTexture) taa;
  };

}

#ifndef __cplusplus
namespace shaders
{
  struct BasicVSOut
  {
    float4 ndc_pos   : SV_Position;
    float4 world_pos : POSITIONT;
    float3 normal    : NORMAL0;
    float2 uv        : TEXCOORD0;
    float4 curr_pos  : POSITION0;
    float4 prev_pos  : POSITION1;
//    float4 tangent   : TANGENT0;
//    float4 bitangent : BITANGENT0;
  };
}
#endif

#ifdef __cplusplus
#undef SRV
#undef CBV
#undef UAV
#endif

#endif