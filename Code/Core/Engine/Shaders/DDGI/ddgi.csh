#include "../root_signature.hlsli"
#include "../interlop.hlsli"
#include "../include/ddgi_common.hlsli"

ConstantBuffer<ProbeBlendingSrt> g_Srt : register(b0);

float2 get_normalized_octahedral_coords(int2 tex_coords, int num_texels)
{
  float2 oct_tex_coords = float2(tex_coords.x % num_texels, tex_coords.y % num_texels);

  oct_tex_coords += 0.5f;

  oct_tex_coords /= float(num_texels);
  oct_tex_coords *= 2.0f;
  oct_tex_coords -= 1.0f;

  return oct_tex_coords;
}


[RootSignature(BINDLESS_ROOT_SIGNATURE)]
[numthreads(kProbeNumIrradianceTexels, kProbeNumIrradianceTexels, 1)]
void CS_ProbeBlending(
  uint3 dispatch_thread_id : SV_DispatchThreadID,
  uint3 group_thread_id    : SV_GroupThreadID,
  uint3 group_id           : SV_GroupID,
  uint  group_index        : SV_GroupIndex
) {
  bool is_border_texel = (group_thread_id.x == 0 || group_thread_id.x == (kProbeNumIrradianceInteriorTexels + 1));
  is_border_texel     |= (group_thread_id.y == 0 || group_thread_id.y == (kProbeNumIrradianceInteriorTexels + 1));

  ConstantBuffer<DDGIVolDesc> vol_desc   = DEREF(g_Srt.vol_desc);
  Texture2DArray<float4>      ray_data   = DEREF(g_Srt.ray_data);
  RWTexture2DArray<float4>    irradiance = DEREF(g_Srt.irradiance);

  int  probe_index = get_probe_index(dispatch_thread_id, kProbeNumIrradianceTexels, vol_desc);
  uint num_probes  = vol_desc.probe_count.x * vol_desc.probe_count.y * vol_desc.probe_count.z;

  if (probe_index >= num_probes || probe_index < 0) return;

  if (!is_border_texel)
  {
    int3   thread_coords   = int3(group_id.x * kProbeNumIrradianceInteriorTexels, group_id.y * kProbeNumIrradianceInteriorTexels, dispatch_thread_id.z) + group_thread_id - int3(1, 1, 0);
    float2 probe_octant_uv = get_normalized_octahedral_coords(thread_coords.xy, kProbeNumIrradianceInteriorTexels);
    float3 probe_ray_dir   = octahedral_decode_dir(probe_octant_uv);

    float4 result = 0.0f;

    // Loop through all of our samples and blend them together using a cosine weighted sum
    for (int iray = 0; iray < vol_desc.probe_num_rays; iray++)
    {
      float3 sampled_ray_dir = get_probe_ray_dir(iray, vol_desc);

      // For each sample, we're going to figure out how much it contributes to the texel
      // we're currently blending irradiance data for. Each sample is cosine weighted so that samples
      // that more accurately "represent" the currentl irradiance texel's direction on the octahedron
      // are blended more into this particular texel. We don't want samples that are on the complete
      // opposite of the sphere to influence the irradiance of a texel on the other side of the sphere
      float weight = max(0.0f, dot(probe_ray_dir, sampled_ray_dir));

      // Get the sampled data from the current frame for our ray index
      uint3  sample_texel_coords = get_ray_data_texel_coords(iray, probe_index, vol_desc);
      float3 sample_radiance     = ray_data[sample_texel_coords].rgb;
      float  sample_distance     = ray_data[sample_texel_coords].a;

      // If the sample distance is negative, that means that it hit a backface and we definitely don't
      // want to blend it
      if (sample_distance < 0.0f)
      {
        continue;
      }

      // Add our cosine-weighted radiance to our total sum
      result += float4(sample_radiance * weight, weight);
    }

    // So now that we have our sum, it's time to do Monte Carlo things.
    //
    // Reminder of the rendering equation: ∫ fᵣ(x,ωᵢ,ωₒ,λ,t) Lᵢ(x,ωᵢ,ωₒ,λ,t) (ωᵢ⋅n̂) dωᵢ
    // In DDGI, we have a bunch of light probes and those light probes consist of a sphere of a set of points
    // and directions. To put it more concretely with how it's implemented: Our spheres are octahedrons with a finite
    // set of texels. Each texel is a point and the location of the texel in the octahedron indicates the direction.
    // These manifest as the `probe_octant_uv` for the point p and `probe_ray_dir` for the normalized direction `n`.
    // This information can then be used to get "irradiance" (the flux of energy per unit area arriving at a surface)
    //
    // With that in mind, what we want to calculate/store is the "irradiance", which can be calculated as:
    // ∫ Lᵢ(p,ωᵢ) (ωᵢ⋅n̂) dωᵢ
    // With a Monte Carlo estimate of N samples, this is approximated as: 2π * (1/N) Σ Lᵢ(p,ωᵢ) (ωᵢ⋅n̂)
    // The 2π factor is the integration domain which is a hemisphere.
    //
    // To decrease variance, instead of dividing by N, we can divide by the sum of the weights, in effect
    // making it divided by the PDF that more closely matches our sampling function.
    // (That's my understanding of it at least, I'm not entirely certain that's correct)
    //
    // However the expected value of E[ Σ (ωᵢ⋅n̂) ] = Σ E[(ωᵢ⋅n̂)] = Σ (1/2) = N/2 which is off by a factor of 2
    // (Remember we're dividing that sum out of our total sum of radiance samples, 
    //  and what we really should be dividing is N)
    //
    // So what we really need to do is divide by 2 * the sum of the weights, hence the following code below.
    float epsilon = float(vol_desc.probe_num_rays) * 1e-9f;
    result.rgb   *= 1.0f / (2.0f * max(result.a, epsilon));

    // Get the current irradiance of the probe
    float3 probe_irradiance = irradiance[dispatch_thread_id].rgb;

    // An experimentally determined gamma that makes things more pleasing during lighting changes
    static const float encoding_gamma = 5.0f;
    result.rgb = pow(result.rgb, (1.0f / encoding_gamma));

    // Get the delta of the irradiance from what we just sampled to our current irradiance
    float3 delta = (result.rgb - probe_irradiance.rgb);

    // TODO(Brandon): In the original implementation, this hystersis had a lot of smart things of adjusting
    // to quick changes in brightness. I've just hard-coded it for now for simplicity but we'll eventually
    // want to do that
    float  hysteresis = 0.97f;

    // Lerp between our old irradiance and our new irradiance based on the hysteresis
    float3 lerp_delta = (1.0f - hysteresis) * delta;
    result = float4(probe_irradiance.rgb + lerp_delta, 1.0f);

    irradiance[dispatch_thread_id] = result;
    return;
  }

  // We don't want to handle corners until all non-corners have finished writing their data...
  AllMemoryBarrierWithGroupSync();

  bool is_corner_texel = (group_thread_id.x == 0 || group_thread_id.x == (kProbeNumIrradianceTexels - 1)) && 
                         (group_thread_id.y == 0 || group_thread_id.y == (kProbeNumIrradianceTexels - 1));
  bool is_row_texel    = (group_thread_id.x > 0 && group_thread_id.x < (kProbeNumIrradianceTexels - 1));
  uint3 src_coords     = uint3(group_id.x * kProbeNumIrradianceTexels,
                               group_id.y * kProbeNumIrradianceTexels,
                               dispatch_thread_id.z);
  if (is_corner_texel)
  {
    src_coords.x += 1;
    src_coords.y += 1;
  }
  else if (is_row_texel)
  {
    src_coords.x += (kProbeNumIrradianceTexels - 1) - group_thread_id.x;
    src_coords.y += group_thread_id.y - 1;
  }
  else // if (is_col_texel)
  {
    src_coords.x += group_thread_id.x - 1;
    src_coords.y += (kProbeNumIrradianceTexels - 1) - group_thread_id.y;
  }
  irradiance[dispatch_thread_id] = irradiance[src_coords];
}
