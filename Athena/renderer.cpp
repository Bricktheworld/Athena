#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>

#include "renderer.h"
#include "shaders/interlop.hlsli"
#include "vendor/ufbx/ufbx.h"
#include "vendor/imgui/imgui.h"
#include "vendor/imgui/imgui_impl_win32.h"
#include "vendor/imgui/imgui_impl_dx12.h"

using namespace gfx;
using namespace gfx::render;

ShaderManager
init_shader_manager(const gfx::GraphicsDevice* device)
{
	ShaderManager ret = {0};
	for (u8 i = 0; i < kShaderCount; i++)
	{
		const wchar_t* path = kShaderPaths[i];
		ret.shaders[i] = load_shader_from_file(device, path);
	}

	return ret;
}

void
destroy_shader_manager(ShaderManager* shader_manager)
{
	for (u8 i = 0; i < kShaderCount; i++)
	{
		destroy_shader(&shader_manager->shaders[i]);
	}

	zero_memory(shader_manager, sizeof(ShaderManager));
}

Renderer
init_renderer(MEMORY_ARENA_PARAM, const GraphicsDevice* device, const SwapChain* swap_chain, const ShaderManager& shader_manager, HWND window)
{
	Renderer ret = {0};
	ret.transient_resource_cache = init_transient_resource_cache(MEMORY_ARENA_FWD, device);

	GraphicsPipelineDesc fullscreen_pipeline_desc = 
	{
		.vertex_shader  = shader_manager.shaders[kVsFullscreen],
		.pixel_shader   = shader_manager.shaders[kPsFullscreen],
		.rtv_formats    = Span{swap_chain->format},
	};

	ret.fullscreen_pipeline = init_graphics_pipeline(device, fullscreen_pipeline_desc, L"Fullscreen");

	ret.standard_brdf_pipeline = init_compute_pipeline(device, shader_manager.shaders[kCsStandardBrdf], L"Standard BRDF");
	ret.dof_pipeline           = init_compute_pipeline(device, shader_manager.shaders[kCsDof], L"Depth of Field");
	ret.debug_gbuffer_pipeline = init_compute_pipeline(device, shader_manager.shaders[kCsDebugGBuffer], L"Debug GBuffer");

	ret.imgui_descriptor_heap = init_descriptor_linear_allocator(device, 1, kDescriptorHeapTypeCbvSrvUav);
	init_imgui_ctx(device, swap_chain, window, &ret.imgui_descriptor_heap);

	return ret;
}

void
destroy_renderer(Renderer* renderer)
{
	destroy_imgui_ctx();
	destroy_compute_pipeline(&renderer->debug_gbuffer_pipeline);
	destroy_compute_pipeline(&renderer->standard_brdf_pipeline);
	destroy_graphics_pipeline(&renderer->fullscreen_pipeline);
	destroy_transient_resource_cache(&renderer->transient_resource_cache);
	zero_memory(renderer, sizeof(Renderer));
}

void
begin_renderer_recording(MEMORY_ARENA_PARAM, Renderer* renderer)
{
	renderer->meshes = init_array<Mesh>(MEMORY_ARENA_FWD, 128);
}

void
submit_mesh(Renderer* renderer, Mesh mesh)
{
	*array_add(&renderer->meshes) = mesh;
}

static
Mat4 view_from_camera(Camera* camera)
{
	constexpr float kPitchLimit = kPI / 2.0f - 0.05f;
	camera->pitch = MIN(kPitchLimit, MAX(-kPitchLimit, camera->pitch));
	if (camera->yaw > kPI)
	{
		camera->yaw -= kPI * 2.0f;
	}
	else if (camera->yaw < -kPI)
	{
		camera->yaw += kPI * 2.0f;
	}

	f32 y = sinf(camera->pitch);
	f32 r = cosf(camera->pitch);
	f32 z = r * cosf(camera->yaw);
	f32 x = r * sinf(camera->yaw);

	Vec3 lookat = Vec3(x, y, z);
	return look_at_lh(camera->world_pos, lookat, Vec3(0.0f, 1.0f, 0.0f));
}

static void
draw_debug()
{
}

void
execute_render(MEMORY_ARENA_PARAM,
               Renderer* renderer,
               const gfx::GraphicsDevice* device,
               gfx::SwapChain* swap_chain,
               Camera* camera,
               const gfx::GpuBuffer& vertex_buffer,
               const gfx::GpuBuffer& index_buffer,
               const RenderOptions& render_options)
{
	RenderGraph graph = init_render_graph(MEMORY_ARENA_FWD);

	// Render GBuffers
	RenderPass* geometry_pass = add_render_pass(MEMORY_ARENA_FWD, &graph, kCmdQueueTypeGraphics, L"Geometry Pass");

	Handle<GpuImage> gbuffers[kGBufferRenderTargetCount];
	for (u8 i = 0; i < kGBufferRenderTargetCount; i++)
	{
		GpuImageDesc desc = 
		{
			.width = swap_chain->width,
			.height = swap_chain->height,
			.format = kGBufferRenderTargetFormats[i],
			.color_clear_value = Vec4(0.0f, 0.0f, 0.0f, 0.0f),
		};

		gbuffers[i] = create_image(&graph, kGBufferRenderTargetNames[i], desc);
	}

	GpuImageDesc geometry_depth_desc = 
	{
		.width = swap_chain->width,
		.height = swap_chain->height,
		.format = kGBufferDepthFormat,
		.depth_clear_value = 0.0f,
	};

	Handle<GpuImage> gbuffer_depth = create_image(&graph, L"Geometry Depth Buffer", geometry_depth_desc);

	for (u8 i = 0; i < kGBufferRenderTargetCount; i++)
	{
		cmd_clear_render_target_view(geometry_pass, &gbuffers[i], Vec4(0.0f, 0.0f, 0.0f, 0.0f));
	}
	cmd_clear_depth_stencil_view(geometry_pass, &gbuffer_depth, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0);
	cmd_om_set_render_targets(geometry_pass, gbuffers, gbuffer_depth);

	interlop::Transform transform;
	transform.model = Mat4::columns(Vec4(1, 0, 0, 0),
	                                Vec4(0, 1, 0, 0),
	                                Vec4(0, 0, 1, 0),
	                                Vec4(0, 0, 10, 1));
	transform.model_inverse = transform_inverse_no_scale(transform.model);
	Handle<GpuBuffer> transform_buffer = create_buffer(&graph, L"Transform Buffer", transform);

	interlop::Scene scene;
	scene.proj = perspective_infinite_reverse_lh(kPI / 4.0f, f32(swap_chain->width) / f32(swap_chain->height), kZNear);
	scene.view = view_from_camera(camera);
	scene.view_proj = scene.proj * scene.view;
	scene.camera_world_pos = camera->world_pos;

	Handle<GpuBuffer> scene_buffer = create_buffer(&graph, L"Scene Buffer", scene);

	Handle<GpuBuffer> graph_vertex_buffer = import_buffer(&graph, &vertex_buffer);
	cmd_ia_set_index_buffer(geometry_pass, &index_buffer);
	for (Mesh mesh : renderer->meshes)
	{
		cmd_set_graphics_pso(geometry_pass, &mesh.gbuffer_pso);
		cmd_graphics_bind_shader_resources<interlop::MaterialRenderResources>(geometry_pass, {.vertices = graph_vertex_buffer, .scene = scene_buffer, .transform = transform_buffer});
		cmd_draw_indexed_instanced(geometry_pass, mesh.index_count, 1, mesh.index_buffer_offset, 0, 0);
	}

	// Lighting pass
	RenderPass* lighting_pass = add_render_pass(MEMORY_ARENA_FWD, &graph, kCmdQueueTypeGraphics, L"Lighting Pass");

	GpuImageDesc color_buffer_desc =
	{
		.width = swap_chain->width,
		.height = swap_chain->height,
		.format = kColorBufferFormat,
		.color_clear_value = Vec4(0, 0, 0, 1),
	};

	Handle<GpuImage> color_buffer = create_image(&graph, L"Color Buffer", color_buffer_desc);

	u32 work_groups_x = (swap_chain->width + 15) / 16;
	u32 work_groups_y = (swap_chain->height + 15) / 16;

	interlop::StandardBRDFComputeResources standard_brdf_resources = 
	{
		.scene                          = scene_buffer,
		.gbuffer_material_ids           = gbuffers[kGBufferMaterialId],
		.gbuffer_world_pos              = gbuffers[kGBufferWorldPos],
		.gbuffer_diffuse_rgb_metallic_a = gbuffers[kGBufferDiffuseRGBMetallicA],
		.gbuffer_normal_rgb_roughness_a = gbuffers[kGBufferNormalRGBRoughnessA],
		.render_target                  = color_buffer,
	};

	cmd_set_compute_pso(lighting_pass, &renderer->standard_brdf_pipeline);
	cmd_compute_bind_shader_resources(lighting_pass, standard_brdf_resources);
	cmd_dispatch(lighting_pass, work_groups_x, work_groups_y, 1);


#if 0
	// Do any post processing
	Handle<GpuImage> output_buffer = create_image(&graph, L"Output Buffer", color_buffer_desc);
	RenderPass* post_processing_pass = add_render_pass(MEMORY_ARENA_FWD, &graph, kCmdQueueTypeGraphics, L"Post Processing");

	interlop::DofOptions dof_options = 
	{
		.z_near = kZNear,
		.aperture = render_options.aperture,
		.focal_length = render_options.focal_length,
		.focusing_dist = render_options.focusing_dist,
	};

	Handle<GpuBuffer> dof_options_buffer = create_buffer(&graph, L"Depth of Field Options", dof_options);
	interlop::DofComputeResources dof_resources =
	{
		.options = dof_options_buffer,
		.color_buffer = color_buffer,
		.depth_buffer = gbuffer_depth,
		.render_target = output_buffer,
	};
	cmd_set_compute_pso(post_processing_pass, &renderer->dof_pipeline);
	cmd_compute_bind_shader_resources(post_processing_pass, dof_resources);
	cmd_dispatch(post_processing_pass, work_groups_x, work_groups_y, 1);
#endif


	Handle<GpuImage> debug_buffer = create_image(&graph, L"Debug Buffer", color_buffer_desc);

	// Debug passes will output to the output buffer separately and overwrite anything in it.
	bool using_debug = false;
	RenderPass* debug_pass = add_render_pass(MEMORY_ARENA_FWD, &graph, kCmdQueueTypeGraphics, L"Debug Pass");
	switch(render_options.debug_view)
	{
		case kDebugViewGBufferMaterialID:
		case kDebugViewGBufferWorldPosition:
		case kDebugViewGBufferDiffuse:
		case kDebugViewGBufferMetallic:
		case kDebugViewGBufferNormal:
		case kDebugViewGBufferRoughness:
		case kDebugViewGBufferDepth:
		{
			interlop::DebugGBufferOptions options;
			options.gbuffer_target = u32(render_options.debug_view - kDebugViewGBufferMaterialID);
			Handle<GpuBuffer> gbuffer_options = create_buffer(&graph, L"Debug GBuffer Options", options);
			interlop::DebugGBufferResources debug_gbuffer_resources =
			{
				.options                        = gbuffer_options,
				.gbuffer_material_ids           = gbuffers[kGBufferMaterialId],
				.gbuffer_world_pos              = gbuffers[kGBufferWorldPos],
				.gbuffer_diffuse_rgb_metallic_a = gbuffers[kGBufferDiffuseRGBMetallicA],
				.gbuffer_normal_rgb_roughness_a = gbuffers[kGBufferNormalRGBRoughnessA],
				.gbuffer_depth                  = gbuffer_depth,
				.render_target                  = debug_buffer,
			};
			cmd_set_compute_pso(debug_pass, &renderer->debug_gbuffer_pipeline);
			cmd_compute_bind_shader_resources(debug_pass, debug_gbuffer_resources);
			cmd_dispatch(debug_pass, work_groups_x, work_groups_y, 1);
			using_debug = true;
		} break;
		default: break;
	}

	// Render fullscreen triangle
	const GpuImage* back_buffer = swap_chain_acquire(swap_chain);
	Handle<GpuImage> graph_back_buffer = import_back_buffer(&graph, back_buffer);

	RenderPass* output_pass = add_render_pass(MEMORY_ARENA_FWD, &graph, kCmdQueueTypeGraphics, L"Output Pass");
	cmd_clear_render_target_view(output_pass, &graph_back_buffer, Vec4(0.0f, 0.0f, 0.0f, 1.0f));
	cmd_om_set_render_targets(output_pass, {graph_back_buffer}, None);
	cmd_set_graphics_pso(output_pass, &renderer->fullscreen_pipeline);

	cmd_graphics_bind_shader_resources<interlop::FullscreenRenderResources>(output_pass, {.texture = using_debug ? debug_buffer : color_buffer });
	cmd_draw_instanced(output_pass, 3, 1, 0, 0);
	cmd_draw_imgui_on_top(output_pass, &renderer->imgui_descriptor_heap);

	// Hand off to the GPU
	execute_render_graph(MEMORY_ARENA_FWD, device, &graph, &renderer->transient_resource_cache, swap_chain->back_buffer_index);

	swap_chain_submit(swap_chain, device, back_buffer);

	// Clear the render entries
	clear_array(&renderer->meshes);
}

Scene
init_scene(MEMORY_ARENA_PARAM, const gfx::GraphicsDevice* device)
{
	Scene ret = {0};
	ret.scene_objects = init_array<SceneObject>(MEMORY_ARENA_FWD, 128);
	ret.point_lights  = init_array<interlop::PointLight>(MEMORY_ARENA_FWD, 128);
	ret.scene_object_heap = sub_alloc_memory_arena(MEMORY_ARENA_FWD, MiB(8));
	GpuBufferDesc vertex_uber_desc = {0};
	vertex_uber_desc.size = MiB(512);

	ret.vertex_uber_buffer = alloc_gpu_buffer_no_heap(device, vertex_uber_desc, kGpuHeapTypeLocal, L"Vertex Buffer");

	GpuBufferDesc index_uber_desc = {0};
	index_uber_desc.size = MiB(256);

	ret.index_uber_buffer = alloc_gpu_buffer_no_heap(device, index_uber_desc, kGpuHeapTypeLocal, L"Index Buffer");
	return ret;
}

static UploadContext g_upload_context;

void
init_global_upload_context(MEMORY_ARENA_PARAM, const gfx::GraphicsDevice* device)
{
	GpuBufferDesc staging_desc = {0};
	staging_desc.size = MiB(32);

	g_upload_context.staging_buffer = alloc_gpu_buffer_no_heap(device, staging_desc, kGpuHeapTypeUpload, L"Staging Buffer");
	g_upload_context.staging_offset = 0;
	g_upload_context.cmd_list_allocator = init_cmd_list_allocator(MEMORY_ARENA_FWD, device, &device->copy_queue, 16);
	g_upload_context.cmd_list = alloc_cmd_list(&g_upload_context.cmd_list_allocator);
	g_upload_context.device = device;
	g_upload_context.cpu_upload_arena = sub_alloc_memory_arena(MEMORY_ARENA_FWD, MiB(512));
}

void
destroy_global_upload_context()
{
//	ACQUIRE(&g_upload_context, UploadContext* upload_ctx)
//	{
//		free_gpu_ring_buffer(&upload_ctx->ring_buffer);
//	};
}

static void
print_error(const ufbx_error *error, const char *description)
{
	char buffer[1024];
	ufbx_format_error(buffer, sizeof(buffer), error);
	fprintf(stderr, "%s\n%s\n", description, buffer);
}

static void
flush_upload_staging()
{
	if (g_upload_context.staging_offset == 0)
		return;

	FenceValue value = submit_cmd_lists(&g_upload_context.cmd_list_allocator, {g_upload_context.cmd_list});
	g_upload_context.cmd_list = alloc_cmd_list(&g_upload_context.cmd_list_allocator);

	yield_for_fence_value(&g_upload_context.cmd_list_allocator.fence, value);

	g_upload_context.staging_offset = 0;
}

static void
upload_gpu_data(GpuBuffer* dst_gpu, u64 dst_offset, const void* src, u64 size)
{
	if (g_upload_context.staging_buffer.desc.size - g_upload_context.staging_offset < size)
	{
		flush_upload_staging();
	}
	void* dst = (void*)(u64(unwrap(g_upload_context.staging_buffer.mapped)) + g_upload_context.staging_offset);
	memcpy(dst, src, size);

	g_upload_context.cmd_list.d3d12_list->CopyBufferRegion(dst_gpu->d3d12_buffer,
	                                                       dst_offset,
	                                                       g_upload_context.staging_buffer.d3d12_buffer,
	                                                       g_upload_context.staging_offset,
	                                                       size);
	g_upload_context.staging_offset += size;
}

static u32
alloc_into_vertex_uber(Scene* scene, u32 vertex_count)
{
	u32 ret = scene->vertex_uber_buffer_offset;
	ASSERT((ret + vertex_count) * sizeof(interlop::Vertex) <= scene->vertex_uber_buffer.desc.size);

	scene->vertex_uber_buffer_offset += vertex_count;

	return ret;
}

static u32
alloc_into_index_uber(Scene* scene, u32 index_count)
{
	u32 ret = scene->index_uber_buffer_offset;
	ASSERT((ret + index_count) * sizeof(u16) <= scene->index_uber_buffer.desc.size);

	scene->index_uber_buffer_offset += index_count;

	return ret;
}


static void 
mesh_import_scene(const aiScene* assimp_scene, Array<Mesh>* out,  Scene* scene)
{
	for (u32 imesh = 0; imesh < assimp_scene->mNumMeshes; imesh++)
	{
		reset_memory_arena(&g_upload_context.cpu_upload_arena);

		Mesh* out_mesh = array_add(out);

		const aiMesh* assimp_mesh = assimp_scene->mMeshes[imesh];

		u32 num_vertices = assimp_mesh->mNumVertices;
		u32 num_indices = assimp_mesh->mNumFaces * 3;
		auto* vertices = push_memory_arena<interlop::Vertex>(&g_upload_context.cpu_upload_arena, num_vertices);

		const aiVector3D kAssimpZero3D(0.0f, 0.0f, 0.0f);

		for (u32 ivertex = 0; ivertex < assimp_mesh->mNumVertices; ivertex++)
		{
			const aiVector3D* a_pos     = &assimp_mesh->mVertices[ivertex];
			const aiVector3D* a_normal  = &assimp_mesh->mNormals[ivertex];
			const aiVector3D* a_uv      = assimp_mesh->HasTextureCoords(0) ? &assimp_mesh->mTextureCoords[0][ivertex] : &kAssimpZero3D;
//			const aiVector3D* a_tangent = &assimp_mesh->mTangents[i];
//			const aiVector3D* a_tangent = &assimp_mesh->mTangents[i];
			vertices[ivertex].position = Vec4(a_pos->x, a_pos->y, a_pos->z, 1.0f);
			vertices[ivertex].normal   = Vec4(a_normal->x, a_normal->y, a_normal->z, 1.0f);
			vertices[ivertex].uv       = Vec4(a_uv->x, a_uv->y, 0.0f, 0.0f);
		}

		u32 vertex_buffer_offset = alloc_into_vertex_uber(scene, num_vertices);

		u16* indices = push_memory_arena<u16>(&g_upload_context.cpu_upload_arena, num_indices);

		u32 iindex = 0;
		for (u32 iface = 0; iface < assimp_mesh->mNumFaces; iface++)
		{
			const aiFace* a_face = &assimp_mesh->mFaces[iface];
			if (a_face->mNumIndices != 3)
			{
				dbgln("Skipping face with %u indices", a_face->mNumIndices);
				continue;
			}

			ASSERT(a_face->mNumIndices == 3);
			// TODO(Brandon): This is fucking stupid. Why doesn't d3d12 use BaseVertexLocation to offset these?? I have no fucking clue...
			indices[iindex + 0] = a_face->mIndices[0] + vertex_buffer_offset;
			indices[iindex + 1] = a_face->mIndices[1] + vertex_buffer_offset;
			indices[iindex + 2] = a_face->mIndices[2] + vertex_buffer_offset;
			iindex += 3;
		}

		num_indices = iindex;


		out_mesh->index_count = num_indices;
		out_mesh->index_buffer_offset = alloc_into_index_uber(scene, num_indices);
		upload_gpu_data(&scene->vertex_uber_buffer,
		                vertex_buffer_offset * sizeof(interlop::Vertex),
		                vertices,
		                num_vertices * sizeof(interlop::Vertex));
		upload_gpu_data(&scene->index_uber_buffer,
		                out_mesh->index_buffer_offset * sizeof(u16),
		                indices,
		                num_indices * sizeof(u16));
	}
	flush_upload_staging();
}

static Array<Mesh>
load_mesh_from_file(MEMORY_ARENA_PARAM,
                    Scene* scene,
                    const ShaderManager& shader_manager,
                    const char* mesh_path,
                    ShaderIndex vertex_shader,
                    ShaderIndex material_shader)
{
	Array<Mesh> ret = {0};

	Assimp::Importer importer;
	dbgln("Assimp reading file...");
	const aiScene* assimp_scene = importer.ReadFile(mesh_path, aiProcess_CalcTangentSpace | aiProcess_Triangulate | aiProcess_JoinIdenticalVertices | aiProcess_SortByPType | aiProcess_PreTransformVertices );
	dbgln("Assimp done reading.");

	ASSERT(assimp_scene != nullptr);

	ret = init_array<Mesh>(MEMORY_ARENA_FWD, assimp_scene->mNumMeshes);

	mesh_import_scene(assimp_scene, &ret, scene);

	GraphicsPipelineDesc graphics_pipeline_desc = 
	{
		.vertex_shader = shader_manager.shaders[vertex_shader],
		.pixel_shader = shader_manager.shaders[material_shader],
		.rtv_formats = kGBufferRenderTargetFormats,
		.dsv_format = kGBufferDepthFormat,
		.comparison_func = kDepthComparison,
		.stencil_enable = false,
	};
	GraphicsPSO pso = init_graphics_pipeline(g_upload_context.device, graphics_pipeline_desc, L"Mesh PSO");
	for (Mesh& mesh : ret)
	{
		mesh.vertex_shader = vertex_shader;
		mesh.material_shader = material_shader;
		mesh.gbuffer_pso = pso;
	}

	importer.FreeScene();

	return ret;
}

SceneObject*
add_scene_object(Scene* scene,
                 const ShaderManager& shader_manager,
                 const char* mesh,
                 ShaderIndex vertex_shader,
                 ShaderIndex material_shader)
{
	SceneObject* ret = array_add(&scene->scene_objects);
	ret->flags = kSceneObjectMesh;
	ret->meshes = load_mesh_from_file(&scene->scene_object_heap, scene, shader_manager, mesh, vertex_shader, material_shader);

	return ret;
}

interlop::PointLight* add_point_light(Scene* scene)
{
	interlop::PointLight* ret = array_add(&scene->point_lights);
	ret->position = Vec4(0, 0, 0, 1);
	ret->color = Vec4(1, 1, 1, 1);
	ret->radius = 10;
	ret->intensity = 10;

	return ret;
}

void
submit_scene(const Scene& scene, Renderer* renderer)
{
	for (const SceneObject& obj : scene.scene_objects)
	{
//		u8 flags = obj.flags;
//		if (flags & kSceneObjectPendingLoad)
//		{
//			if (!job_has_completed(obj.loading_signal))
//				continue;
//
//			flags &= ~kSceneObjectPendingLoad;
//		}

		for (const Mesh& mesh : obj.meshes)
		{
			submit_mesh(renderer, mesh);
		}
	}
}
