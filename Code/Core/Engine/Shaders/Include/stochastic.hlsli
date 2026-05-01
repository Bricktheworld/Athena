#pragma once

#if !defined(__cplusplus)

// https://media.contentapi.ea.com/content/dam/ea/seed/presentations/2019-ray-tracing-gems-chapter-25-barre-brisebois-et-al.pdf
struct MultiscaleMeanEstimatorData
{
  float3 mean;
  float3 short_mean;
  float  vbbr;
  float3 variance;
  float  inconsistency;
};

float3 multiscale_mean_estimator(
  inout float3 mean,
  inout float3 short_mean,
  inout float vbbr,
  inout float3 variance,
  inout float inconsistency,
  float3 y,
  float short_window_blend = 0.08f
) {
  // Determine the mean using the fixed "small" hysteresis, giving us 
  float3 delta         = y - short_mean;
  short_mean           = lerp(short_mean, y, short_window_blend);

  float  variance_blend = short_window_blend * 0.5f;
  variance              = lerp(variance, delta * delta, variance_blend);
  float3 dev            = sqrt(max(1e-5, variance));

  float3 short_diff     = mean - short_mean;
  float  relative_diff  = luma_rec709(abs(short_diff) / max(1e-5, variance));
  inconsistency         = lerp(inconsistency, relative_diff, 0.08);

  float  variance_
}

#endif
