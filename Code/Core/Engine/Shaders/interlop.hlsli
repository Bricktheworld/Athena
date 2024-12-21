#ifndef __INTERLOP_HLSLI__
#define __INTERLOP_HLSLI__

#if defined(__cplusplus)
typedef Mat4  float4x4;
typedef Vec2  float2;
typedef Vec3  float3;
typedef Vec4  float4;
typedef UVec2 uint2;
typedef UVec3 uint3;
typedef UVec4 uint4;
typedef SVec2 int2;
typedef SVec3 int3;
typedef SVec4 int4;
typedef u32   uint;
#else
typedef float4x4 Mat4;
typedef float2   Vec2;
typedef float3   Vec3;
typedef float4   Vec4;
typedef float4   Quat;
typedef uint2    UVec2;
typedef uint3    UVec3;
typedef uint4    UVec4;
typedef int2     SVec2;
typedef int3     SVec3;
typedef int4     SVec4;
typedef uint     u32;
typedef float    f32;
#endif

#if defined(__cplusplus)
template <typename T>
struct BufferPtr
{
  u32 m_Index;
};
#else
template <typename T>
using BufferPtr = uint;
#endif

#if defined(__cplusplus)
struct ByteAddressBufferPtr
{
  u32 m_Index;
};
#else
typedef uint ByteAddressBufferPtr;
#endif

#if defined(__cplusplus)
template <typename T>
struct ConstantBufferPtr
{
  u32 m_Index;
};
#else
template <typename T>
using ConstantBufferPtr = uint;
#endif

#if defined(__cplusplus)
template <typename T>
struct RWBufferPtr
{
  u32 m_Index;
};
#else
template <typename T>
using RWBufferPtr = uint;
#endif

#if defined(__cplusplus)
struct RWByteAddressBufferPtr
{
  u32 m_Index;
};
#else
template <typename T>
using RWByteAddressBufferPtr = uint;
#endif

#if defined(__cplusplus)
template <typename T>
struct RWStructuredBufferPtr
{
  u32 m_Index;
};
#else
template <typename T>
using RWStructuredBufferPtr = uint;
#endif

#if defined(__cplusplus)
template <typename T>
struct RWTexture2DPtr
{
  u32 m_Index;
};
#else
template <typename T>
using RWTexture2DPtr = uint;
#endif

#if defined(__cplusplus)
template <typename T>
struct RWTexture2DArrayPtr
{
  u32 m_Index;
};
#else
template <typename T>
using RWTexture2DArrayPtr = uint;
#endif

#if defined(__cplusplus)
template <typename T>
struct StructuredBufferPtr
{
  u32 m_Index;
};
#else
template <typename T>
using StructuredBufferPtr = uint;
#endif

#if defined(__cplusplus)
template <typename T>
struct Texture2DPtr
{
  u32 m_Index;
};
#else
template <typename T>
using Texture2DPtr = uint;
#endif

#if defined(__cplusplus)
template <typename T>
struct Texture2DArrayPtr
{
  u32 m_Index;
};
#else
template <typename T>
using Texture2DArrayPtr = uint;
#endif

#if !defined(__cplusplus)
#define DEREF(ptr) ResourceDescriptorHeap[ptr]
#endif


struct Vertex
{
  Vec3 position; // Position MUST be at the START of the struct in order for BVHs to be built
  Vec3 normal;
  Vec2 uv;
};

struct Meshlet
{
  u32 vertex_count;
  u32 vertex_offset;
  u32 index_count;
  u32 index_offset;
};

struct DirectionalLight
{
  Vec4 direction;
  Vec4 diffuse;
  f32  intensity;
  Vec3 __pad__;
};

struct Viewport
{
  Mat4 proj;
  Mat4 view_proj;
  Mat4 prev_view_proj;
  Mat4 inverse_view_proj;
  Vec4 camera_world_pos;
  Vec2 taa_jitter;
  Vec2 __pad__;
  DirectionalLight directional_light;
};

struct Transform
{
  Mat4 model;
  Mat4 model_inverse;
//    Mat4 prev_model;
};

struct TransformBuffer
{
  // The array 2 is just so that the compiler thinks that it's an array
  Transform transforms[2];
};

struct MaterialSrt
{
  ConstantBufferPtr<Transform> transform;
  Texture2DPtr<float4> diffuse;
  Texture2DPtr<float4> normal;
  u32 gpu_id;
};

struct FullscreenSrt
{
  Texture2DPtr<float4> texture;
};

struct TextureCopySrt
{
  Texture2DPtr<float4>   src;
  RWTexture2DPtr<float4> dst;
};

struct PostProcessingSrt
{
  Texture2DPtr<float4> texture;
};

struct PointLight
{
  Vec4 position;
  Vec4 color;
  f32  radius;
  f32  intensity;
};

struct DofCocSrt
{
  Texture2DPtr<float4>   color_buffer;
  Texture2DPtr<float>    depth_buffer;
  RWTexture2DPtr<float2> render_target;

  f32 z_near;
  f32 aperture;
  f32 focal_dist;
  f32 focal_range;
};

struct DofCoCDilateSrt
{
  Texture2DPtr<float2>   coc_buffer;
  RWTexture2DPtr<float2> render_target;
};

struct DofBlurHorizSrt
{
  Texture2DPtr<float4>   color_buffer;
  Texture2DPtr<float2>   coc_buffer;

  RWTexture2DPtr<float4> red_near_target;
  RWTexture2DPtr<float4> green_near_target;
  RWTexture2DPtr<float4> blue_near_target;

  RWTexture2DPtr<float4> red_far_target;
  RWTexture2DPtr<float4> green_far_target;
  RWTexture2DPtr<float4> blue_far_target;
};

struct DofBlurVertSrt
{
  Texture2DPtr<float2>   coc_buffer;

  Texture2DPtr<float4>   red_near_buffer;
  Texture2DPtr<float4>   green_near_buffer;
  Texture2DPtr<float4>   blue_near_buffer;

  Texture2DPtr<float4>   red_far_buffer;
  Texture2DPtr<float4>   green_far_buffer;
  Texture2DPtr<float4>   blue_far_buffer;

  RWTexture2DPtr<float3> blurred_near_target;
  RWTexture2DPtr<float3> blurred_far_target;
};

struct DofCompositeSrt
{
  Texture2DPtr<float2>   coc_buffer;

  Texture2DPtr<float4>   color_buffer;
  Texture2DPtr<float3>   near_buffer;
  Texture2DPtr<float3>   far_buffer;

  RWTexture2DPtr<float3> render_target;
};

#define kProbeNumIrradianceInteriorTexels 14
#define kProbeNumIrradianceTexels 16
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

struct ProbeTraceSrt
{
  ConstantBufferPtr<DDGIVolDesc> vol_desc;
  Texture2DArrayPtr<float4>      probe_irradiance;

  RWTexture2DArrayPtr<float4>    ray_data;
};

struct ProbeBlendingSrt
{
  ConstantBufferPtr<DDGIVolDesc> vol_desc;
  Texture2DArrayPtr<float4>      ray_data;
  RWTexture2DArrayPtr<float4>    irradiance;
};

struct StandardBrdfSrt
{
  Texture2DPtr<uint>             gbuffer_material_ids;
  Texture2DPtr<float4>           gbuffer_diffuse_rgb_metallic_a;
  Texture2DPtr<float4>           gbuffer_normal_rgb_roughness_a;
  Texture2DPtr<float>            gbuffer_depth;

  ConstantBufferPtr<DDGIVolDesc> ddgi_vol_desc;
  Texture2DArrayPtr<float4>      ddgi_probe_irradiance;

  RWTexture2DPtr<float4>         render_target;
};

struct TemporalAASrt
{
  Texture2DPtr<float4>   prev_hdr;
  Texture2DPtr<float4>   curr_hdr;

  Texture2DPtr<float2>   prev_velocity;
  Texture2DPtr<float2>   curr_velocity;

  Texture2DPtr<float>    gbuffer_depth;

  RWTexture2DPtr<float4> taa;
};


#ifndef __cplusplus
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
#endif

#undef SRV
#undef CBV
#undef UAV

#endif