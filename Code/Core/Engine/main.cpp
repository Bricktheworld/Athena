//#include "Core/Foundation/tests.h"
#include "Core/Foundation/math.h"
#include "Core/Foundation/threading.h"
#include "Core/Foundation/context.h"
#include "Core/Foundation/profiling.h"
#include "Core/Foundation/filesystem.h"

#include "Core/Engine/memory.h"
#include "Core/Engine/job_system.h"

#include "Core/Engine/Render/graphics.h"
#include "Core/Engine/Render/renderer.h"
#include "Core/Engine/Render/render_graph.h"

#include "Core/Engine/Vendor/imgui/imgui.h"
#include "Core/Engine/Vendor/imgui/imgui_impl_win32.h"
#include "Core/Engine/Vendor/imgui/imgui_impl_dx12.h"

#include "Core/Engine/Shaders/interlop.hlsli"

#include "Core/Engine/Vendor/DirectXTK/Mouse.h"
#include "Core/Engine/Vendor/DirectXTK/Keyboard.h"

extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 610;}
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = "."; }

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

static void
draw_debug(interlop::DirectionalLight* out_directional_light, Camera* out_camera)
{
  // Start the Dear ImGui frame
  ImGui_ImplDX12_NewFrame();
  ImGui_ImplWin32_NewFrame();
  ImGui::NewFrame();

  bool show = true;
  ImGui::ShowDemoWindow(&show);

  ImGui::Begin("Rendering");

#if 0
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
#endif

  ImGui::DragFloat3("Direction", (f32*)&out_directional_light->direction, 0.1f, -1.0f, 1.0f);
  ImGui::DragFloat3("Diffuse", (f32*)&out_directional_light->diffuse, 0.1f, 0.0f, 1.0f);
  ImGui::DragFloat ("Intensity", &out_directional_light->intensity, 0.1f, 0.0f, 100.0f);

  ImGui::InputFloat3("Camera Position", (f32*)&out_camera->world_pos);

  static Vec2 bezier_size  = Vec2(36.0f, 36.0f);
  ImGui::DragFloat2("Bezier Size", (f32*)&bezier_size, 0.2f);

  ImDrawList* draw_list = ImGui::GetWindowDrawList();
  const ImVec2 p = ImGui::GetCursorScreenPos();
  f32 x = p.x;
  f32 y = p.y;

  ImVec2 cp4[4] = { ImVec2(x, y), ImVec2(x + bezier_size.x * 0.5f, y), ImVec2(x + bezier_size.x * 0.5f, y + bezier_size.y), ImVec2(x + bezier_size.x, y + bezier_size.y) };
  draw_list->AddBezierCubic(cp4[0], cp4[1], cp4[2], cp4[3], IM_COL32_WHITE, 1.0f);
//  draw_list->PathLineTo(ImVec2(10.0f, 10.0f));
//  draw_list->PathStroke(IM_COL32(1.0f, 1.0f, 1.0f, 1.0f));

  ImGui::End();

  ImGui::Render();
}

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
//#define FULLSCREEN
#ifndef FULLSCREEN
  DWORD dw_style = WS_OVERLAPPEDWINDOW; // WS_POPUP
#else
  DWORD dw_style = WS_POPUP;
#endif
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

  GraphicsDevice graphics_device = init_graphics_device();
  defer { destroy_graphics_device(&graphics_device); };

  SwapChain swap_chain = init_swap_chain(window, &graphics_device);
  defer { destroy_swap_chain(&swap_chain); };

  init_global_upload_context(&graphics_device);
  defer { destroy_global_upload_context(); };

  ShaderManager shader_manager = init_shader_manager(&graphics_device);
  defer { destroy_shader_manager(&shader_manager); };

  init_renderer(&graphics_device, &swap_chain, shader_manager, window);
  defer { destroy_renderer(); };

  init_unified_geometry_buffer(&graphics_device);
  defer { destroy_unified_geometry_buffer(); };

  Scene scene       = init_scene(g_InitHeap, &graphics_device);

  SceneObject* sponza = nullptr;
  {
    ScratchAllocator scratch_arena = alloc_scratch_arena();
    defer { free_scratch_arena(&scratch_arena); };

    fs::FileStream sponza_built_file = open_built_asset_file(path_to_asset_id("Assets/Source/sponza/Sponza.gltf"));
//    fs::FileStream sponza_built_file = open_built_asset_file(path_to_asset_id("Assets/Source/cube.fbx"));
    defer { fs::close_file(&sponza_built_file); };

    u64 buf_size = fs::get_file_size(sponza_built_file);
    u8* buf      = HEAP_ALLOC(u8, scratch_arena, buf_size);

    ASSERT(fs::read_file(sponza_built_file, buf, buf_size));

    ModelData model;
    AssetLoadResult res = load_model(scratch_arena, buf, buf_size, &model);
    ASSERT(res == AssetLoadResult::kOk);

    sponza = add_scene_object(&scene, shader_manager, model, kVS_Basic, kPS_BasicNormalGloss);
  }

  build_acceleration_structures(&graphics_device);

  DirectX::Keyboard d3d12_keyboard;
  DirectX::Mouse d3d12_mouse;
  d3d12_mouse.SetWindow(window);

  RenderOptions render_options;

  bool done = false;
  while (!done)
  {
    reset_frame_heap();

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

    draw_debug(&scene.directional_light, &scene.camera);

    begin_renderer_recording();
    submit_scene(scene);

    const GpuTexture* back_buffer = swap_chain_acquire(&swap_chain);
    execute_render_graph(&g_Renderer.graph, &graphics_device, back_buffer, swap_chain.back_buffer_index);
    swap_chain_submit(&swap_chain, &graphics_device, back_buffer);
  }

  wait_for_device_idle(&graphics_device);
//  kill_job_system(job_system);
}

int APIENTRY
WinMain(HINSTANCE instance, HINSTANCE prev_instance, PSTR cmdline, int show_code)
{
  set_current_thread_name(L"Athena Main");

  profiler::init();

//#ifdef DEBUG
//	LoadLibrary(L"C:\\Program Files\\Microsoft PIX\\2305.10\\WinPixGpuCapturer.dll");
//#endif

  // TODO(Brandon): lol
  static constexpr size_t kHeapSize = GiB(2);

  init_engine_memory();
  defer { destroy_engine_memory(); };

  init_context(g_InitHeap, g_OverflowHeap);

  application_entry(instance, show_code);

  return 0;
}

