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
typedef int       s32;
typedef float     f32;
typedef float16_t f16;
typedef half2     Vec2f16;
typedef half3     Vec3f16;
typedef half4     Vec4f16;

#define kQNaN (asfloat(0x7FC00000))
#endif

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
  Vec4 direction;
  Vec4 diffuse;
  f32  intensity;
  Vec3 __pad__;
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


#define kProbeNumIrradianceInteriorTexels 14
#define kProbeNumIrradianceTexels 16
#define kProbeNumDistanceInteriorTexels 14
#define kProbeNumDistanceTexels 16

static const UVec3 kProbeCountPerClipmap = UVec3(16, 16, 16);
static const u32   kProbeClipmapCount    = 3;
static const u32   kProbeMaxActiveCount  = kProbeCountPerClipmap.x * kProbeCountPerClipmap.y * kProbeCountPerClipmap.z * kProbeClipmapCount;
static const u32   kProbeMaxRayCount     = 64 * kProbeMaxActiveCount; // 32768; // This is 8 rays per probe in a 16 x 16 x 16 grid, choose wisely!

struct GiProbeRayAllocSrt
{
  RWBufferPtr<half> gi_probe_radiance;
};

struct GiProbeRayRadiance
{
};

struct DDGIVolDesc
{
  Vec4  origin;
  Vec4  probe_spacing;

  Mat4  probe_ray_rotation;

  UVec3 probe_count;

  u32   probe_num_rays;

  f32   probe_hysteresis;
  f32   probe_max_ray_distance;

  u32   debug_ray_probe;
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

enum SdfType
{
  kSdfTypeSphere,
};

struct DebugSdf
{
  Vec3    position;
  f32     radius;

  Vec3    color;
  SdfType type;
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