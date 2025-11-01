#ifndef __INTERLOP_HLSLI__
#define __INTERLOP_HLSLI__

#if defined(__cplusplus)
typedef Mat4    float4x4;
typedef Vec2    float2;
typedef Vec3    float3;
typedef Vec4    float4;
typedef UVec2   uint2;
typedef UVec3   uint3;
typedef UVec4   uint4;
typedef SVec2   int2;
typedef SVec3   int3;
typedef SVec4   int4;
typedef u32     uint;

typedef f16     float16_t;
typedef f16     half;
typedef f32     float32_t;

typedef Vec2f16 half2;
typedef Vec3f16 half3;
typedef Vec4f16 half4;
#else
typedef float4x4  Mat4;
typedef float2    Vec2;
typedef float3    Vec3;
typedef float4    Vec4;
typedef float4    Quat;
typedef uint2     UVec2;
typedef uint3     UVec3;
typedef uint4     UVec4;
typedef int2      SVec2;
typedef int3      SVec3;
typedef int4      SVec4;
typedef uint      u32;
typedef uint16_t  u16;
typedef int       s32;
typedef float     f32;
typedef float16_t f16;
typedef half2     Vec2f16;
typedef half3     Vec3f16;
typedef half4     Vec4f16;

#define kQNaN (asfloat(0x7FC00000))
#endif

#include "Include/spherical_harmonics.hlsli"

struct MultiDrawIndirectArgs
{
  u32 vertex_count_per_instance;
  u32 instance_count;
  u32 start_vertex_location;
  u32 start_instance_location;
};

struct MultiDrawIndirectIndexedArgs
{
  u32 index_count_per_instance;
  u32 instance_count;
  u32 start_index_location;
  s32 base_vertex_location;
  u32 start_instance_location;
};

struct DispatchIndirectArgs
{
  u32 x;
  u32 y;
  u32 z;
};

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
  Vec3 direction;
  f32  illuminance;

  Vec3 diffuse;
  u32  __pad__;

  Vec3 sky_diffuse;
  f32  sky_illuminance;
};

struct Viewport
{
  Mat4 proj;
  Mat4 view;
  Mat4 view_proj;
  Mat4 prev_view_proj;
  Mat4 inverse_view_proj;
  Vec4 camera_world_pos;
  Vec4 prev_camera_world_pos;
  Vec2 taa_jitter;
  Vec2 __pad__;
  DirectionalLight directional_light;
};

struct RenderSettingsGpu
{
  f32    focal_dist;                    // In meters
  f32    focal_range;                   // In meters
  f32    dof_blur_radius;               //
  u32    dof_sample_count;              //

  f32    aperture;                      // In f-stops (aperture 16 => f/16)
  f32    shutter_time;                  // In seconds
  f32    iso;                           // (100, 200, etc.)
  u32    __pad0__;

  Vec2   mouse_pos;
  u32    __pad1__;
  u32    __pad2__;

  Vec3   diffuse_gi_probe_spacing;
  // Flags
  u32    disable_taa:              1;
  u32    disable_diffuse_gi:       1;
  u32    disable_hdr:              1;
  u32    disable_dof:              1;
  u32    debug_gi_probes:          1;
  u32    debug_gi_sample_probes:   1;
  u32    enabled_debug_draw:       1;
  u32    freeze_gi_probe_rotation: 1;
  u32    freeze_gi_probe_clipmap:  1;
};

struct Transform
{
  Mat4 model;
  Mat4 model_inverse;
  Mat4 prev_model;
};

struct SceneObjGpu
{
  Mat4 model;
  Mat4 model_inverse;
  Mat4 prev_model;

  u32  material_id;
};

struct MaterialGpu
{
  Texture2DPtr<float4> diffuse;
  Texture2DPtr<float3> normal;
  Texture2DPtr<float>  roughness;
  Texture2DPtr<float>  metalness;
};

struct MaterialSrt
{
  ConstantBufferPtr<Transform> transform;
  Texture2DPtr<float4> diffuse;
  Texture2DPtr<float4> normal;
  float4 diffuse_base;

  u32 gpu_id;
};

struct MaterialUploadCmd
{
  MaterialGpu material;
  u32         mat_gpu_id;
};

struct MaterialUploadSrt
{
  StructuredBufferPtr<MaterialUploadCmd> material_uploads;
  u32                                    count;

  RWStructuredBufferPtr<MaterialGpu>     material_gpu_buffer;
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

struct ToneMappingSrt
{
  Texture2DPtr<float4> texture;
  u32                  disable_hdr;
};

struct PointLight
{
  Vec4 position;
  Vec4 color;
  f32  radius;
  f32  intensity;
};

#define kDoFResolutionScale 2

struct DoFCocSrt
{
  Texture2DPtr<float4>   hdr_buffer;
  Texture2DPtr<float>    depth_buffer;
  RWTexture2DPtr<float4> coc_buffer;

  f32 z_near;
  f32 aperture;
  f32 focal_dist;
  f32 focal_range;
};

struct DoFBokehBlurSrt
{
  Texture2DPtr<float>    depth_buffer;
  Texture2DPtr<float4>   coc_buffer;
  Texture2DPtr<float4>   hdr_buffer;

  RWTexture2DPtr<float4> blur_buffer;

  f32                    z_near;
  f32                    blur_radius;
  u32                    sample_count;
};

struct DoFCompositeSrt
{
  Texture2DPtr<float4>   coc_buffer;

  Texture2DPtr<float4>   hdr_buffer;
  Texture2DPtr<float4>   blur_buffer;

  RWTexture2DPtr<float4> render_target;
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

enum SdfType
{
  kSdfTypeSphere = 0,
  kSdfTypeSphericalHarmonic = 1,
};

struct DebugSdf
{
  Vec3           position;
  f32            radius;

  // TODO(bshihabi): We don't need both here... we want some kind of union
  Vec3           color;

  uint           type;

  SH::L1_F16_RGB luminance;
};

struct DebugLinePoint
{
  Vec3 position;
  Vec3 color;
};

struct DebugLineDrawSrt
{
  StructuredBufferPtr<DebugLinePoint> debug_line_vert_buffer;
};

struct DebugSdfDrawSrt
{
  StructuredBufferPtr<DebugSdf> debug_sdf_buffer;
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