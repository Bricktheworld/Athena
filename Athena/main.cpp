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
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = u8".\\D3D12\\"; }

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

static void
increment_job(uintptr_t param)
{
	volatile u64* data = reinterpret_cast<volatile u64*>(param);
	for (u64 i = 0; i < INCREMENT_AMOUNT; i++)
	{
		InterlockedIncrement(data);
	}
}

static void
run_render_graph()
{
	USE_SCRATCH_ARENA();

	using namespace rg;
	RenderGraph render_graph = init_render_graph(SCRATCH_ARENA_PASS);

	RenderPass* pass0 = add_render_pass(SCRATCH_ARENA_PASS, &render_graph, "pass0");
	RenderPass* pass1 = add_render_pass(SCRATCH_ARENA_PASS, &render_graph, "pass1");
	RenderPass* pass2 = add_render_pass(SCRATCH_ARENA_PASS, &render_graph, "pass2");
	RenderPass* pass3 = add_render_pass(SCRATCH_ARENA_PASS, &render_graph, "pass3");
	RenderPass* pass4 = add_render_pass(SCRATCH_ARENA_PASS, &render_graph, "pass4");
	RenderPass* pass5 = add_render_pass(SCRATCH_ARENA_PASS, &render_graph, "pass5");
	RenderPass* pass6 = add_render_pass(SCRATCH_ARENA_PASS, &render_graph, "pass6");
	RenderPass* pass7 = add_render_pass(SCRATCH_ARENA_PASS, &render_graph, "pass7");
	RenderPass* pass8 = add_render_pass(SCRATCH_ARENA_PASS, &render_graph, "pass8");

	Handle<GpuImage> pass0_out = render_graph_create_image(&render_graph, "pass0_out", {});
	Handle<GpuImage> pass1_out = render_graph_create_image(&render_graph, "pass1_out", {});
	Handle<GpuImage> pass2_out = render_graph_create_image(&render_graph, "pass2_out", {});
	Handle<GpuImage> pass3_out = render_graph_create_image(&render_graph, "pass3_out", {});
	Handle<GpuImage> pass4_out = render_graph_create_image(&render_graph, "pass4_out", {});
	Handle<GpuImage> pass5_out = render_graph_create_image(&render_graph, "pass5_out", {});
	Handle<GpuImage> pass6_out = render_graph_create_image(&render_graph, "pass6_out", {});
	Handle<GpuImage> pass7_out = render_graph_create_image(&render_graph, "pass7_out", {});
	Handle<GpuImage> pass8_out = render_graph_create_image(&render_graph, "pass8_out", {});

	render_pass_write(pass8, &pass8_out);

	render_pass_write(pass6, &pass6_out);

	render_pass_read(pass3, pass6_out);
	render_pass_read(pass3, pass8_out);
	render_pass_write(pass3, &pass3_out);

	render_pass_write(pass7, &pass7_out);

	render_pass_write(pass5, &pass5_out);

	render_pass_read(pass4, pass5_out);
	render_pass_read(pass4, pass7_out);
	render_pass_write(pass4, &pass4_out);

	render_pass_read(pass1, pass4_out);
	render_pass_write(pass1, &pass1_out);

	render_pass_read(pass2, pass3_out);
	render_pass_write(pass2, &pass2_out);

	render_pass_read(pass0, pass3_out);
	render_pass_read(pass0, pass2_out);
	render_pass_read(pass0, pass1_out);
	render_pass_write(pass0, &pass0_out);


	CompiledRenderGraph compiled = compile_render_graph(SCRATCH_ARENA_PASS, &render_graph, nullptr);
}

static void
window_setup(uintptr_t p_param)
{
#if 0

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO* io = &ImGui::GetIO();
	io->ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

	ImGui::StyleColorsDark();

	D3D12_DESCRIPTOR_HEAP_DESC desc = {};
	desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	desc.NumDescriptors = 1;
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	ID3D12DescriptorHeap* imgui_desc_heap = nullptr;
	HASSERT(graphics_device.dev->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&imgui_desc_heap)));

	ID3D12Device* imgui_dev = nullptr;
	HASSERT(graphics_device.dev->QueryInterface(IID_PPV_ARGS(&imgui_dev)));

	ImGui_ImplWin32_Init(window);
	ImGui_ImplDX12_Init(imgui_dev,
	                    FRAMES_IN_FLIGHT,
	                    DXGI_FORMAT_R8G8B8A8_UNORM,
	                    imgui_desc_heap,
	                    imgui_desc_heap->GetCPUDescriptorHandleForHeapStart(),
	                    imgui_desc_heap->GetGPUDescriptorHandleForHeapStart());

	bool show_demo_window = true;
	bool show_another_window = false;

	bool done = false;
	while (!done)
	{
		run_render_graph();

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

		gd_update(&graphics_device);
		{
			// Start the Dear ImGui frame
			ImGui_ImplDX12_NewFrame();
			ImGui_ImplWin32_NewFrame();
			ImGui::NewFrame();

			// 1. Show the big demo window (Most of the sample code is in ImGui::ShowDemoWindow()! You can browse its code to learn more about Dear ImGui!).
			if (show_demo_window)
				ImGui::ShowDemoWindow(&show_demo_window);

			// 2. Show a simple window that we create ourselves. We use a Begin/End pair to create a named window.
			{
				static float f = 0.0f;
				static int counter = 0;

				ImGui::Begin("Hello, world!");                          // Create a window called "Hello, world!" and append into it.

				ImGui::Text("This is some useful text.");               // Display some text (you can use a format strings too)
				ImGui::Checkbox("Demo Window", &show_demo_window);      // Edit bools storing our window open/close state
				ImGui::Checkbox("Another Window", &show_another_window);

				ImGui::SliderFloat("float", &f, 0.0f, 1.0f);            // Edit 1 float using a slider from 0.0f to 1.0f

				if (ImGui::Button("Button"))                            // Buttons return true when clicked (most widgets return true when edited/activated)
					counter++;
				ImGui::SameLine();
				ImGui::Text("counter = %d", counter);

				ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / io->Framerate, io->Framerate);
				ImGui::End();
			}

			// 3. Show another simple window.
			if (show_another_window)
			{
				ImGui::Begin("Another Window", &show_another_window);   // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
				ImGui::Text("Hello from another window!");
				if (ImGui::Button("Close Me"))
					show_another_window = false;
				ImGui::End();
			}

			// Rendering
			ImGui::Render();
			graphics_device.cmd_list->SetDescriptorHeaps(1, &imgui_desc_heap);
			ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), graphics_device.cmd_list);
		}

		gd_present(&graphics_device);

	}

	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	COM_RELEASE(imgui_desc_heap);
	COM_RELEASE(imgui_dev);
	destroy_graphics_device(&graphics_device);
#endif
}

struct FrameEntryParams
{
	GraphicsDevice* graphics_device = nullptr;
	SwapChain* swap_chain = nullptr;
	GraphicsPipeline* fullscreen_pipeline = nullptr;
	GraphicsPipeline* cube_pipeline = nullptr;
	GpuBuffer* index_buffer = nullptr;
	DescriptorHeap* cbv_srv_uav_heap = nullptr;
	BufferSrv* vertex_srv = nullptr;
	BufferCbv* scene_cbv = nullptr;
	BufferCbv* transform_cbv = nullptr;
};

static void
frame_entry(uintptr_t ptr)
{
	FrameEntryParams* params = reinterpret_cast<FrameEntryParams*>(ptr);
	CmdList cmd = alloc_cmd_list(&params->graphics_device->graphics_cmd_allocator);
	RenderTargetView* rtv = swap_chain_acquire(params->swap_chain);

	cmd_set_primitive_topology(&cmd);
	cmd_set_graphics_root_signature(&cmd);

	cmd_image_transition(&cmd, rtv->image, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	cmd_set_render_targets(&cmd, rtv, 1, None);
	cmd_set_viewport(&cmd,
	                 0.0,
	                 0.0,
	                 static_cast<f32>(params->swap_chain->width),
	                 static_cast<f32>(params->swap_chain->height));
	cmd_set_scissor(&cmd);

	cmd_clear_rtv(&cmd, rtv, Rgba(1.0, 0.0, 0.0, 1.0));
	cmd_clear_dsv(&cmd, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, &params->swap_chain->depth_stencil_view, 0.0f, 0);

	cmd_set_pipeline(&cmd, params->fullscreen_pipeline);
	cmd_set_index_buffer(&cmd, params->index_buffer);
	cmd_draw(&cmd, 3);

	cmd_set_descriptor_heaps(&cmd, params->cbv_srv_uav_heap, 1);

	cmd_set_pipeline(&cmd, params->cube_pipeline);
	interlop::CubeRenderResources cube_resources = {0};
	cube_resources.position_idx = params->vertex_srv->descriptor.index;
	cube_resources.scene_idx = params->scene_cbv->descriptor.index;
	cube_resources.transform_idx = params->transform_cbv->descriptor.index;
	cmd_set_graphics_32bit_constants(&cmd, &cube_resources);
	cmd_set_index_buffer(&cmd, params->index_buffer);
	cmd_draw_indexed(&cmd, ARRAY_LENGTH(INDICES));

	cmd_image_transition(&cmd, rtv->image, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);
	submit_cmd_list(&params->graphics_device->graphics_cmd_allocator, &cmd);
	swap_chain_submit(params->swap_chain, params->graphics_device, rtv);
}

struct UploaderParams
{
	const GraphicsDevice* device = nullptr;
	GpuBuffer* dst = nullptr;
	const void* src = nullptr;
	u64 size = 0;
};

static void
uploader_job(uintptr_t ptr)
{
	USE_SCRATCH_ARENA();
	UploaderParams* params = reinterpret_cast<UploaderParams*>(ptr);
	GpuUploadRingBuffer upload_buffer = alloc_gpu_ring_buffer(SCRATCH_ARENA_PASS,
	                                                          params->device,
	                                                          KiB(64));
	defer { free_gpu_ring_buffer(&upload_buffer); };
	CmdListAllocator cmd_allocator = init_cmd_list_allocator(SCRATCH_ARENA_PASS,
	                                                         params->device,
	                                                         &params->device->copy_queue,
	                                                         4);
	defer { destroy_cmd_list_allocator(&cmd_allocator); };
	
	FenceValue fence_value = block_gpu_upload_buffer(&cmd_allocator,
	                                                 &upload_buffer,
	                                                 params->dst,
	                                                 0, params->src,
	                                                 params->size, 4);
	dbgln("Waiting for upload to complete...");
	block_for_fence_value(&upload_buffer.fence, fence_value);
	dbgln("Successfully uploaded to buffer!");
}

static void
application_entry(MEMORY_ARENA_PARAM, HINSTANCE instance, int show_code, JobSystem* job_system)
{
	SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

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
	pipeline_desc.rtv_formats[0] = swap_chain.format;
	pipeline_desc.num_render_targets = 1;
	pipeline_desc.dsv_format = None;
	GraphicsPipeline fullscreen_pipeline = init_graphics_pipeline(&graphics_device, pipeline_desc, L"Fullscreen Pipeline");
	defer { destroy_graphics_pipeline(&fullscreen_pipeline); };

	GpuBufferDesc buffer_desc = {0};
	buffer_desc.size = KiB(64);
	buffer_desc.heap_type = GPU_HEAP_TYPE_LOCAL;

	GpuBuffer index_buffer = alloc_buffer_no_heap(&graphics_device, buffer_desc, L"Test Index Buffer");
	defer { free_buffer_no_heap(&index_buffer); };

	GpuBuffer vertex_buffer = alloc_buffer_no_heap(&graphics_device, buffer_desc, L"Test Vertex Buffer");
	defer { free_buffer_no_heap(&vertex_buffer); };

	UploaderParams index_uploader_params = {0};
	index_uploader_params.device = &graphics_device;
	index_uploader_params.dst = &index_buffer;
	index_uploader_params.src = INDICES;
	index_uploader_params.size = sizeof(INDICES);

	UploaderParams vertex_uploader_params = {0};
	vertex_uploader_params.device = &graphics_device;
	vertex_uploader_params.dst = &vertex_buffer;
	vertex_uploader_params.src = VERTICES;
	vertex_uploader_params.size = sizeof(VERTICES);

	Job index_uploader_job = {0};
	index_uploader_job.entry = &uploader_job,
	index_uploader_job.param = (uintptr_t)&index_uploader_params;
	blocking_kick_job(JOB_PRIORITY_LOW, index_uploader_job, job_system);
	Job vertex_uploader_job = {0};
	vertex_uploader_job.entry = &uploader_job;
	vertex_uploader_job.param = (uintptr_t)&vertex_uploader_params;
	blocking_kick_job(JOB_PRIORITY_LOW, vertex_uploader_job, job_system);

	DescriptorHeap cbv_srv_uav_heap = init_descriptor_heap(MEMORY_ARENA_FWD,
	                                                       &graphics_device,
	                                                       1024,
	                                                       DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	defer { destroy_descriptor_heap(&cbv_srv_uav_heap); };

	BufferSrv vertex_srv = alloc_buffer_srv(&graphics_device,
	                                        &cbv_srv_uav_heap,
	                                        &vertex_buffer,
	                                        0,
	                                        ARRAY_LENGTH(VERTICES),
	                                        sizeof(Vec4));

	buffer_desc.heap_type = GPU_HEAP_TYPE_UPLOAD;
	GpuBuffer scene_buffer = alloc_buffer_no_heap(&graphics_device, buffer_desc, L"Scene buffer");
	GpuBuffer transform_buffer = alloc_buffer_no_heap(&graphics_device, buffer_desc, L"Transform buffer");
	defer { 
		free_buffer_no_heap(&transform_buffer);
		free_buffer_no_heap(&scene_buffer);
	};

	BufferCbv scene_cbv = alloc_buffer_cbv(&graphics_device,
	                                       &cbv_srv_uav_heap,
	                                       &scene_buffer,
	                                       0, sizeof(interlop::SceneBuffer));
	BufferCbv transform_cbv = alloc_buffer_cbv(&graphics_device,
	                                           &cbv_srv_uav_heap,
	                                           &transform_buffer,
	                                           0, sizeof(interlop::TransformBuffer));

	interlop::SceneBuffer scene_buf;
	scene_buf.proj = perspective_infinite_reverse_lh(PI / 4.0f,
	                                                 static_cast<f32>(swap_chain.width) /
	                                                 static_cast<f32>(swap_chain.height),
	                                                 0.1f);
	scene_buf.view = look_at_lh(Vec3(0.0, 0.0, -10.0), Vec3(0.0, 0.0, 1.0), Vec3(0.0, 1.0, 0.0));
	scene_buf.view_proj = scene_buf.proj * scene_buf.view;
	memcpy(unwrap(scene_buffer.mapped), &scene_buf, sizeof(scene_buf));

	interlop::TransformBuffer transform_buf;
	transform_buf.model = Mat4();
	memcpy(unwrap(transform_buffer.mapped), &transform_buf, sizeof(transform_buf));

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
	pipeline_desc.rtv_formats[0] = swap_chain.format;
	pipeline_desc.num_render_targets = 1;
	pipeline_desc.dsv_format = None;
	GraphicsPipeline cube_pipeline = init_graphics_pipeline(&graphics_device, pipeline_desc, L"Cube Pipeline");
	defer { destroy_graphics_pipeline(&cube_pipeline); };


	FrameEntryParams frame_entry_params = {0};
	frame_entry_params.graphics_device = &graphics_device;
	frame_entry_params.swap_chain = &swap_chain;
	frame_entry_params.fullscreen_pipeline = &fullscreen_pipeline;
	frame_entry_params.cube_pipeline = &cube_pipeline;
	frame_entry_params.index_buffer = &index_buffer;
	frame_entry_params.cbv_srv_uav_heap = &cbv_srv_uav_heap;
	frame_entry_params.vertex_srv = &vertex_srv;
	frame_entry_params.scene_cbv = &scene_cbv;
	frame_entry_params.transform_cbv = &transform_cbv;

	bool done = false;
	while (!done)
	{
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

		Job job = {0};
		job.entry = &frame_entry;
		job.param = reinterpret_cast<uintptr_t>(&frame_entry_params);
		blocking_kick_job(JOB_PRIORITY_MEDIUM, job, job_system);
	}
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

	kill_job_system(job_system);

	return 0;
}

