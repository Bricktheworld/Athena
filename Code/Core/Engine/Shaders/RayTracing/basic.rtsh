#include "../interlop.hlsli"
#include "../root_signature.hlsli"
#include "../include/rt_common.hlsli"
#include "../include/lighting_common.hlsli"
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

ConstantBuffer<interlop::BasicRTResources> rt_resources : register(b0);

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

	Payload payload;

	ConstantBuffer<interlop::Scene>       scene            = ResourceDescriptorHeap[rt_resources.scene];
	RWTexture2D<float4>                   render_target    = ResourceDescriptorHeap[rt_resources.render_target];
	ConstantBuffer<interlop::DDGIVolDesc> vol_desc         = ResourceDescriptorHeap[rt_resources.vol_desc];
  Texture2DArray<float4>                probe_irradiance = ResourceDescriptorHeap[rt_resources.probe_irradiance];
  Texture2DArray<float2>                probe_distance   = ResourceDescriptorHeap[rt_resources.probe_distance];

	interlop::DirectionalLight directional_light = scene.directional_light;

	float3 ws_pos   = get_camera_world_pos(launch_index, scene.inverse_view_proj);
	float3 ray_orig = scene.camera_world_pos.xyz;
	float3 ray_dir  = normalize(ws_pos - ray_orig);

	RayDesc ray = (RayDesc)0;
	ray.Origin    = ray_orig;
	ray.Direction = ray_dir;
	ray.TMin = 0.0f;
	ray.TMax = 1e27f;
	TraceRay(g_AccelerationStructure, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xFF, 0, 0, 0, ray, payload);

	if (payload.t < 0.0f)
	{
		render_target[launch_index] = float4(0.0f, 0.0f, 0.0f, 1.0f);
		return;
	}

	float  shadow_atten = saturate(light_visibility(directional_light.direction.xyz,
                                                  payload.ws_pos,
                                                  payload.normal,
                                                  1e27f,
                                                  0.001f));

	float3 view_direction = normalize(scene.camera_world_pos.xyz - payload.ws_pos);
  float3 direct_diffuse_lighting = evaluate_directional_radiance(directional_light.diffuse.xyz, directional_light.intensity) * 
                                   evaluate_cos_theta(directional_light.direction.xyz, payload.normal) * shadow_atten;

  float3 cam_dir      = normalize(payload.ws_pos - scene.camera_world_pos.xyz);
  float3 surface_bias = get_surface_bias(payload.normal, cam_dir, vol_desc);
  float3 indirect = get_vol_irradiance(payload.ws_pos,
                                       surface_bias,
                                       payload.normal,
                                       vol_desc,
                                       probe_irradiance,
                                       probe_distance);

  float3 irradiance = direct_diffuse_lighting + saturate(indirect);
  float3 lambertian = evaluate_lambertian(1.0f);

	float3 color = payload.t < 0.0f ? float3(0.0f, 0.0f, 0.0f) : lambertian * irradiance;
	render_target[launch_index] = float4(color, 1.0f);
}

[shader("closesthit")]
void hit(inout Payload payload, BuiltInTriangleIntersectionAttributes attr)
{
	payload.t = RayTCurrent();

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