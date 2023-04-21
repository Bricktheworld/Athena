#include "memory/memory.h"
#include "graphics.h"
#include <windows.h>
#include <d3dcompiler.h>
#include "vendor/d3dx12.h"
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "d3dcompiler.lib")

static ComPtr<IDXGIAdapter4> get_adapter()
{
	ComPtr<IDXGIFactory4> dxgi_factory;
	u32 create_factory_flags = 0;
#ifdef DEBUG
	create_factory_flags = DXGI_CREATE_FACTORY_DEBUG;
#endif

	HASSERT(CreateDXGIFactory2(create_factory_flags, IID_PPV_ARGS(&dxgi_factory)));

	ComPtr<IDXGIAdapter1> dxgi_adapter1 = nullptr;
	ComPtr<IDXGIAdapter4> dxgi_adapter4 = nullptr;

	size_t max_dedicated_vram = 0;
	for (u32 i = 0; dxgi_factory->EnumAdapters1(i, &dxgi_adapter1) != DXGI_ERROR_NOT_FOUND; i++)
	{
		DXGI_ADAPTER_DESC1 dxgi_adapter_desc1;
		HASSERT(dxgi_adapter1->GetDesc1(&dxgi_adapter_desc1));

		if ((dxgi_adapter_desc1.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0)
			continue;
		if (FAILED(D3D12CreateDevice(dxgi_adapter1.Get(), D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)))
			continue;
		if (dxgi_adapter_desc1.DedicatedVideoMemory <= max_dedicated_vram)
			continue;
		
		max_dedicated_vram = dxgi_adapter_desc1.DedicatedVideoMemory;
		HASSERT(dxgi_adapter1.As(&dxgi_adapter4));
	}

	ASSERT(dxgi_adapter4 != nullptr);

	return dxgi_adapter4;
}

static ComPtr<ID3D12Device2> init_d3d12_device(ComPtr<IDXGIAdapter4> adapter)
{
	ComPtr<ID3D12Device2> d3d12_device = nullptr;
	HASSERT(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d12_device)));
	ASSERT(d3d12_device != nullptr);

#ifdef DEBUG
	ComPtr<ID3D12InfoQueue> info_queue = nullptr;
	HASSERT(d3d12_device.As(&info_queue));
	ASSERT(info_queue != nullptr);
	info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
	info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
	info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);
#endif
	return d3d12_device;
}

static bool check_tearing_support()
{
	BOOL allow_tearing = FALSE;

	ComPtr<IDXGIFactory4> factory4 = nullptr;
	HASSERT(CreateDXGIFactory1(IID_PPV_ARGS(&factory4)));

	ComPtr<IDXGIFactory5> factory5 = nullptr;
	if (FAILED(factory4.As(&factory5)))
		return false;

	if (FAILED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow_tearing, sizeof(allow_tearing))))
		return false;

	return allow_tearing == TRUE;
}

static ComPtr<IDXGISwapChain4> init_swap_chain(HWND window,
                                                 ComPtr<ID3D12CommandQueue> command_queue,
                                                 u32 width,
                                                 u32 height,
                                                 u32 buffer_count)
{
	ComPtr<IDXGISwapChain4> dxgi_swap_chain4 = nullptr;
	ComPtr<IDXGIFactory4> dxgi_factory4 = nullptr;
	u32 create_factory_flags = 0;
#ifdef DEBUG
	create_factory_flags = DXGI_CREATE_FACTORY_DEBUG;
#endif

	HASSERT(CreateDXGIFactory2(create_factory_flags, IID_PPV_ARGS(&dxgi_factory4)));

	DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = { 0 };
	swap_chain_desc.Width = width;
	swap_chain_desc.Height = height;
	swap_chain_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swap_chain_desc.Stereo = FALSE;
	swap_chain_desc.SampleDesc = { 1, 0 };
	swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swap_chain_desc.BufferCount = buffer_count;
	swap_chain_desc.Scaling = DXGI_SCALING_STRETCH;
	swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swap_chain_desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	swap_chain_desc.Flags = check_tearing_support() ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

	ComPtr<IDXGISwapChain1> swap_chain1 = nullptr;
	HASSERT(dxgi_factory4->CreateSwapChainForHwnd(command_queue.Get(),
	                                              window,
	                                              &swap_chain_desc,
	                                              nullptr,
	                                              nullptr,
	                                              &swap_chain1));

	HASSERT(dxgi_factory4->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER));
	HASSERT(swap_chain1.As(&dxgi_swap_chain4));
	ASSERT(dxgi_swap_chain4 != nullptr);

	return dxgi_swap_chain4;
}

static ComPtr<ID3D12DescriptorHeap> init_descriptor_heap(ComPtr<ID3D12Device2> d3d12_dev, 
                                                           D3D12_DESCRIPTOR_HEAP_TYPE type,
                                                           u32 count)
{
	ComPtr<ID3D12DescriptorHeap> descriptor_heap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC desc;
	desc.NumDescriptors = count;
	desc.Type = type;
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	desc.NodeMask = 0;

	HASSERT(d3d12_dev->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptor_heap)));
	ASSERT(descriptor_heap != nullptr);

	return descriptor_heap;
}

static void update_render_target_views(ComPtr<ID3D12Device2> d3d12_dev,
                                       ComPtr<IDXGISwapChain4> swap_chain,
                                       ComPtr<ID3D12DescriptorHeap> descriptor_heap,
                                       ComPtr<ID3D12Resource>* out_back_buffers)
{
	auto rtv_descriptor_size = d3d12_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(descriptor_heap->GetCPUDescriptorHandleForHeapStart());

	for (u8 i = 0; i < FRAMES_IN_FLIGHT; i++)
	{
		ComPtr<ID3D12Resource> back_buffer = nullptr;
		HASSERT(swap_chain->GetBuffer(i, IID_PPV_ARGS(&back_buffer)));
		d3d12_dev->CreateRenderTargetView(back_buffer.Get(), nullptr, rtv_handle);

		out_back_buffers[i] = back_buffer;

		rtv_handle.Offset(rtv_descriptor_size);
	}
}

static ComPtr<ID3D12CommandQueue> init_cmd_queue(ComPtr<ID3D12Device2> d3d12_dev,
                                                 D3D12_COMMAND_LIST_TYPE type)
{
	ComPtr<ID3D12CommandQueue> cmd_queue = nullptr;

	D3D12_COMMAND_QUEUE_DESC desc = { };
	desc.Type = type;
	desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	desc.NodeMask = 0;

	HASSERT(d3d12_dev->CreateCommandQueue(&desc, IID_PPV_ARGS(&cmd_queue)));
	ASSERT(cmd_queue != nullptr);

	return cmd_queue;
}

static ComPtr<ID3D12CommandAllocator> init_cmd_allocator(ComPtr<ID3D12Device2> d3d12_dev,
                                                         D3D12_COMMAND_LIST_TYPE type)
{
	ComPtr<ID3D12CommandAllocator> cmd_allocator = nullptr;
	HASSERT(d3d12_dev->CreateCommandAllocator(type, IID_PPV_ARGS(&cmd_allocator)));
	ASSERT(cmd_allocator != nullptr);

	return cmd_allocator;
}

static ComPtr<ID3D12GraphicsCommandList> init_cmd_list(ComPtr<ID3D12Device2> d3d12_dev, 
                                                       ComPtr<ID3D12CommandAllocator> cmd_allocator,
                                                       D3D12_COMMAND_LIST_TYPE type)
{
	ComPtr<ID3D12GraphicsCommandList> cmd_list = nullptr;
	HASSERT(d3d12_dev->CreateCommandList(0, type, cmd_allocator.Get(), nullptr, IID_PPV_ARGS(&cmd_list)));
	ASSERT(cmd_list != nullptr);

	return cmd_list;
}

static ComPtr<ID3D12Fence> init_fence(ComPtr<ID3D12Device2> d3d12_dev)
{
	ComPtr<ID3D12Fence> fence = nullptr;
	HASSERT(d3d12_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
	ASSERT(fence != nullptr);

	return fence;
}

static HANDLE init_event_handle()
{
	HANDLE fence_event = CreateEventW(NULL, FALSE, FALSE, NULL);
	ASSERT(fence_event);

	return fence_event;
}

static u64 signal(ComPtr<ID3D12CommandQueue> cmd_queue,
                  ComPtr<ID3D12Fence> fence,
                  u64* fence_value)
{
	u64 fence_value_for_signal = ++(*fence_value);
	HASSERT(cmd_queue->Signal(fence.Get(), fence_value_for_signal));

	return fence_value_for_signal;
}

static void wait_for_fence_value(ComPtr<ID3D12Fence> fence,
                                 u64 fence_value,
                                 HANDLE fence_event,
                                 u64 max_duration_ms = -1)
{
	if (fence->GetCompletedValue() < fence_value)
	{
		HASSERT(fence->SetEventOnCompletion(fence_value, fence_event));
		WaitForSingleObject(fence_event, static_cast<DWORD>(max_duration_ms));
	}
}

static void wait_for_device_idle(ComPtr<ID3D12CommandQueue> cmd_queue,
                             ComPtr<ID3D12Fence> fence,
                             u64* fence_value,
                             HANDLE fence_event)
{
	u64 fence_value_for_signal = signal(cmd_queue, fence, fence_value);
	wait_for_fence_value(fence, fence_value_for_signal, fence_event);
}

static void update_buffer_resource(ComPtr<ID3D12Device2> d3d12_dev,
                                   ComPtr<ID3D12GraphicsCommandList> cmd_list, 
                                   ID3D12Resource** dst,
                                   ID3D12Resource** scratch,
                                   size_t size,
                                   const void* src,
                                   D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE)
{
	auto props = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
	auto buffer = CD3DX12_RESOURCE_DESC::Buffer(size, flags);
	HASSERT(d3d12_dev->CreateCommittedResource(&props,
	                                           D3D12_HEAP_FLAG_NONE,
	                                           &buffer,
	                                           D3D12_RESOURCE_STATE_COPY_DEST,
	                                           nullptr,
	                                           IID_PPV_ARGS(dst)));

	if (src == nullptr)
		return;

	props = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	buffer = CD3DX12_RESOURCE_DESC::Buffer(size);
	HASSERT(d3d12_dev->CreateCommittedResource(&props,
	                                           D3D12_HEAP_FLAG_NONE,
	                                           &buffer,
	                                           D3D12_RESOURCE_STATE_GENERIC_READ,
	                                           nullptr,
	                                           IID_PPV_ARGS(scratch)));
	D3D12_SUBRESOURCE_DATA subresource_data = {};
	subresource_data.pData = src;
	subresource_data.RowPitch = size;
	subresource_data.SlicePitch  = size;

	UpdateSubresources(cmd_list.Get(), *dst, *scratch, 0, 0, 1, &subresource_data);
}

GraphicsDevice init_graphics_device(HWND window)
{
#ifdef DEBUG
	ComPtr<ID3D12Debug> debug_interface = nullptr;
	HASSERT(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_interface)));
	debug_interface->EnableDebugLayer();
#endif

	RECT client_rect;
	GetClientRect(window, &client_rect);
	u32 client_width = client_rect.right - client_rect.left;
	u32 client_height = client_rect.bottom - client_rect.top;

	GraphicsDevice res;
	auto adapter = get_adapter();
	res.tearing_supported = check_tearing_support();
	res.dev = init_d3d12_device(adapter);

	res.cmd_queue = init_cmd_queue(res.dev, D3D12_COMMAND_LIST_TYPE_DIRECT);
	res.swap_chain = init_swap_chain(window, res.cmd_queue, client_width, client_height, FRAMES_IN_FLIGHT);

	res.back_buffer_index = res.swap_chain->GetCurrentBackBufferIndex();

	res.rtv_descriptor_heap = init_descriptor_heap(res.dev, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, FRAMES_IN_FLIGHT);
	res.rtv_descriptor_size = res.dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	update_render_target_views(res.dev, res.swap_chain, res.rtv_descriptor_heap, res.back_buffers);

	for (u8 i = 0; i < FRAMES_IN_FLIGHT; i++)
	{
		res.cmd_allocators[i] = init_cmd_allocator(res.dev, D3D12_COMMAND_LIST_TYPE_DIRECT);
	}

	res.cmd_list = init_cmd_list(res.dev, res.cmd_allocators[res.back_buffer_index], D3D12_COMMAND_LIST_TYPE_DIRECT);
	res.cmd_list->Close();

	res.fence = init_fence(res.dev);
	res.fence_event = init_event_handle();

	res.scissor_rect = CD3DX12_RECT(0, 0, LONG_MAX, LONG_MAX);
	res.viewport = CD3DX12_VIEWPORT(0.0f, 0.0f, static_cast<f32>(client_width), static_cast<f32>(client_height));

	res.proj = perspective_infinite_reverse_lh(PI / 4.0f,
	                                           static_cast<f32>(client_width) / static_cast<f32>(client_height),
	                                           0.1f);

//	ComPtr<ID3D12Resource> intermediate_vertex_buffer;
//	update_buffer_resource(res.dev,
//	                       res.cmd_list.Get(),
//	                       &res.vertex_buffer,
//	                       &intermediate_vertex_buffer,
//	                       sizeof(VERTICES),
//	                       VERTICES);

	return res;
}

void destroy_graphics_device(GraphicsDevice* d)
{
	wait_for_device_idle(d->cmd_queue, d->fence, &d->fence_value, d->fence_event);

	zero_memory(d, sizeof(GraphicsDevice));
}

void gd_update(GraphicsDevice* d)
{
	wait_for_fence_value(d->fence, d->frame_fence_values[d->back_buffer_index], d->fence_event);

	auto cmd_allocator = d->cmd_allocators[d->back_buffer_index];
	auto back_buffer = d->back_buffers[d->back_buffer_index];

	cmd_allocator->Reset();
	d->cmd_list->Reset(cmd_allocator.Get(), nullptr);

	{
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(back_buffer.Get(),
		                                                                        D3D12_RESOURCE_STATE_PRESENT,
		                                                                        D3D12_RESOURCE_STATE_RENDER_TARGET);

		d->cmd_list->ResourceBarrier(1, &barrier);
	}

	{
		Rgba clear_color(0.4f, 0.6f, 0.9f, 1.0f);
		CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(d->rtv_descriptor_heap->GetCPUDescriptorHandleForHeapStart(),
										d->back_buffer_index,
										d->rtv_descriptor_size);
	
		d->cmd_list->ClearRenderTargetView(rtv, (f32*)&clear_color, 0, nullptr);
	}

}

void gd_present(GraphicsDevice* d)
{
	auto back_buffer = d->back_buffers[d->back_buffer_index];
	{
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(back_buffer.Get(),
		                                                                        D3D12_RESOURCE_STATE_RENDER_TARGET,
		                                                                        D3D12_RESOURCE_STATE_PRESENT);
		d->cmd_list->ResourceBarrier(1, &barrier);
	}

	HASSERT(d->cmd_list->Close());

	ID3D12CommandList* const cmd_list[] =
	{
		d->cmd_list.Get()
	};

	d->cmd_queue->ExecuteCommandLists(ARRAY_LENGTH(cmd_list), cmd_list);

	d->frame_fence_values[d->back_buffer_index] = signal(d->cmd_queue, d->fence, &d->fence_value);

	u32 sync_interval = d->vsync ? 1 : 0;
	u32 present_flags = d->tearing_supported && !d->vsync ? DXGI_PRESENT_ALLOW_TEARING : 0;
	HASSERT(d->swap_chain->Present(sync_interval, present_flags));

	d->back_buffer_index = d->swap_chain->GetCurrentBackBufferIndex();
}
