#include "tests.h"
#include "math/math.h"
#include "graphics.h"
#include "job_system.h"
#include "threading.h"
#include "context.h"
#include "vendor/imgui/imgui.h"
#include "vendor/imgui/imgui_impl_win32.h"
#include "vendor/imgui/imgui_impl_dx12.h"
#include "render_graph.h"
#include "shaders/interlop.hlsli"

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 610;}
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }

extern IMGUI_IMPL_API LRESULT
ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT CALLBACK
window_proc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam) 
{
	if (ImGui_ImplWin32_WndProcHandler(window, msg, wparam, lparam))
		return true;

	LRESULT res = 0;
	switch (msg)
	{
		case WM_SIZE:
		{
		} break;
		case WM_DESTROY:
		{
			PostQuitMessage(0);
		} break;
		case WM_CLOSE:
		{
			PostQuitMessage(0);
		} break;
		case WM_ACTIVATEAPP:
		{
		} break;
		default:
		{
			res = DefWindowProcW(window, msg, wparam, lparam);
		} break;
	}

	return res;
}

static constexpr const wchar_t* CLASS_NAME = L"AthenaWindowClass";
static constexpr const wchar_t* WINDOW_NAME = L"Athena";

static constexpr u64 INCREMENT_AMOUNT = 10000;

using namespace gfx;

static void
uploader_job(const GraphicsDevice* device, GpuBuffer* dst, const void* src, u64 size)
{
	USE_SCRATCH_ARENA();
	GpuUploadRingBuffer upload_buffer = alloc_gpu_ring_buffer(SCRATCH_ARENA_PASS,
	                                                          device,
	                                                          KiB(64));
	defer { free_gpu_ring_buffer(&upload_buffer); };
	CmdListAllocator cmd_allocator = init_cmd_list_allocator(SCRATCH_ARENA_PASS,
	                                                         device,
	                                                         &device->copy_queue,
	                                                         4);
	defer { destroy_cmd_list_allocator(&cmd_allocator); };
	
	FenceValue fence_value = block_gpu_upload_buffer(&cmd_allocator,
	                                                 &upload_buffer,
	                                                 dst,
	                                                 0, src,
	                                                 size, 4);
	dbgln("Waiting for upload to complete...");
	block_for_fence_value(&upload_buffer.fence, fence_value);
	dbgln("Successfully uploaded to buffer!");
}

static void
frame_entry(MEMORY_ARENA_PARAM,
            const GraphicsDevice* device,
            SwapChain* swap_chain,
            render::TransientResourceCache* resource_cache,
            const PipelineState* cube_pipeline,
            const PipelineState* fullscreen_pipeline,
            const GpuBuffer* vertex_buffer,
            const GpuBuffer* index_buffer)
{
		const GpuImage* back_buffer = swap_chain_acquire(swap_chain);
		u32 display_width = back_buffer->desc.width;
		u32 display_height = back_buffer->desc.height;
		render::RenderGraph graph = render::init_render_graph(MEMORY_ARENA_FWD);


		render::RenderPass* cube_pass = render::add_render_pass(MEMORY_ARENA_FWD, &graph, kCmdQueueTypeGraphics, L"Cube Pass");

		Mat4 proj = perspective_infinite_reverse_lh(PI / 4.0f, f32(display_width) / f32(display_height), 0.1f);
		Mat4 view = look_at_lh(Vec3(0.0, 0.0, -10.0), Vec3(0.0, 0.0, 1.0), Vec3(0.0, 1.0, 0.0));
		Mat4 view_proj = proj * view;
		Mat4 model;
		auto scene_buffer = render::create_buffer<interlop::SceneBuffer>(&graph, L"Scene Buffer", 
			                                                               {.proj = proj,
																																			.view = view,
																																			.view_proj = view_proj});

		auto transform_buffer = render::create_buffer<interlop::TransformBuffer>(&graph, L"Transform Buffer", { .model = model });
		auto position_buffer = render::import_buffer(&graph, vertex_buffer);

		render::Handle<GpuImage> color_buffer = render::create_image(&graph, L"Color Buffer",
		                                                             {.width = display_width,
																																  .height = display_height,
																																  .format = DXGI_FORMAT_R8G8B8A8_UNORM });
		render::Handle<GpuImage> depth_buffer = render::create_image(&graph, L"Depth Buffer", 
		                                                             {.width = display_width,
		                                                              .height = display_height,
																																  .format = DXGI_FORMAT_D16_UNORM});

		render::cmd_clear_render_target_view(cube_pass, &color_buffer, {0.0f, 0.0f, 0.0f, 1.0f});
		render::cmd_clear_depth_stencil_view(cube_pass, &depth_buffer, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 0.0f, 0);
		render::cmd_om_set_render_targets(cube_pass, {color_buffer}, depth_buffer);
		render::cmd_set_pipeline_state(cube_pass, cube_pipeline);
		
		render::cmd_bind_shader_resources<interlop::CubeRenderResources>(cube_pass, {position_buffer, scene_buffer, transform_buffer});
		render::cmd_ia_set_index_buffer(cube_pass, index_buffer, DXGI_FORMAT_R16_UINT);
		render::cmd_draw_indexed_instanced(cube_pass, ARRAY_LENGTH(INDICES), 1, 0, 0, 0);

		render::Handle<GpuImage> graph_back_buffer = render::import_back_buffer(&graph, back_buffer);

		render::RenderPass* fullscreen_pass = render::add_render_pass(MEMORY_ARENA_FWD, &graph, kCmdQueueTypeGraphics, L"Fullscreen Pass");
		render::cmd_clear_render_target_view(fullscreen_pass, &graph_back_buffer, {0.0f, 0.0f, 0.0f, 1.0f});

		render::cmd_om_set_render_targets(fullscreen_pass, {graph_back_buffer}, None);
		render::cmd_set_pipeline_state(fullscreen_pass, fullscreen_pipeline);

		render::Handle<render::Sampler> sampler = render::create_sampler(&graph, L"Fullscreen sampler");
		render::cmd_bind_shader_resources<interlop::FullscreenRenderResources>(fullscreen_pass, {color_buffer, sampler});
		render::cmd_draw_instanced(fullscreen_pass, 3, 1, 0, 0);

		render::execute_render_graph(MEMORY_ARENA_FWD, device, &graph, resource_cache, swap_chain->back_buffer_index);

		swap_chain_submit(swap_chain, device, back_buffer);
}

static void
application_entry(MEMORY_ARENA_PARAM, HINSTANCE instance, int show_code, JobSystem* job_system)
{
//	SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof(wc);
	wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	wc.lpfnWndProc = &window_proc;
	wc.hInstance = instance;
	wc.lpszClassName = CLASS_NAME;

	ASSERT(RegisterClassExW(&wc));

	RECT window_rect = {0, 0, 1920, 1080};
	AdjustWindowRect(&window_rect, WS_OVERLAPPEDWINDOW, 0);

	HWND window = CreateWindowExW(0,
	                              wc.lpszClassName,
	                              WINDOW_NAME,
	                              WS_OVERLAPPEDWINDOW | WS_VISIBLE,
	                              CW_USEDEFAULT,
	                              CW_USEDEFAULT,
	                              window_rect.right - window_rect.left,
	                              window_rect.bottom - window_rect.top,
	                              0,
	                              0,
	                              instance,
	                              0);
	ASSERT(window != nullptr);
	ShowWindow(window, show_code);
	UpdateWindow(window);

	GraphicsDevice graphics_device = init_graphics_device(MEMORY_ARENA_FWD);
	defer { destroy_graphics_device(&graphics_device); };
	SwapChain swap_chain = init_swap_chain(MEMORY_ARENA_FWD, window, &graphics_device);
	defer { destroy_swap_chain(&swap_chain); };

	GpuShader fullscreen_vs = load_shader_from_file(&graphics_device, L"vertex/fullscreen_vs.hlsl.bin");
	GpuShader fullscreen_ps = load_shader_from_file(&graphics_device, L"pixel/fullscreen_ps.hlsl.bin");
	defer {
		destroy_shader(&fullscreen_ps);
		destroy_shader(&fullscreen_vs); 
	};
	GraphicsPipelineDesc pipeline_desc = {0};
	pipeline_desc.vertex_shader = fullscreen_vs;
	pipeline_desc.pixel_shader = fullscreen_ps;
	pipeline_desc.comparison_func = D3D12_COMPARISON_FUNC_GREATER;
	pipeline_desc.stencil_enable = false;
	pipeline_desc.rtv_formats = {swap_chain.format};
	pipeline_desc.dsv_format = None;
	PipelineState fullscreen_pipeline = init_graphics_pipeline(&graphics_device, pipeline_desc, L"Fullscreen Pipeline");
	defer { destroy_pipeline_state(&fullscreen_pipeline); };

	GpuBufferDesc buffer_desc = {0};
	buffer_desc.size = KiB(64);

	GpuBuffer index_buffer = alloc_gpu_buffer_no_heap(&graphics_device,
	                                                  buffer_desc,
	                                                  kGpuHeapTypeLocal,
	                                                  L"Test Index Buffer");
	defer { free_gpu_buffer(&index_buffer); };

	GpuBuffer vertex_buffer = alloc_gpu_buffer_no_heap(&graphics_device,
	                                                   buffer_desc,
	                                                   kGpuHeapTypeLocal,
	                                                   L"Test Vertex Buffer");
	defer { free_gpu_buffer(&vertex_buffer); };

	blocking_kick_job(kJobPriorityLow,
	                  job_system,
	                  uploader_job(&graphics_device, &index_buffer, INDICES, sizeof(INDICES)));
	blocking_kick_job(kJobPriorityLow,
	                  job_system,
	                  uploader_job(&graphics_device, &vertex_buffer, VERTICES, sizeof(VERTICES)));


	interlop::TransformBuffer transform_buf;
	transform_buf.model = Mat4();

	GpuShader cube_vs = load_shader_from_file(&graphics_device, L"vertex/cube_vs.hlsl.bin");
	GpuShader cube_ps = load_shader_from_file(&graphics_device, L"pixel/cube_ps.hlsl.bin");
	defer {
		destroy_shader(&cube_ps);
		destroy_shader(&cube_vs); 
	};
	pipeline_desc.vertex_shader = cube_vs;
	pipeline_desc.pixel_shader = cube_ps;
	pipeline_desc.comparison_func = D3D12_COMPARISON_FUNC_GREATER;
	pipeline_desc.stencil_enable = false;
	pipeline_desc.rtv_formats = {swap_chain.format};
	pipeline_desc.dsv_format = DXGI_FORMAT_D16_UNORM;

	PipelineState cube_pipeline = init_graphics_pipeline(&graphics_device, pipeline_desc, L"Cube Pipeline");
	defer { destroy_pipeline_state(&cube_pipeline); };

//	DescriptorHeap cbv_srv_uav_heap = init_descriptor_heap(MEMORY_ARENA_FWD, &graphics_device, 1, kDescriptorHeapTypeCbvSrvUav);
//	defer { destroy_descriptor_heap(&cbv_srv_uav_heap); };
//
//	init_imgui_ctx(MEMORY_ARENA_FWD, &graphics_device, &swap_chain, window, &cbv_srv_uav_heap);
//	defer { destroy_imgui_ctx(); };
	render::TransientResourceCache resource_cache = render::init_transient_resource_cache(MEMORY_ARENA_FWD, &graphics_device);
	defer { render::destroy_transient_resource_cache(&resource_cache); };

	MemoryArena frame_arena = sub_alloc_memory_arena(MEMORY_ARENA_FWD, MiB(1));

	bool done = false;
	while (!done)
	{
		defer { reset_memory_arena(&frame_arena); };

		MSG message;
		while (PeekMessageW(&message, 0, 0, 0, PM_REMOVE))
		{
			if (message.message == WM_QUIT)
			{
				done = true;
				break;
			}
			TranslateMessage(&message);
			DispatchMessageW(&message);
		}

		if (done)
			break;
		
		blocking_kick_job(kJobPriorityMedium,
		                  job_system,
		                  frame_entry(&frame_arena,
		                              &graphics_device,
		                              &swap_chain,
		                              &resource_cache,
		                              &cube_pipeline,
		                              &fullscreen_pipeline,
		                              &vertex_buffer,
		                              &index_buffer));
	}

	kill_job_system(job_system);
}

int APIENTRY
WinMain(HINSTANCE instance, HINSTANCE prev_instance, PSTR cmdline, int show_code)
{
	set_current_thread_name(L"Athena Main");

	init_application_memory();
	defer { destroy_application_memory(); };

	run_all_tests();

	MemoryArena arena = alloc_memory_arena(MiB(64));
	defer { free_memory_arena(&arena); };

	MemoryArena scratch_arena = sub_alloc_memory_arena(&arena, DEFAULT_SCRATCH_SIZE);
	init_context(scratch_arena);

	JobSystem* job_system = init_job_system(&arena, 512);
	Array<Thread> threads = spawn_job_system_workers(&arena, job_system);

	MemoryArena game_memory = alloc_memory_arena(MiB(128));

	application_entry(&game_memory, instance, show_code, job_system);
	join_threads(threads.memory, static_cast<u32>(threads.size));

	return 0;
}

