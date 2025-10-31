#include "../interlop.hlsli"
#include "../root_signature.hlsli"
#include "../include/rt_common.hlsli"
#include "../include/standard_brdf_common.hlsli"
#include "../include/math.hlsli"
#include "../Include/debug_draw.hlsli"

ConstantBuffer<StandardBrdfSrt> g_Srt : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(8, 8, 1)]
void CS_StandardBrdf(uint2 dispatch_thread_id : SV_DispatchThreadID)
{
  Texture2D<uint>                     gbuffer_material_ids     = DEREF(g_Srt.gbuffer_material_ids);
  Texture2D<unorm float4>             gbuffer_diffuse_metallic = DEREF(g_Srt.gbuffer_diffuse_rgb_metallic_a);
  Texture2D<float4>                   gbuffer_normal_roughness = DEREF(g_Srt.gbuffer_normal_rgb_roughness_a);
  Texture2D<float>                    gbuffer_depth            = DEREF(g_Srt.gbuffer_depth);

  StructuredBuffer<DiffuseGiProbe>    diffuse_gi_probes        = DEREF(g_Srt.diffuse_gi_probes);
  Texture2DArray<u16>                 diffuse_gi_page_table    = DEREF(g_Srt.diffuse_gi_page_table);

  RWTexture2D<float4>                 render_target            = DEREF(g_Srt.render_target);

  DirectionalLight                    directional_light        = g_ViewportBuffer.directional_light;


  uint   material_id  = gbuffer_material_ids    [dispatch_thread_id];
  float3 diffuse      = gbuffer_diffuse_metallic[dispatch_thread_id].rgb;
  float  metallic     = gbuffer_diffuse_metallic[dispatch_thread_id].a;
  float3 normal       = normalize(gbuffer_normal_roughness[dispatch_thread_id].xyz);
  float  depth        = gbuffer_depth           [dispatch_thread_id];

  float2 screen_size;
  gbuffer_depth.GetDimensions(screen_size.x, screen_size.y);


  float3 ws_pos = screen_to_world(float3(float2(dispatch_thread_id) + 0.5f, depth), screen_size).xyz;

  if (material_id == 0)
  {
    render_target[dispatch_thread_id] = float4(0.0f, 0.0f, 0.0f, 1.0f);
    return;
  }

  float shadow_atten = light_visibility(
    directional_light.direction.xyz,
    ws_pos,
    normal,
    1e27f,
    0.001f
  );

  float3 view_direction = normalize(g_ViewportBuffer.camera_world_pos.xyz - ws_pos);

  Lux3   directional_illuminance;
  directional_illuminance.m_Value = directional_light.illuminance * directional_light.diffuse.rgb;

  BSDF   directional_bsdf   = cook_torrance_bsdf(directional_light.direction.xyz, view_direction, normal, 0.8f, 0.0f, diffuse);
  Nits3  direct_luminance   = directional_bsdf * directional_illuminance.attenuated(shadow_atten);

  Nits3  indirect_luminance = sample_indirect_luminance(ws_pos, normal, diffuse, diffuse_gi_page_table, diffuse_gi_probes);

  Nits3 luminance;
  luminance.m_Value         = direct_luminance.m_Value + indirect_luminance.m_Value;

  float3 display_output     = luminance.m_Value;

  render_target[dispatch_thread_id] = float4(display_output, 1.0f);
}