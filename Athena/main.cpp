#include "tests.h"
#include "math/math.h"
#include "graphics.h"
#include "job_system.h"
#include "threading.h"
#include "context.h"
#include "renderer.h"
#include "vendor/imgui/imgui.h"
#include "vendor/imgui/imgui_impl_win32.h"
#include "vendor/imgui/imgui_impl_dx12.h"
#include "render_graph.h"
#include "shaders/interlop.hlsli"
#include <Keyboard.h>
#include <Mouse.h>
#include "profiling.h"

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

using namespace gfx;

static void
draw_debug(RenderOptions* out_render_options,
           interlop::DirectionalLight* out_directional_light,
           Camera* out_camera)
{
  // Start the Dear ImGui frame
  ImGui_ImplDX12_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();

  bool show = true;
  ImGui::ShowDemoWindow(&show);

  ImGui::Begin("Rendering");

  if (ImGui::BeginCombo("View", GET_RENDER_BUFFER_NAME(out_render_options->debug_view)))
  {
    for (u32 i = 0; i < RenderBuffers::kCount; i++)
    {
      bool is_selected = out_render_options->debug_view == i;
      if (ImGui::Selectable(GET_RENDER_BUFFER_NAME(i), is_selected))
      {
        out_render_options->debug_view = (RenderBuffers::Entry)i;
      }
  
      if (is_selected)
      {
        ImGui::SetItemDefaultFocus();
      }
    }
  
    ImGui::EndCombo();
  }

  ImGui::DragFloat("Aperture", &out_render_options->aperture, 0.0f, 50.0f);
  ImGui::DragFloat("Focal Distance", &out_render_options->focal_dist, 0.0f, 1000.0f);
  ImGui::DragFloat("Focal Range", &out_render_options->focal_range, 0.0f, 100.0f);

  ImGui::DragFloat3("Direction", (f32*)&out_directional_light->direction, 0.1f, -1.0f, 1.0f);
  ImGui::DragFloat3("Diffuse", (f32*)&out_directional_light->diffuse, 0.1f, 0.0f, 1.0f);
  ImGui::DragFloat ("Intensity", &out_directional_light->intensity, 0.1f, 0.0f, 100.0f);

  ImGui::InputFloat3("Camera Position", (f32*)&out_camera->world_pos);

  ImGui::End();

  ImGui::Render();
}

static void
application_entry(MEMORY_ARENA_PARAM, HINSTANCE instance, int show_code, JobSystem* job_system)
{
//  SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

  WNDCLASSEXW wc = {};
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
  wc.lpfnWndProc = &window_proc;
  wc.hInstance = instance;
  wc.lpszClassName = CLASS_NAME;

  RegisterClassExW(&wc);

  RECT window_rect = {0, 0, 1920, 1080};
//#define FULLSCREEN
#ifndef FULLSCREEN
  DWORD dw_style = WS_OVERLAPPEDWINDOW; // WS_POPUP
#else
  DWORD dw_style = WS_POPUP;
#endif
  AdjustWindowRect(&window_rect, dw_style, 0);

  HWND window = CreateWindowExW(0,
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
                                0);
  ASSERT(window != nullptr);
  ShowWindow(window, show_code);
  UpdateWindow(window);

  GraphicsDevice graphics_device = init_graphics_device(MEMORY_ARENA_FWD);
  defer { destroy_graphics_device(&graphics_device); };
  SwapChain swap_chain = init_swap_chain(MEMORY_ARENA_FWD, window, &graphics_device);
  defer { destroy_swap_chain(&swap_chain); };
  init_global_upload_context(MEMORY_ARENA_FWD, &graphics_device);
  defer { destroy_global_upload_context(); };

  ShaderManager shader_manager = init_shader_manager(&graphics_device);
  defer { destroy_shader_manager(&shader_manager); };

  Renderer renderer = init_renderer(MEMORY_ARENA_FWD, &graphics_device, &swap_chain, shader_manager, window);
  defer { destroy_renderer(&renderer); };

  Scene scene       = init_scene(MEMORY_ARENA_FWD, &graphics_device);
//  SceneObject* dragon = add_scene_object(&scene, shader_manager, "assets/dragon.fbx", kVsBasic, kPsBasicNormalGloss);
//  SceneObject* dragon_scene = add_scene_object(&scene, shader_manager, "assets/dragon_scene.fbx", kVsBasic, kPsBasicNormalGloss);
//  SceneObject* cube = add_scene_object(&scene, shader_manager, "assets/cube.fbx", kVsBasic, kPsBasicNormalGloss);
//  SceneObject* cube2 = add_scene_object(&scene, shader_manager, "assets/cube2.fbx", kVsBasic, kPsBasicNormalGloss);
//  SceneObject* sponza = add_scene_object(&scene, shader_manager, "assets/sponza.fbx", kVsBasic, kPsBasicNormalGloss);
  SceneObject* sponza = add_scene_object(&scene, shader_manager, "assets/sponza/Sponza.gltf", kVsBasic, kPsBasicNormalGloss);
//  SceneObject* cornell = add_scene_object(&scene, shader_manager, "assets/cornell.fbx", kVsBasic, kPsBasicNormalGloss);

  build_acceleration_structures(&graphics_device, &scene);

  MemoryArena frame_arena = sub_alloc_memory_arena(MEMORY_ARENA_FWD, MiB(4));

  DirectX::Keyboard d3d12_keyboard;
  DirectX::Mouse d3d12_mouse;
  d3d12_mouse.SetWindow(window);

  RenderOptions render_options;

  bool done = false;
  while (!done)
  {
    reset_memory_arena(&frame_arena);

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
      scene.camera.pitch -= delta.y;
      scene.camera.yaw   += delta.x;
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
    Quat rot = quat_from_rotation_y(scene.camera.yaw); // * quat_from_rotation_x(-scene.camera.pitch);  //quat_from_euler_yxz(scene.camera.yaw, 0, 0);
    move = rotate_vec3_by_quat(move, rot);
    move *= 4.0f / 60.0f;

    scene.camera.world_pos += move;

    if (done)
      break;

    draw_debug(&render_options, &scene.directional_light, &scene.camera);

//    blocking_kick_closure_job(kJobPriorityMedium, [&]()
//    {
    begin_renderer_recording(&frame_arena, &renderer);
    submit_scene(scene, &renderer);
    execute_render(&frame_arena,
                  &renderer,
                  &graphics_device,
                  &swap_chain,
                  &scene.camera,
                  scene.vertex_uber_buffer,
                  scene.index_uber_buffer,
                  scene.bvh,
                  render_options,
                  scene.directional_light);
//    });
  }

  wait_for_device_idle(&graphics_device);
  kill_job_system(job_system);
}

int APIENTRY
WinMain(HINSTANCE instance, HINSTANCE prev_instance, PSTR cmdline, int show_code)
{
  set_current_thread_name(L"Athena Main");

  profiler::init();

//#ifdef DEBUG
//	LoadLibrary(L"C:\\Program Files\\Microsoft PIX\\2305.10\\WinPixGpuCapturer.dll");
//#endif

  init_application_memory();
  defer { destroy_application_memory(); };

  run_all_tests();

  MemoryArena arena = alloc_memory_arena(MiB(64));
  defer { free_memory_arena(&arena); };

  MemoryArena scratch_arena = sub_alloc_memory_arena(&arena, DEFAULT_SCRATCH_SIZE);
  init_context(scratch_arena);

  JobSystem* job_system = init_job_system(&arena, 512);
  Array<Thread> threads = spawn_job_system_workers(&arena, job_system);

  MemoryArena game_memory = alloc_memory_arena(GiB(1));

  application_entry(&game_memory, instance, show_code, job_system);
  join_threads(threads.memory, static_cast<u32>(threads.size));

  return 0;
}

