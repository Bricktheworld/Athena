#include "memory/memory.h"
#include "graphics.h"
#include <windows.h>
#include <d3dcompiler.h>
#include "vendor/d3dx12.h"
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "d3dcompiler.lib")

static IDXGIFactory4*
init_factory()
{
	IDXGIFactory4* factory = nullptr;
	u32 create_factory_flags = 0;
#ifdef DEBUG
	create_factory_flags = DXGI_CREATE_FACTORY_DEBUG;
#endif

	HASSERT(CreateDXGIFactory2(create_factory_flags, IID_PPV_ARGS(&factory)));
	ASSERT(factory != nullptr);

	return factory;
}

static IDXGIAdapter1*
init_adapter(IDXGIFactory4* factory)
{
	IDXGIAdapter1* res = nullptr;

	size_t max_dedicated_vram = 0;
	IDXGIAdapter1* current = nullptr;
	for (u32 i = 0; factory->EnumAdapters1(i, &current) != DXGI_ERROR_NOT_FOUND; i++)
	{
		DXGI_ADAPTER_DESC1 dxgi_adapter_desc = {0};
		HASSERT(current->GetDesc1(&dxgi_adapter_desc));

		if ((dxgi_adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0 || 
			FAILED(D3D12CreateDevice(current, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr)) ||
			dxgi_adapter_desc.DedicatedVideoMemory <= max_dedicated_vram)
		{
			COM_RELEASE(current);
			continue;
		}
		
		max_dedicated_vram = dxgi_adapter_desc.DedicatedVideoMemory;
		COM_RELEASE(res);
		res = current;
		current = nullptr;
	}

	ASSERT(res != nullptr);
	return res;
}

static ID3D12Device2*
init_d3d12_device(IDXGIAdapter1* adapter)
{
	ID3D12Device2* d3d12_device = nullptr;
	HASSERT(D3D12CreateDevice(adapter, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&d3d12_device)));
	ASSERT(d3d12_device != nullptr);

#ifdef DEBUG
	ID3D12InfoQueue* info_queue = nullptr;
	HASSERT(d3d12_device->QueryInterface(IID_PPV_ARGS(&info_queue)));
	ASSERT(info_queue != nullptr);
	info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, TRUE);
	info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, TRUE);
	info_queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_WARNING, TRUE);
#endif
	return d3d12_device;
}

static bool
check_tearing_support(IDXGIFactory4* factory4)
{
	BOOL allow_tearing = FALSE;

	IDXGIFactory5* factory5 = nullptr;
	if (FAILED(factory4->QueryInterface(IID_PPV_ARGS(&factory5))))
		return false;

	if (FAILED(factory5->CheckFeatureSupport(DXGI_FEATURE_PRESENT_ALLOW_TEARING, &allow_tearing, sizeof(allow_tearing))))
		return false;

	COM_RELEASE(factory5);

	return allow_tearing == TRUE;
}

static IDXGISwapChain4*
init_swap_chain(HWND window,
                                        IDXGIFactory4* factory,
                                        ID3D12CommandQueue* command_queue,
                                        u32 width,
                                        u32 height,
                                        u32 buffer_count)
{
	IDXGISwapChain4* dxgi_swap_chain4 = nullptr;

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
	swap_chain_desc.Flags = check_tearing_support(factory) ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

	IDXGISwapChain1* swap_chain1 = nullptr;
	HASSERT(factory->CreateSwapChainForHwnd(command_queue,
	                                        window,
	                                        &swap_chain_desc,
	                                        nullptr,
	                                        nullptr,
	                                        &swap_chain1));

	HASSERT(factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER));
	HASSERT(swap_chain1->QueryInterface(IID_PPV_ARGS(&dxgi_swap_chain4)));
	COM_RELEASE(swap_chain1);

	ASSERT(dxgi_swap_chain4 != nullptr);

	return dxgi_swap_chain4;
}

static ID3D12DescriptorHeap*
init_descriptor_heap(ID3D12Device2* d3d12_dev, 
                                                  D3D12_DESCRIPTOR_HEAP_TYPE type,
                                                  u32 count)
{
	ID3D12DescriptorHeap* descriptor_heap = nullptr;
	D3D12_DESCRIPTOR_HEAP_DESC desc;
	desc.NumDescriptors = count;
	desc.Type = type;
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	desc.NodeMask = 0;

	HASSERT(d3d12_dev->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&descriptor_heap)));
	ASSERT(descriptor_heap != nullptr);

	return descriptor_heap;
}

static void
update_render_target_views(ID3D12Device2* d3d12_dev,
                                       IDXGISwapChain4* swap_chain,
                                       ID3D12DescriptorHeap* descriptor_heap,
                                       ID3D12Resource** out_back_buffers)
{
	auto rtv_descriptor_size = d3d12_dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv_handle(descriptor_heap->GetCPUDescriptorHandleForHeapStart());

	for (u8 i = 0; i < FRAMES_IN_FLIGHT; i++)
	{
		ID3D12Resource* back_buffer = nullptr;
		HASSERT(swap_chain->GetBuffer(i, IID_PPV_ARGS(&back_buffer)));
		d3d12_dev->CreateRenderTargetView(back_buffer, nullptr, rtv_handle);

		out_back_buffers[i] = back_buffer;

		rtv_handle.Offset(rtv_descriptor_size);
	}
}

static ID3D12CommandQueue*
init_cmd_queue(ID3D12Device2* d3d12_dev,
                                          D3D12_COMMAND_LIST_TYPE type)
{
	ID3D12CommandQueue* cmd_queue = nullptr;

	D3D12_COMMAND_QUEUE_DESC desc = { };
	desc.Type = type;
	desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	desc.NodeMask = 0;

	HASSERT(d3d12_dev->CreateCommandQueue(&desc, IID_PPV_ARGS(&cmd_queue)));
	ASSERT(cmd_queue != nullptr);

	return cmd_queue;
}

static ID3D12CommandAllocator*
init_cmd_allocator(ID3D12Device2* d3d12_dev,
                                                  D3D12_COMMAND_LIST_TYPE type)
{
	ID3D12CommandAllocator* cmd_allocator = nullptr;
	HASSERT(d3d12_dev->CreateCommandAllocator(type, IID_PPV_ARGS(&cmd_allocator)));
	ASSERT(cmd_allocator != nullptr);

	return cmd_allocator;
}

static ID3D12GraphicsCommandList*
init_cmd_list(ID3D12Device2* d3d12_dev, 
                                                ID3D12CommandAllocator* cmd_allocator,
                                                D3D12_COMMAND_LIST_TYPE type)
{
	ID3D12GraphicsCommandList* cmd_list = nullptr;
	HASSERT(d3d12_dev->CreateCommandList(0, type, cmd_allocator, nullptr, IID_PPV_ARGS(&cmd_list)));
	ASSERT(cmd_list != nullptr);

	return cmd_list;
}

static ID3D12Fence* 
init_fence(ID3D12Device2* d3d12_dev)
{
	ID3D12Fence* fence = nullptr;
	HASSERT(d3d12_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
	ASSERT(fence != nullptr);

	return fence;
}

static HANDLE
init_event_handle()
{
	HANDLE fence_event = CreateEventW(NULL, FALSE, FALSE, NULL);
	ASSERT(fence_event);

	return fence_event;
}

static u64
signal(ID3D12CommandQueue* cmd_queue,
                  ID3D12Fence* fence,
                  u64* fence_value)
{
	u64 fence_value_for_signal = ++(*fence_value);
	HASSERT(cmd_queue->Signal(fence, fence_value_for_signal));

	return fence_value_for_signal;
}

static void
wait_for_fence_value(ID3D12Fence* fence,
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

static void
wait_for_device_idle(ID3D12CommandQueue* cmd_queue,
                                 ID3D12Fence* fence,
                                 u64* fence_value,
                                 HANDLE fence_event)
{
	u64 fence_value_for_signal = signal(cmd_queue, fence, fence_value);
	wait_for_fence_value(fence, fence_value_for_signal, fence_event);
}

static void
update_buffer_resource(ID3D12Device2* d3d12_dev,
                                   ID3D12GraphicsCommandList* cmd_list, 
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

	UpdateSubresources(cmd_list, *dst, *scratch, 0, 0, 1, &subresource_data);
}

UploadBuffer
alloc_upload_buffer(GraphicsDevice* d, size_t size)
{
	D3D12_HEAP_PROPERTIES heap_props = {};
	heap_props.Type = D3D12_HEAP_TYPE_UPLOAD;
	heap_props.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heap_props.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heap_props.CreationNodeMask = 1;
	heap_props.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC resource_desc = {};
	resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resource_desc.Width = size;
	resource_desc.Height = 1;
	resource_desc.DepthOrArraySize = 1;
	resource_desc.MipLevels = 1;
	resource_desc.Format = DXGI_FORMAT_UNKNOWN;
	resource_desc.SampleDesc.Count = 1;
	resource_desc.SampleDesc.Quality = 0;
	resource_desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	resource_desc.Flags = D3D12_RESOURCE_FLAG_NONE;
	resource_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;

	ID3D12Resource* resource = nullptr;
	HASSERT(d->dev->CreateCommittedResource(&heap_props,
	                                        D3D12_HEAP_FLAG_NONE,
	                                        &resource_desc,
	                                        D3D12_RESOURCE_STATE_GENERIC_READ,
	                                        nullptr,
	                                        IID_PPV_ARGS(&resource)));

	void* mapped = nullptr;
	auto range = CD3DX12_RANGE(0, size);
	HASSERT(resource->Map(0, &range, &mapped));

	UploadBuffer res = {0};
	res.resource = resource;
	res.gpu_addr = resource->GetGPUVirtualAddress();
	res.mapped = mapped;

	return res;
}

void
free_upload_buffer(UploadBuffer* upload_buffer)
{
	COM_RELEASE(upload_buffer->resource);
	zero_memory(&upload_buffer, sizeof(UploadBuffer));
}

static CmdAllocatorPool 
init_cmd_allocator_pool(MEMORY_ARENA_PARAM,
                        ID3D12Device2* d3d12_dev,
                        D3D12_COMMAND_LIST_TYPE type,
                        u32 size)
{
	CmdAllocatorPool ret = {0};
	ret.free_allocators = push_memory_arena<ID3D12CommandAllocator*>(MEMORY_ARENA_FWD, size);

	for (u32 i = 0; i < size; i++)
	{
		ret.free_allocators[i] = init_cmd_allocator(d3d12_dev, type);
	}

	ret.free_allocator_count = size;

	ret.submitted_allocators = push_memory_arena<ID3D12CommandAllocator*>(MEMORY_ARENA_FWD, size);
	zero_memory(ret.submitted_allocators, sizeof(ID3D12CommandAllocator*) * size);

	ret.submitted_allocator_count = 0;

	return ret;
}

static void
destroy_cmd_allocator_pool(CmdAllocatorPool* cmd_allocator_pool)
{
	for (u32 i = 0; i < cmd_allocator_pool->free_allocator_count; i++)
	{
		COM_RELEASE(cmd_allocator_pool->free_allocators[i]);
	}

	for (u32 i = 0; i < cmd_allocator_pool->submitted_allocator_count; i++)
	{
		COM_RELEASE(cmd_allocator_pool->submitted_allocators[i]);
	}
	zero_memory(cmd_allocator_pool, sizeof(CmdAllocatorPool));
}

GraphicsDevice
init_graphics_device(HWND window)
{
#ifdef DEBUG
	ID3D12Debug* debug_interface = nullptr;
	HASSERT(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_interface)));
	debug_interface->EnableDebugLayer();
	defer { COM_RELEASE(debug_interface); };
#endif

	RECT client_rect;
	GetClientRect(window, &client_rect);
	u32 client_width = client_rect.right - client_rect.left;
	u32 client_height = client_rect.bottom - client_rect.top;

	GraphicsDevice res;
	auto* factory = init_factory();
	defer { COM_RELEASE(factory); };

	auto* adapter = init_adapter(factory);
	defer { COM_RELEASE(adapter); };

	res.tearing_supported = check_tearing_support(factory);
	res.dev = init_d3d12_device(adapter);

	res.cmd_queue = init_cmd_queue(res.dev, D3D12_COMMAND_LIST_TYPE_DIRECT);
	res.swap_chain = init_swap_chain(window, factory, res.cmd_queue, client_width, client_height, FRAMES_IN_FLIGHT);

	res.back_buffer_index = res.swap_chain->GetCurrentBackBufferIndex();

	res.rtv_descriptor_heap = init_descriptor_heap(res.dev, D3D12_DESCRIPTOR_HEAP_TYPE_RTV, FRAMES_IN_FLIGHT);
	res.rtv_descriptor_size = res.dev->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

	res.dsv_heap = init_descriptor_heap(res.dev, D3D12_DESCRIPTOR_HEAP_TYPE_DSV, 1);

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

	{
		UploadBuffer vertex_upload_buffer = alloc_upload_buffer(&res, sizeof(VERTICES));
		memcpy(vertex_upload_buffer.mapped, VERTICES, sizeof(VERTICES));
	
		res.vertex_buffer = vertex_upload_buffer.resource;
		res.vertex_buffer_view.BufferLocation = res.vertex_buffer->GetGPUVirtualAddress();
		res.vertex_buffer_view.SizeInBytes = sizeof(VERTICES);
		res.vertex_buffer_view.StrideInBytes = sizeof(Vertex);
	
		UploadBuffer index_upload_buffer = alloc_upload_buffer(&res, sizeof(INDICES));
		memcpy(index_upload_buffer.mapped, INDICES, sizeof(INDICES));
	
		res.index_buffer = index_upload_buffer.resource;
		res.index_buffer_view.BufferLocation = res.index_buffer->GetGPUVirtualAddress();
		res.index_buffer_view.Format = DXGI_FORMAT_R16_UINT;
		res.index_buffer_view.SizeInBytes = sizeof(INDICES);
	}

	{
		D3D12_FEATURE_DATA_ROOT_SIGNATURE feature_data = {0};
		feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_1;
		if (FAILED(res.dev->CheckFeatureSupport(D3D12_FEATURE_ROOT_SIGNATURE, &feature_data, sizeof(feature_data))))
		{
			feature_data.HighestVersion = D3D_ROOT_SIGNATURE_VERSION_1_0;
		}
	
		D3D12_ROOT_SIGNATURE_FLAGS flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT |
										D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS |
										D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS |
										D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS |
										D3D12_ROOT_SIGNATURE_FLAG_DENY_PIXEL_SHADER_ROOT_ACCESS;
	
		CD3DX12_ROOT_PARAMETER1 root_params[1];
		root_params[0].InitAsConstants(sizeof(Mat4) / 4, 0, 0, D3D12_SHADER_VISIBILITY_VERTEX);
	
		CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC root_signature_desc;
		root_signature_desc.Init_1_1(ARRAY_LENGTH(root_params), root_params, 0, nullptr, flags);
	
		ID3DBlob* root_signature_blob = nullptr;
		defer { COM_RELEASE(root_signature_blob); };

		ID3DBlob* error_blob = nullptr;
		defer { COM_RELEASE(error_blob); };

		HASSERT(D3DX12SerializeVersionedRootSignature(&root_signature_desc,
													feature_data.HighestVersion,
													&root_signature_blob,
													&error_blob));
		HASSERT(res.dev->CreateRootSignature(0, root_signature_blob->GetBufferPointer(), root_signature_blob->GetBufferSize(), IID_PPV_ARGS(&res.root_signature)));
	}

	{
		ID3DBlob* vs_blob = nullptr;
		defer { COM_RELEASE(vs_blob); };
		HASSERT(D3DReadFileToBlob(L"test_vs_d.cso", &vs_blob));

		ID3DBlob* ps_blob = nullptr;
		defer { COM_RELEASE(ps_blob); };
		HASSERT(D3DReadFileToBlob(L"test_ps_d.cso", &ps_blob));

		D3D12_INPUT_ELEMENT_DESC input_layout[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
			{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		};

		struct PipelineStateStream
		{
			CD3DX12_PIPELINE_STATE_STREAM_ROOT_SIGNATURE root_signature;
			CD3DX12_PIPELINE_STATE_STREAM_INPUT_LAYOUT input_layout;
			CD3DX12_PIPELINE_STATE_STREAM_PRIMITIVE_TOPOLOGY primitive_topology_type;
			CD3DX12_PIPELINE_STATE_STREAM_VS vs;
			CD3DX12_PIPELINE_STATE_STREAM_PS ps;
			CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL_FORMAT dsv_format;
			CD3DX12_PIPELINE_STATE_STREAM_DEPTH_STENCIL1 depth_state;
			CD3DX12_PIPELINE_STATE_STREAM_RENDER_TARGET_FORMATS rtv_formats;
		};
		PipelineStateStream pipeline_state_stream = {0};

		D3D12_RT_FORMAT_ARRAY rtv_formats = {0};
		rtv_formats.NumRenderTargets = 1;
		rtv_formats.RTFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;

		pipeline_state_stream.root_signature = res.root_signature;
		pipeline_state_stream.input_layout = { input_layout, ARRAY_LENGTH(input_layout) };
		pipeline_state_stream.primitive_topology_type = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
		pipeline_state_stream.vs = CD3DX12_SHADER_BYTECODE(vs_blob);
		pipeline_state_stream.ps = CD3DX12_SHADER_BYTECODE(ps_blob);
		pipeline_state_stream.dsv_format = DXGI_FORMAT_D32_FLOAT;
		CD3DX12_DEPTH_STENCIL_DESC1 depth_stencil_desc;
		depth_stencil_desc.DepthEnable = TRUE;
		depth_stencil_desc.DepthFunc = D3D12_COMPARISON_FUNC_GREATER;
		depth_stencil_desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
		depth_stencil_desc.StencilEnable = FALSE;
		pipeline_state_stream.depth_state = depth_stencil_desc;

		pipeline_state_stream.rtv_formats = rtv_formats;

		D3D12_PIPELINE_STATE_STREAM_DESC pipeline_state_stream_desc = {sizeof(pipeline_state_stream), &pipeline_state_stream};
		HASSERT(res.dev->CreatePipelineState(&pipeline_state_stream_desc, IID_PPV_ARGS(&res.pipeline_state)));
	}

	{
		D3D12_CLEAR_VALUE clear_value = {};
		clear_value.Format = DXGI_FORMAT_D32_FLOAT;
		clear_value.DepthStencil = { 0.0f, 0 };

		auto props = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
		auto desc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_D32_FLOAT,
		                                         client_width,
		                                         client_height,
		                                         1, 0, 1, 0,
		                                         D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL);

		HASSERT(res.dev->CreateCommittedResource(&props,
		                                         D3D12_HEAP_FLAG_NONE,
		                                         &desc,
		                                         D3D12_RESOURCE_STATE_DEPTH_WRITE,
		                                         &clear_value, 
		                                         IID_PPV_ARGS(&res.depth_buffer)));

		D3D12_DEPTH_STENCIL_VIEW_DESC view_desc = {0};
		view_desc.Format = DXGI_FORMAT_D32_FLOAT;
		view_desc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;
		view_desc.Texture2D.MipSlice = 0;
		view_desc.Flags = D3D12_DSV_FLAG_NONE;

		res.dev->CreateDepthStencilView(res.depth_buffer, &view_desc, res.dsv_heap->GetCPUDescriptorHandleForHeapStart());
	}

	return res;
}

void
destroy_graphics_device(GraphicsDevice* d)
{
	wait_for_device_idle(d->cmd_queue, d->fence, &d->fence_value, d->fence_event);

	COM_RELEASE(d->pipeline_state);
	COM_RELEASE(d->root_signature);

	COM_RELEASE(d->index_buffer);
	COM_RELEASE(d->vertex_buffer);
	COM_RELEASE(d->dsv_heap);
	COM_RELEASE(d->depth_buffer);

	COM_RELEASE(d->fence);

	COM_RELEASE(d->rtv_descriptor_heap);
	COM_RELEASE(d->cmd_list);

	for (u8 i = 0; i < FRAMES_IN_FLIGHT; i++)
	{
		COM_RELEASE(d->back_buffers[i]);
		COM_RELEASE(d->cmd_allocators[i]);
	}

	COM_RELEASE(d->swap_chain);
	COM_RELEASE(d->cmd_queue);
	COM_RELEASE(d->dev);

	zero_memory(d, sizeof(GraphicsDevice));
}

UploadContext
init_upload_context(MEMORY_ARENA_PARAM, GraphicsDevice* d, u32 max_allocators)
{
	UploadContext ret = {0};
	ret.dev = d->dev;
	ret.cmd_allocator_pool = init_cmd_allocator_pool(MEMORY_ARENA_FWD, ret.dev, D3D12_COMMAND_LIST_TYPE_DIRECT, max_allocators);
	return ret;
}

void
destroy_upload_context(UploadContext* upload_context)
{
	destroy_cmd_allocator_pool(&upload_context->cmd_allocator_pool);
	zero_memory(upload_context, sizeof(UploadContext));
}

void
gd_update(GraphicsDevice* d)
{
	wait_for_fence_value(d->fence, d->frame_fence_values[d->back_buffer_index], d->fence_event);

	auto cmd_allocator = d->cmd_allocators[d->back_buffer_index];
	auto back_buffer = d->back_buffers[d->back_buffer_index];

	cmd_allocator->Reset();
	d->cmd_list->Reset(cmd_allocator, nullptr);

	{
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(back_buffer,
		                                                                        D3D12_RESOURCE_STATE_PRESENT,
		                                                                        D3D12_RESOURCE_STATE_RENDER_TARGET);

		d->cmd_list->ResourceBarrier(1, &barrier);
	}

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtv(d->rtv_descriptor_heap->GetCPUDescriptorHandleForHeapStart(),
									d->back_buffer_index,
									d->rtv_descriptor_size);
	CD3DX12_CPU_DESCRIPTOR_HANDLE dsv(d->dsv_heap->GetCPUDescriptorHandleForHeapStart());
	{
		Rgba clear_color(0.4f, 0.6f, 0.9f, 1.0f);
	
		d->cmd_list->ClearRenderTargetView(rtv, (f32*)&clear_color, 0, nullptr);

		d->cmd_list->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 0.0f, 0, 0, nullptr);
	}
	{
		d->cmd_list->SetPipelineState(d->pipeline_state);
		d->cmd_list->SetGraphicsRootSignature(d->root_signature);
		d->cmd_list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		d->cmd_list->IASetVertexBuffers(0, 1, &d->vertex_buffer_view);
		d->cmd_list->IASetIndexBuffer(&d->index_buffer_view);
	}
	{
		d->cmd_list->RSSetViewports(1, &d->viewport);
		d->cmd_list->RSSetScissorRects(1, &d->scissor_rect);
		d->cmd_list->OMSetRenderTargets(1, &rtv, FALSE, &dsv);

		Mat4 view = look_at_lh(Vec3(0.0, 0.0, -10.0), Vec3(0.0, 0.0, 1.0), Vec3(0.0, 1.0, 0.0));
		Mat4 model = {};
		Mat4 mvp = d->proj * view * model;

		d->cmd_list->SetGraphicsRoot32BitConstants(0, sizeof(Mat4) / 4, &mvp, 0);
		d->cmd_list->DrawIndexedInstanced(ARRAY_LENGTH(INDICES), 1, 0, 0, 0);
	}

}

void
gd_present(GraphicsDevice* d)
{
	auto back_buffer = d->back_buffers[d->back_buffer_index];
	{
		CD3DX12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(back_buffer,
		                                                                        D3D12_RESOURCE_STATE_RENDER_TARGET,
		                                                                        D3D12_RESOURCE_STATE_PRESENT);
		d->cmd_list->ResourceBarrier(1, &barrier);
	}

	HASSERT(d->cmd_list->Close());

	ID3D12CommandList* const cmd_list[] = {d->cmd_list};

	d->cmd_queue->ExecuteCommandLists(ARRAY_LENGTH(cmd_list), cmd_list);

	d->frame_fence_values[d->back_buffer_index] = signal(d->cmd_queue, d->fence, &d->fence_value);

	u32 sync_interval = d->vsync ? 1 : 0;
	u32 present_flags = d->tearing_supported && !d->vsync ? DXGI_PRESENT_ALLOW_TEARING : 0;
	HASSERT(d->swap_chain->Present(sync_interval, present_flags));

	d->back_buffer_index = d->swap_chain->GetCurrentBackBufferIndex();
}
