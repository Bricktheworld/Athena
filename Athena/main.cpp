#include "tests.h"
#include "math/math.h"
#include "graphics.h"
#include "job_system.h"
#include "threading.h"
#include "context.h"
#include "vendor/imgui/imgui.h"
#include "vendor/imgui/imgui_impl_win32.h"
#include "vendor/imgui/imgui_impl_dx12.h"

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
frame_entry(uintptr_t param)
{
	volatile u64 data = param;
	static constexpr size_t JOB_FORK_COUNT = 128;
	Job jobs[JOB_FORK_COUNT];
	for (u32 i = 0; i < JOB_FORK_COUNT; i++)
	{
		jobs[i].entry = &increment_job;
		jobs[i].param = (uintptr_t)&data;
	}

	JobCounterID counter = kick_jobs(JOB_PRIORITY_HIGH, jobs, JOB_FORK_COUNT);
	yield_to_counter(counter);
	ASSERT(data == param + JOB_FORK_COUNT * INCREMENT_AMOUNT);

	Job recursive_job = {0};
	recursive_job.entry = &frame_entry;
	recursive_job.param = data;

	kick_job(JOB_PRIORITY_HIGH, recursive_job);
}

struct WindowSetupParam
{
	HINSTANCE instance = 0;
	int show_code = 0;
};

static void
window_setup(uintptr_t p_param)
{
	auto* param = (WindowSetupParam*)p_param;

	SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

	WNDCLASSEXW wc = {};
	wc.cbSize = sizeof(wc);
	wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	wc.lpfnWndProc = &window_proc;
	wc.hInstance = param->instance;
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
	                              param->instance,
	                              0);
	ASSERT(window != nullptr);
	ShowWindow(window, param->show_code);
	UpdateWindow(window);

	GraphicsDevice graphics_device = init_graphics_device(window);

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

	kill_job_system(get_job_system());
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

	WindowSetupParam param = { 0 };
	param.instance = instance;
	param.show_code = show_code;

	Job startup_job = {0};
	startup_job.entry = &window_setup;
	startup_job.param = reinterpret_cast<uintptr_t>(&param);
	kick_job(JOB_PRIORITY_HIGH, startup_job, job_system);

	join_threads(threads.memory, static_cast<u32>(threads.size));

	return 0;
}

