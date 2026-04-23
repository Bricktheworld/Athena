//#include "Core/Foundation/tests.h"
#include "Core/Foundation/math.h"
#include "Core/Foundation/threading.h"
#include "Core/Foundation/context.h"
#include "Core/Foundation/profiling.h"
#include "Core/Foundation/filesystem.h"

#include "Core/Engine/memory.h"
#include "Core/Engine/scene.h"
#include "Core/Engine/job_system.h"
#include "Core/Engine/asset_streaming.h"
#include "Core/Engine/asset_server.h"

#include "Core/Engine/Render/graphics.h"
#include "Core/Engine/Render/renderer.h"
#include "Core/Engine/Render/render_graph.h"
#include "Core/Engine/Render/misc.h"


#include "Core/Engine/Vendor/imgui/imgui.h"
#include "Core/Engine/Vendor/imgui/imgui_impl_win32.h"
#include "Core/Engine/Vendor/imgui/imgui_impl_dx12.h"

#include "Core/Engine/Shaders/interlop.hlsli"

#include "Core/Engine/Vendor/DirectXTK/Mouse.h"
#include "Core/Engine/Vendor/DirectXTK/Keyboard.h"

#include "Core/Vendor/LivePP/API/x64/LPP_API_x64_CPP.h"

extern IMGUI_IMPL_API LRESULT
ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

Window*      g_MainWindow  = nullptr;

// TODO(bshihabi): Move these elsewhere or into a single struct
static bool g_EnableFullscreen       = false;
static bool g_EnableValidationLayers = false;
static bool g_EnableGpuValidation    = false;

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
      if (g_MainWindow != nullptr)
      {
        g_MainWindow->needs_resize = true;
      }
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
      DirectX::Keyboard::ProcessMessage(msg, wparam, lparam);
      DirectX::Mouse::ProcessMessage(msg, wparam, lparam);
    } break;
    case WM_ACTIVATE:
    case WM_INPUT:
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MOUSEWHEEL:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_MOUSEHOVER:
    {
      DirectX::Mouse::ProcessMessage(msg, wparam, lparam);
    } break;
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYUP:
    {
      DirectX::Keyboard::ProcessMessage(msg, wparam, lparam);
    } break;
    case WM_SYSKEYDOWN:
    {
      DirectX::Keyboard::ProcessMessage(msg, wparam, lparam);
    } break;
    case WM_MOUSEACTIVATE:
    {
      // When you click activate the window, we want Mouse to ignore it.
      return MA_ACTIVATEANDEAT;
    }
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
application_entry(HINSTANCE instance, int show_code)
{
//  SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  HICON icon = LoadIconA(instance, MAKEINTRESOURCEA(1));

  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
  wc.lpfnWndProc = &window_proc;
  wc.hInstance = instance;
  wc.lpszClassName = CLASS_NAME;
  wc.hIcon = icon;

  RegisterClassExW(&wc);

  RECT window_rect = {0, 0, 1920, 1080};

  DWORD dw_style = g_EnableFullscreen ? WS_POPUP : WS_OVERLAPPEDWINDOW;

  AdjustWindowRect(&window_rect, dw_style, 0);

  HWND window = CreateWindowExW(
    0,
    wc.lpszClassName,
    WINDOW_NAME,
    dw_style | WS_VISIBLE,
    CW_USEDEFAULT,
    CW_USEDEFAULT,
    window_rect.right - window_rect.left,
    window_rect.bottom - window_rect.top,
    0,
    0,
    instance,
    0
  );
  ASSERT(window != nullptr);
  ShowWindow(window, show_code);
  UpdateWindow(window);

  u32 gpu_flags = 0;
  if (g_EnableValidationLayers)
  {
    gpu_flags |= kGpuFlagsEnableValidationLayers;
  }

  if (g_EnableGpuValidation)
  {
    gpu_flags |= kGpuFlagsEnableGpuValidation;
  }

  init_gpu_device(window, gpu_flags);
  defer { destroy_gpu_device(); };

  // init_asset_loader();
  // defer { destroy_asset_loader(); };

  init_asset_registry();
  init_asset_streamer();
  defer { destroy_asset_streamer(); };

  g_MainWindow = HEAP_ALLOC(Window, g_InitHeap, 1);

  g_MainWindow->swap_chain = init_swap_chain(window, g_GpuDevice);
  defer { destroy_swap_chain(&g_MainWindow->swap_chain); };

  Result<void, SocketErr> res = init_asset_server("127.0.0.1", 8000);
  if (!res)
  {
    return;
  }
  defer { destroy_asset_server(); };

  init_shader_manager(g_GpuDevice);
  defer { destroy_shader_manager(); };

  init_renderer(g_GpuDevice, &g_MainWindow->swap_chain, window);
  defer { destroy_renderer(); };

  init_unified_geometry_buffer(g_GpuDevice);
  defer { destroy_unified_geometry_buffer(); };

  init_scene();

  {
    CPU_PROFILE_SCOPE("Load Sponza");
    ModelHandle sponza_model = kick_model_load(ASSET_ID("Assets/Source/sponza/Sponza.gltf"));
    // kick_asset_load(sponza_model);
    while (true)
    {
      if (!sponza_model.is_loaded())
      {
        continue;
      }

      for (u32 isubset = 0; isubset < sponza_model->subsets.size; isubset++)
      {
        SceneObjHandle handle = init_render_scene_obj(sponza_model, isubset);
        (void)handle;
      }

      dbgln("Loaded sponza!");
      break;
    }
  }

  // TODO(bshihabi): Remove this... It is temporary for BVH building
  wait_for_gpu_device_idle(g_GpuDevice);
  build_acceleration_structures();


  DirectX::Keyboard d3d12_keyboard;
  DirectX::Mouse d3d12_mouse;
  d3d12_mouse.SetWindow(window);

  lpp::LppSynchronizedAgent lpp_agent = lpp::LppCreateSynchronizedAgent(nullptr, L"Code/Core/Vendor/LivePP");

  bool lpp_is_valid = lpp::LppIsValidSynchronizedAgent(&lpp_agent);
  if (!lpp_is_valid)
  {
    dbgln("Warning, LPP not initialized! You probably aren't running the engine in the correct working directory...");
  }
  else
  {
    lpp_agent.EnableModule(lpp::LppGetCurrentModulePath(), lpp::LPP_MODULES_OPTION_ALL_IMPORT_MODULES, nullptr, nullptr);
  }

  Camera* camera = get_scene_camera();
  camera->world_pos = Vec3(8.28f, 4.866f, 0.685f);
  camera->pitch     = -0.203f;
  camera->yaw       = -1.61f;

  DirectionalLight* directional_light = get_scene_directional_light();
  directional_light->temperature = 5000;
  directional_light->direction = Vec3(-1.0f, -1.0f, 0.0f);
  directional_light->illuminance = 75000.0f;
  directional_light->direction.x = -0.380f;
  directional_light->direction.y = -1.0f;
  directional_light->direction.z = -0.180f;
  directional_light->sky_diffuse     = Vec3(0.529, 0.807, 0.921);
  directional_light->sky_illuminance = 20000;

  bool done = false;
  while (!done)
  {
    if (lpp_is_valid)
    {
      if (lpp_agent.WantsReload())
      {
        dbgln("Live++ Hot Reloading...");
        wait_for_gpu_device_idle(g_GpuDevice);

        lpp_agent.CompileAndReloadChanges(lpp::LPP_RELOAD_BEHAVIOUR_WAIT_UNTIL_CHANGES_ARE_APPLIED);

        renderer_on_resize(&g_MainWindow->swap_chain);

        dbgln("Live++ Hot Reloaded Successfully!");

      }

      if (lpp_agent.WantsRestart())
      {
        dbgln("Live++ Requested Restart, Terminating...");
        lpp_agent.Restart(lpp::LPP_RESTART_BEHAVIOUR_INSTANT_TERMINATION, 0u);
      }
    }

    asset_server_update();

    reset_frame_heap();

    asset_streamer_update();

    if (g_MainWindow->needs_resize)
    {
      swap_chain_resize(&g_MainWindow->swap_chain, window, g_GpuDevice);
      renderer_on_resize(&g_MainWindow->swap_chain);
      g_MainWindow->needs_resize = false;
    }

    swap_chain_wait_latency(&g_MainWindow->swap_chain);

    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();


    u64 effective_cpu_start_time = begin_cpu_profiler_timestamp();

    const GpuTexture* back_buffer = swap_chain_acquire(&g_MainWindow->swap_chain);

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


    auto keyboard = d3d12_keyboard.GetState();
    if (keyboard.Escape)
    {
      done = true;
    }

    auto mouse = d3d12_mouse.GetState();

    if (mouse.positionMode == DirectX::Mouse::MODE_RELATIVE)
    {
      Vec2 delta = Vec2(f32(mouse.x), f32(mouse.y)) * 0.001f;
      camera->pitch -= delta.y;
      camera->yaw   += delta.x;
    }
    else if (mouse.positionMode == DirectX::Mouse::MODE_ABSOLUTE)
    {
      Vec2 pos = Vec2(f32(mouse.x), f32(mouse.y)) / Vec2((f32)g_MainWindow->swap_chain.width, (f32)g_MainWindow->swap_chain.height);
      g_Renderer.settings.mouse_pos = pos;
    }
    d3d12_mouse.SetMode(mouse.rightButton ? DirectX::Mouse::MODE_RELATIVE : DirectX::Mouse::MODE_ABSOLUTE);

    Vec3 move;
    if (keyboard.W)
    {
      move.z += 1.0f;
    }
    if (keyboard.S)
    {
      move.z -= 1.0f;
    }
    if (keyboard.D)
    {
      move.x += 1.0f;
    }
    if (keyboard.A)
    {
      move.x -= 1.0f;
    }
    if (keyboard.E)
    {
      move.y += 1.0f;
    }
    if (keyboard.Q)
    {
      move.y -= 1.0f;
    }
    // TODO(Brandon): Something is completely fucked with my quaternion math...
    Quat rot = quat_from_rotation_y(camera->yaw); // * quat_from_rotation_x(-scene.camera.pitch);  //quat_from_euler_yxz(scene.camera.yaw, 0, 0);
    move = rotate_vec3_by_quat(move, rot);
    move *= 2.0f / 60.0f;

    camera->world_pos += move;

    if (done)
      break;

    execute_render_graph(back_buffer, g_Renderer.settings);
    g_CpuEffectiveTime = end_cpu_profiler_timestamp(effective_cpu_start_time);

    swap_chain_submit(&g_MainWindow->swap_chain, g_GpuDevice, back_buffer);
  }

  lpp::LppDestroySynchronizedAgent(&lpp_agent);


  wait_for_gpu_device_idle(g_GpuDevice);
}

int APIENTRY
WinMain(HINSTANCE instance, HINSTANCE prev_instance, PSTR cmdline, int show_code)
{
  UNREFERENCED_PARAMETER(cmdline);
  UNREFERENCED_PARAMETER(prev_instance);

  u32    argc = __argc;
  char** argv = __argv; // CommandLineToArgvW(cmdline, &argc);
  for (u32 iopt = 1; iopt < argc; iopt++)
  {
    if (_stricmp(argv[iopt], "-fullscreen") == 0)
    {
      g_EnableFullscreen = true;
    }
    else if (_stricmp(argv[iopt], "-d3ddebug") == 0)
    {
      g_EnableValidationLayers = true;
    }
    else if (_stricmp(argv[iopt], "-gpu_validation") == 0)
    {
      g_EnableGpuValidation = true;
    }
  }

  set_current_thread_name(L"Athena Main");

  profiler::init();

  init_engine_memory();
  defer { destroy_engine_memory(); };

  init_thread_context();

  application_entry(instance, show_code);

  return 0;
}

