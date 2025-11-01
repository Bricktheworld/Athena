#ifndef __STANDARD_BRDF_COMMON__
#define __STANDARD_BRDF_COMMON__

#include "ddgi_common.hlsli"

struct StandardBrdfSrt
{
  // GBuffer
  Texture2DPtr<uint>                     gbuffer_material_ids;
  Texture2DPtr<float4>                   gbuffer_diffuse_rgb_metallic_a;
  Texture2DPtr<float4>                   gbuffer_normal_rgb_roughness_a;
  Texture2DPtr<float>                    gbuffer_depth;

  // Diffuse GI resources
  StructuredBufferPtr<DiffuseGiProbe>    diffuse_gi_probes;
  Texture2DArrayPtr<u32>                 diffuse_gi_page_table;

  // Output
  RWTexture2DPtr<float4>                 render_target;
};

#endif