#pragma once
#include "types.h"
#include "math/math.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

typedef Vec4 Rgba;
typedef Vec3 Rgb;

template <typename T>
using ComPtr = Microsoft::WRL::ComPtr<T>;

static constexpr u8 FRAMES_IN_FLIGHT = 3;

struct CBuffer
{
	Mat4 projection;
	Mat4 view;
	Mat4 model;
};

struct GraphicsDevice
{
	Mat4 proj;

	ComPtr<ID3D12Device2> dev = nullptr;
	ComPtr<ID3D12CommandQueue> cmd_queue = nullptr;
	ComPtr<IDXGISwapChain4> swap_chain = nullptr;
	ComPtr<ID3D12GraphicsCommandList> cmd_list = nullptr;
	ComPtr<ID3D12CommandAllocator> cmd_allocators[FRAMES_IN_FLIGHT] = {0};

	ComPtr<ID3D12DescriptorHeap> rtv_descriptor_heap = nullptr;
	u32 rtv_descriptor_size = 0;

	ComPtr<ID3D12Resource> back_buffers[FRAMES_IN_FLIGHT] = {0};
	u32 back_buffer_index = 0;

	ComPtr<ID3D12Fence> fence = nullptr;
	u64 fence_value = 0;
	u64 frame_fence_values[FRAMES_IN_FLIGHT] = {0};
	HANDLE fence_event;

	bool vsync = true;
	bool tearing_supported = false;
	bool fullscreen = false;

	ComPtr<ID3D12Resource> depth_buffer = nullptr;
	ComPtr<ID3D12DescriptorHeap> dsv_heap  = nullptr;

	ComPtr<ID3D12Resource> vertex_buffer;
	D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view = {0};

	ComPtr<ID3D12Resource> index_buffer;
	D3D12_VERTEX_BUFFER_VIEW index_buffer_view = {0};

	ComPtr<ID3D12RootSignature> root_signature = nullptr;
	ComPtr<ID3D12PipelineState> pipeline_state = nullptr;

	D3D12_VIEWPORT viewport = {0};
	D3D12_RECT scissor_rect = {0};
};

struct Vertex
{
	Vec4 position;
	Rgba color;
};

const Vertex VERTICES[8] =
{
	{ Vec4(-1.0f, -1.0f, -1.0f, 1.0f), Rgba(0.0f, 0.0f, 0.0f, 1.0f) }, // 0
	{ Vec4(-1.0f, 1.0f, -1.0f, 1.0f), Rgba(0.0f, 1.0f, 0.0f, 1.0f) },  // 1
	{ Vec4(1.0f, 1.0f, -1.0f, 1.0f), Rgba(1.0f, 1.0f, 0.0f, 1.0f) },   // 2
	{ Vec4(1.0f, -1.0f, -1.0f, 1.0f), Rgba(1.0f, 0.0f, 0.0f, 1.0f) },  // 3
	{ Vec4(-1.0f, -1.0f, 1.0f, 1.0f), Rgba(0.0f, 0.0f, 1.0f, 1.0f) },  // 4
	{ Vec4(-1.0f, 1.0f, 1.0f, 1.0f), Rgba(0.0f, 1.0f, 1.0f, 1.0f) },   // 5
	{ Vec4( 1.0f, 1.0f, 1.0f, 1.0f), Rgba(1.0f, 1.0f, 1.0f, 1.0f) },   // 6
	{ Vec4(1.0f, -1.0f, 1.0f, 1.0f), Rgba(1.0f, 0.0f, 1.0f, 1.0f) }    // 7
};

const u16 INDICES[36] =
{
	0, 1, 2, 0, 2, 3,
	4, 6, 5, 4, 7, 6,
	4, 5, 1, 4, 1, 0,
	3, 2, 6, 3, 6, 7,
	1, 5, 6, 1, 6, 2,
	4, 0, 3, 4, 3, 7
};



GraphicsDevice init_graphics_device(HWND window);
void destroy_graphics_device(GraphicsDevice* d);

void gd_update(GraphicsDevice* d);
void gd_present(GraphicsDevice* d);

