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
	GpuShader* vertex_shader = nullptr;
	GpuShader* pixel_shader = nullptr;
};

static void
frame_entry(uintptr_t ptr)
{
	auto* params = reinterpret_cast<FrameEntryParams*>(ptr);
	CmdList cmd = alloc_cmd_list(&params->graphics_device->graphics_cmd_allocator);
	RenderTargetView* rtv = swap_chain_acquire(params->swap_chain);

	cmd_image_transition(&cmd, rtv->image, D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET);
	cmd_set_viewport(&cmd,
	                 0.0,
	                 0.0,
	                 static_cast<f32>(params->swap_chain->width),
	                 static_cast<f32>(params->swap_chain->height));
	cmd_set_scissor(&cmd);
#define RAND_F32 ((f32)rand()/(f32)(RAND_MAX))
	cmd_clear_rtv(&cmd, rtv, Rgba(RAND_F32, RAND_F32, RAND_F32, 1.0));
	cmd_clear_dsv(&cmd, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, &params->swap_chain->depth_stencil_view, 0.0f, 0);
	cmd_image_transition(&cmd, rtv->image, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT);

	submit_cmd_list(&params->graphics_device->graphics_cmd_allocator, &cmd);
	swap_chain_submit(params->swap_chain, params->graphics_device, rtv);
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

	GpuShader vs = load_shader_from_file(&graphics_device, L"vertex/test_vs.hlsl.bin");
	GpuShader ps = load_shader_from_file(&graphics_device, L"pixel/test_ps.hlsl.bin");
	defer {
		destroy_shader(&ps);
		destroy_shader(&vs); 
	};

	FrameEntryParams frame_entry_params = {0};
	frame_entry_params.graphics_device = &graphics_device;
	frame_entry_params.swap_chain = &swap_chain;
	frame_entry_params.vertex_shader = &vs;
	frame_entry_params.pixel_shader = &ps;

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

