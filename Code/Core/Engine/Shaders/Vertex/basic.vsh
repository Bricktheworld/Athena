#include "../root_signature.hlsli"
#include "../interlop.hlsli"

ConstantBuffer<MaterialRenderResources> render_resources : register(b0);

[RootSignature(BINDLESS_ROOT_SIGNATURE)]
BasicVSOut VS_Basic(uint vert_id: SV_VertexID)
{
	BasicVSOut ret;

	ConstantBuffer<Transform> transform = ResourceDescriptorHeap[render_resources.transform];

	Vertex vertex = g_VertexBuffer[vert_id];

	ret.world_pos = mul(transform.model, float4(vertex.position.xyz, 1.0f));
	ret.ndc_pos   = mul(g_ViewportBuffer.view_proj, ret.world_pos);

  ret.curr_pos  = ret.ndc_pos;
  ret.prev_pos  = mul(g_ViewportBuffer.prev_view_proj, ret.world_pos);

  ret.ndc_pos  += float4(g_ViewportBuffer.taa_jitter * ret.ndc_pos.w, 0.0f, 0.0f);

	float3x3 normal_matrix = (float3x3)transpose(transform.model_inverse);
	float3   normal        = normalize(mul(normal_matrix, vertex.normal.xyz));

//	tangent = normalize(tangent - mul(dot(tangent, normal), normal));
//
//	float3 bitangent = cross(normal, tangent);
//
//	float3x3 tbn_matrix = float3x3(tangent, bitangent, normal);

	ret.normal    = normal;
	ret.uv        = vertex.uv;
	return ret;
}