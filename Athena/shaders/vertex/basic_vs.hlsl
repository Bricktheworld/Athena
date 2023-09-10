#include "../root_signature.hlsli"
#include "../interlop.hlsli"

ConstantBuffer<interlop::MaterialRenderResources> render_resources : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
shaders::BasicVSOut main(uint vert_id: SV_VertexID)
{
	shaders::BasicVSOut ret;

	StructuredBuffer<interlop::Vertex>  vertices  = ResourceDescriptorHeap[render_resources.vertices];
	ConstantBuffer<interlop::Scene>     scene     = ResourceDescriptorHeap[render_resources.scene];
	ConstantBuffer<interlop::Transform> transform = ResourceDescriptorHeap[render_resources.transform];

	interlop::Vertex vertex = vertices[vert_id];

	ret.world_pos = float4(vertex.position.xyz, 1.0f); //mul(transform.model, float4(vertex.position.xyz, 1.0f));
	ret.ndc_pos   = mul(scene.view_proj, ret.world_pos);

	float3x3 normal_matrix = (float3x3)transpose(transform.model_inverse);
	// float3 tangent = normalize(mul(normal_matrix, vertex.tangent));
	float3 normal  = normalize(mul(normal_matrix, vertex.normal.xyz));

//	tangent = normalize(tangent - mul(dot(tangent, normal), normal));
//
//	float3 bitangent = cross(normal, tangent);
//
//	float3x3 tbn_matrix = float3x3(tangent, bitangent, normal);

	ret.normal    = float4(normal, 1.0f);  //vertex.normal;
	ret.uv        = vertex.uv;
	return ret;
}