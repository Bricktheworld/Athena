#include "../interlop.hlsli"
#include "../root_signature.hlsli"

GlobalRootSignature kBindlessRootSignature =
{
	BINDLESS_ROOT_SIGNATURE
};

TriangleHitGroup kHitGroup = 
{
	"", // Any hit
	"hit", // Closest hit
};

struct Payload
{
	      float  t;
	unorm float3 color;
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

[shader("raygeneration")]
void ray_gen()
{
	Payload payload;
	RaytracingAccelerationStructure accel_struct = ResourceDescriptorHeap[rt_resources.bvh];
//	int ray_index         = DispatchRayIndex().x;
//	int probe_plane_index = DispatchRayIndex().x;
}


[shader("miss")]
void miss(inout Payload payload)
{
}

[shader("closesthit")]
void hit(inout Payload payload, BuiltInTriangleIntersectionAttributes attr)
{
}
