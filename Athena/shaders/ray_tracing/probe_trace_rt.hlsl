#include "../interlop.hlsli"
#include "../root_signature.hlsli"
#include "../include/rt_common.hlsli"
#include "../include/lighting_common.hlsli"
#include "../include/math.hlsli"
#include "../include/ddgi_common.hlsli"

GlobalRootSignature kBindlessRootSignature =
{
	BINDLESS_ROOT_SIGNATURE
};

TriangleHitGroup kHitGroup = 
{
	"", // Any hit
	"hit", // Closest hit
};

RaytracingShaderConfig kShaderConfig = 
{
	sizeof(Payload), // Max payload size
	8,  // Max attribute size: sizeof(unorm float2) -> Barycentric
};

RaytracingPipelineConfig kPipelineConfig = 
{
	1, // Max trace recursion depth
};

ConstantBuffer<interlop::ProbeTraceRTResources> rt_resources : register(b0);

inline float3 get_camera_world_pos(uint2 index, float4x4 inverse_view_proj)
{
	float2 xy = index + 0.5f; // center in the middle of the pixel.
	float2 screenPos = xy / DispatchRaysDimensions().xy * 2.0 - 1.0;

	// Invert Y for DirectX-style coordinates.
	screenPos.y = -screenPos.y;

	// Unproject the pixel coordinate into a ray.
	float4 world = mul(inverse_view_proj, float4(screenPos, 1, 1));

	world.xyz /= world.w;
	return world.xyz;
}


[shader("raygeneration")]
void ray_gen()
{
	uint2 launch_index      = DispatchRaysIndex().xy;
	uint2 launch_dimensions = DispatchRaysDimensions().xy;

	ConstantBuffer<interlop::Scene> scene          = ResourceDescriptorHeap[rt_resources.scene];
	RWTexture2DArray<float4>        ray_data       = ResourceDescriptorHeap[rt_resources.out_ray_data];
	ConstantBuffer<interlop::DDGIVolDesc> vol_desc = ResourceDescriptorHeap[rt_resources.vol_desc];

	interlop::DirectionalLight directional_light = scene.directional_light;

	int ray_index         = DispatchRaysIndex().x;
	int probe_plane_index = DispatchRaysIndex().y;
	int plane_index       = DispatchRaysIndex().z;


	int probes_per_plane  = get_probes_per_plane(vol_desc);

	int probe_index       = (plane_index * probes_per_plane) + probe_plane_index;

	int3 probe_coords;
	probe_coords.x        = probe_index % vol_desc.probe_count_x;
	probe_coords.y        = probe_index / (vol_desc.probe_count_x * vol_desc.probe_count_z);
	probe_coords.z        = (probe_index / vol_desc.probe_count_x) % vol_desc.probe_count_z;

	float3 probe_ws_pos   = get_probe_ws_pos(probe_coords, vol_desc);
  float3 probe_ray_dir  = get_probe_ray_dir(ray_index, vol_desc);
  uint3  output_coords  = get_ray_data_texel_coords(ray_index, probe_index, vol_desc);

  RayDesc ray;
  ray.Origin = probe_ws_pos;
  ray.Direction = probe_ray_dir;
  // TODO(Brandon): This isn't a great hack...
  ray.TMin = 0.0f;
  ray.TMax = vol_desc.probe_max_ray_distance;

  Payload payload = (Payload)0;
  TraceRay(g_AccelerationStructure, RAY_FLAG_NONE, 0xFF, 0, 0, 0, ray, payload);


  if (payload.t < 0.0f)
  {
    // TODO(Brandon): On miss, we'll want to use the sky radiance instead of 0
    ray_data[output_coords] = float4(directional_light.diffuse.rgb * directional_light.intensity * 0.0005f, 1e27f); // float4(0.7, 1.0f, 1.0f, 1e27f);
    return;
  }

  if (payload.hit_kind == HIT_KIND_TRIANGLE_BACK_FACE)
  {
    // Decrease hit distance on a backface hit by 80% to decrease the influence of the probe during irradiance sampling
    ray_data[output_coords].w = -payload.t * 0.2f;
    return;
  }


  // Direct lighting and shadowing
	float  shadow_atten = saturate(light_visibility(directional_light.direction.xyz,
                                                  payload.ws_pos,
                                                  payload.normal,
                                                  1e27f,
                                                  0.001f));
  float3 direct_diffuse_lighting = evaluate_lambertian(1.0f) * 
                                   evaluate_directional_radiance(directional_light.diffuse.rgb, directional_light.intensity) * 
                                   evaluate_cos_theta(directional_light.direction.xyz, payload.normal) * 
                                   shadow_atten;
  
  // Indirect lighting
  float3 surface_bias = get_surface_bias(payload.normal, ray.Direction, vol_desc);
  float3 irradiance = 0.0f;

  // TODO(Brandon): Get this from the actual payload data
  float3 lambertian = 1.0f / kPI;

  // Write everything back out
  float3 radiance = direct_diffuse_lighting * lambertian;
  ray_data[output_coords] = float4(saturate(radiance), payload.t);
}

[shader("closesthit")]
void hit(inout Payload payload, BuiltInTriangleIntersectionAttributes attr)
{
	payload.t = RayTCurrent();
  payload.hit_kind = HitKind();

	interlop::Vertex vertex = get_vertex(attr);
	payload.ws_pos          = vertex.position.xyz;
	payload.normal          = vertex.normal.xyz;
	payload.uv              = vertex.uv.xy;
}

[shader("miss")]
void miss(inout Payload payload)
{
	payload.t = -1.0f;
}