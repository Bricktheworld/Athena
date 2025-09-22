#ifndef __DDGI_COMMON__
#define __DDGI_COMMON__

#include "lighting_common.hlsli"
#include "spherical_harmonics.hlsli"

#define kProbeCountPerClipmapX (8)
#define kProbeCountPerClipmapY (4)
#define kProbeCountPerClipmapZ (8)
#define kProbeCountPerClipmap (UVec3(kProbeCountPerClipmapX, kProbeCountPerClipmapY, kProbeCountPerClipmapZ))
#define kProbeClipmapCount (3)
#define kProbeMaxActiveCount (kProbeCountPerClipmapX * kProbeCountPerClipmapY * kProbeCountPerClipmapZ * kProbeClipmapCount)
#define kProbeMaxRayCount (64 * kProbeMaxActiveCount) // 32768; // This is 8 rays per probe in a 16 x 16 x 16 grid, choose wisely!

struct DiffuseGiProbe
{
  SH::L2_F16_RGB luminance;

  Vec3f16 mean;
  Vec3f16 short_mean;
  f16     vbbr;
  Vec3f16 variance;
  f16     inconsistency;
};

struct GiRayLuminance
{
  Nits3f16 luminance;
  Vec3f16  direction;
};

struct RtDiffuseGiSettings
{
  Vec3 probe_spacing[kProbeClipmapCount];
};

struct RtDiffuseGiProbeInitSrt
{
  RWTexture2DArrayPtr<u16> page_table;
};

struct RtDiffuseGiProbeReprojectSrt
{
  RWTexture2DArrayPtr<u16>               page_table;
  RWStructuredBufferPtr<DiffuseGiProbe>  probe_buffer;
  Texture2DArrayPtr<u16>                 page_table_prev;
  ConstantBufferPtr<RtDiffuseGiSettings> settings;
};

struct RtDiffuseGiTraceRaySrt
{
  Mat4                                   rotation;
  Texture2DArrayPtr<u16>                 page_table;
  RWStructuredBufferPtr<GiRayLuminance>  ray_output_buffer;
  ConstantBufferPtr<RtDiffuseGiSettings> settings;
};

struct RtDiffuseGiProbeBlendSrt
{
  ConstantBufferPtr<RtDiffuseGiSettings> settings;
  StructuredBufferPtr<GiRayLuminance>    ray_buffer;
  RWStructuredBufferPtr<DiffuseGiProbe>  probe_buffer;
};

#endif