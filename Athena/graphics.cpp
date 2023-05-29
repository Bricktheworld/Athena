#include "job_system.h"
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

static D3D12_COMMAND_LIST_TYPE
get_d3d12_cmd_list_type(CmdListType type)
{
	switch(type)
	{
		case CMD_LIST_TYPE_GRAPHICS:
			return D3D12_COMMAND_LIST_TYPE_DIRECT;
		case CMD_LIST_TYPE_COMPUTE:
			return D3D12_COMMAND_LIST_TYPE_COMPUTE;
		case CMD_LIST_TYPE_COPY:
			return D3D12_COMMAND_LIST_TYPE_COPY;
		default:
			UNREACHABLE;
	}
	return D3D12_COMMAND_LIST_TYPE_NONE;
}

static ID3D12Fence* 
init_fence(ID3D12Device2* d3d12_dev)
{
	ID3D12Fence* fence = nullptr;
	HASSERT(d3d12_dev->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&fence)));
	ASSERT(fence != nullptr);

	return fence;
}

Fence
init_fence(GraphicsDevice* device)
{
	Fence ret = {0};
	HASSERT(device->d3d12->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&ret.d3d12_fence)));
	ASSERT(ret.d3d12_fence != nullptr);

	ret.cpu_event = CreateEventW(NULL, FALSE, FALSE, NULL);
	ASSERT(ret.cpu_event != nullptr);

	return ret;
}

void
destroy_fence(Fence* fence)
{
	COM_RELEASE(fence->d3d12_fence);
	zero_memory(fence, sizeof(Fence));
}

static FenceValue
inc_fence(Fence* fence)
{
	return ++fence->value;
}

static FenceValue
poll_fence_value(Fence* fence)
{
	fence->last_completed_value = max(fence->last_completed_value, fence->d3d12_fence->GetCompletedValue());
	return fence->last_completed_value;
}

static bool
is_fence_complete(Fence* fence, FenceValue value)
{
	if (value > fence->last_completed_value)
	{
		poll_fence_value(fence);
	}

	return value <= fence->last_completed_value;
}

void
yield_for_fence_value(Fence* fence, FenceValue value)
{
	if (is_fence_complete(fence, value))
		return;

	HASSERT(fence->d3d12_fence->SetEventOnCompletion(value, fence->cpu_event));
	yield_async([&fence]()
	{
		ASSERT(fence != nullptr);
		WaitForSingleObject(fence->cpu_event, -1);
	});
}

CmdQueue
init_cmd_queue(GraphicsDevice* device, CmdListType type)
{
	CmdQueue ret = {0};

	D3D12_COMMAND_QUEUE_DESC desc = { };
	desc.Type = get_d3d12_cmd_list_type(type);
	desc.Priority = D3D12_COMMAND_QUEUE_PRIORITY_NORMAL;
	desc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	desc.NodeMask = 0;

	HASSERT(device->d3d12->CreateCommandQueue(&desc, IID_PPV_ARGS(&ret.d3d12_queue)));
	ASSERT(ret.d3d12_queue != nullptr);

	return ret;
}

void
destroy_cmd_queue(CmdQueue* queue)
{
	COM_RELEASE(queue->d3d12_queue);

	zero_memory(queue, sizeof(CmdQueue));
}

void
cmd_queue_gpu_wait_for_fence(CmdQueue* queue, Fence* fence, FenceValue value)
{
	HASSERT(queue->d3d12_queue->Wait(fence->d3d12_fence, value));
}

FenceValue
cmd_queue_signal(CmdQueue* queue, Fence* fence)
{
	FenceValue value = inc_fence(fence);
	HASSERT(queue->d3d12_queue->Signal(fence->d3d12_fence, value));
	return value;
}


CmdListAllocator
init_cmd_list_allocator(MEMORY_ARENA_PARAM,
                        GraphicsDevice* device,
                        CmdQueue* queue,
                        u16 pool_size)
{
	ASSERT(pool_size > 0);
	CmdListAllocator ret = {0};
	ret.d3d12_queue = queue->d3d12_queue;
	ret.fence = init_fence(device);
	ret.allocators = init_ring_queue<CmdAllocator>(MEMORY_ARENA_FWD, pool_size);
	ret.lists = init_ring_queue<ID3D12GraphicsCommandList*>(MEMORY_ARENA_FWD, pool_size);

	CmdAllocator allocator = {0};
	for (u16 i = 0; i < pool_size; i++)
	{
		HASSERT(device->d3d12->CreateCommandAllocator(get_d3d12_cmd_list_type(queue->type),
		                                              IID_PPV_ARGS(&allocator.d3d12_allocator)));
		allocator.fence_value = 0;
		ring_queue_push(&ret.allocators, allocator);
	}

	for (u16 i = 0; i < pool_size; i++)
	{
		ID3D12GraphicsCommandList* list = nullptr;
		HASSERT(device->d3d12->CreateCommandList(0,
		                                         get_d3d12_cmd_list_type(queue->type),
		                                         allocator.d3d12_allocator,
		                                         nullptr,
		                                         IID_PPV_ARGS(&list)));
		list->Close();
		ring_queue_push(&ret.lists, list);
	}


	return ret;
}

void
destroy_cmd_list_allocator(CmdListAllocator* allocator)
{
	destroy_fence(&allocator->fence);

	while (!ring_queue_is_empty(allocator->lists))
	{
		ID3D12GraphicsCommandList* list = nullptr;
		ring_queue_pop(&allocator->lists, &list);
		COM_RELEASE(list);
	}

	while (!ring_queue_is_empty(allocator->allocators))
	{
		CmdAllocator cmd_allocator = {0};
		ring_queue_pop(&allocator->allocators, &cmd_allocator);
		COM_RELEASE(cmd_allocator.d3d12_allocator);
	}
}

CmdList
alloc_cmd_list(CmdListAllocator* allocator)
{
	CmdList ret = {0};
	CmdAllocator cmd_allocator = {0};
	ring_queue_pop(&allocator->allocators, &cmd_allocator);

	yield_for_fence_value(&allocator->fence, cmd_allocator.fence_value);

	ring_queue_pop(&allocator->lists, &ret.d3d12_list);

	ret.d3d12_allocator = cmd_allocator.d3d12_allocator;

	ret.d3d12_allocator->Reset();
	ret.d3d12_list->Reset(ret.d3d12_allocator, nullptr);

	return ret;
}

void
submit_cmd_list(CmdListAllocator* allocator, CmdList* list)
{
	list->d3d12_list->Close();
	ID3D12CommandList* cmd_lists[] = { list->d3d12_list };
	allocator->d3d12_queue->ExecuteCommandLists(1, cmd_lists);

	FenceValue value = inc_fence(&allocator->fence);
	HASSERT(allocator->d3d12_queue->Signal(allocator->fence.d3d12_fence, value));

	CmdAllocator cmd_allocator = {0};
	cmd_allocator.d3d12_allocator = list->d3d12_allocator;
	cmd_allocator.fence_value = value;
	ring_queue_push(&allocator->allocators, cmd_allocator);
	ring_queue_push(&allocator->lists, list->d3d12_list);
}


static D3D12_HEAP_TYPE
get_d3d12_heap_type(GpuHeapType type)
{
	switch(type)
	{
		case GPU_HEAP_TYPE_LOCAL:
			return D3D12_HEAP_TYPE_DEFAULT;
		case GPU_HEAP_TYPE_UPLOAD:
			return D3D12_HEAP_TYPE_UPLOAD;
		default:
			UNREACHABLE;
	}
	return D3D12_HEAP_TYPE_DEFAULT;
}

GpuResourceHeap
init_gpu_resource_heap(GraphicsDevice* device, u64 size, GpuHeapType type)
{
	D3D12_HEAP_DESC desc = {0};
	desc.SizeInBytes = size;
	desc.Properties = CD3DX12_HEAP_PROPERTIES(get_d3d12_heap_type(type));

	// TODO(Brandon): If we ever do MSAA textures then this needs to change.
	desc.Alignment = KiB(64);
	desc.Flags = D3D12_HEAP_FLAG_NONE;

	GpuResourceHeap ret = {0};
	ret.size = size;
	ret.type = type;

	HASSERT(device->d3d12->CreateHeap(&desc, IID_PPV_ARGS(&ret.d3d12_heap)));

	return ret;
}

void
destroy_gpu_resource_heap(GpuResourceHeap* heap)
{
	COM_RELEASE(heap->d3d12_heap);
	zero_memory(heap, sizeof(GpuResourceHeap));
}

GraphicsDevice
init_graphics_device(MEMORY_ARENA_PARAM)
{
#ifdef DEBUG
	ID3D12Debug* debug_interface = nullptr;
	HASSERT(D3D12GetDebugInterface(IID_PPV_ARGS(&debug_interface)));
	debug_interface->EnableDebugLayer();
	defer { COM_RELEASE(debug_interface); };
#endif

	GraphicsDevice res;
	auto* factory = init_factory();
	defer { COM_RELEASE(factory); };

	auto* adapter = init_adapter(factory);
	defer { COM_RELEASE(adapter); };

	GraphicsDevice ret = {0};
	ret.d3d12 = init_d3d12_device(adapter);
	ret.graphics_queue = init_cmd_queue(&ret, CMD_LIST_TYPE_GRAPHICS);
	ret.graphics_cmd_allocator = init_cmd_list_allocator(MEMORY_ARENA_FWD,
	                                                     &ret,
	                                                     &ret.graphics_queue,
	                                                     FRAMES_IN_FLIGHT * 16);
	ret.compute_queue = init_cmd_queue(&ret, CMD_LIST_TYPE_COMPUTE);
	ret.compute_cmd_allocator = init_cmd_list_allocator(MEMORY_ARENA_FWD,
	                                                    &ret,
	                                                    &ret.compute_queue,
	                                                    FRAMES_IN_FLIGHT * 8);
	ret.copy_queue = init_cmd_queue(&ret, CMD_LIST_TYPE_COPY);
	ret.copy_cmd_allocator = init_cmd_list_allocator(MEMORY_ARENA_FWD,
	                                                 &ret,
	                                                 &ret.copy_queue,
	                                                 FRAMES_IN_FLIGHT * 8);

	return ret;
}

void
wait_for_device_idle(GraphicsDevice* device)
{
//	yield_flush_queue(&device->graphics_queue);
//	yield_flush_queue(&device->compute_queue);
//	yield_flush_queue(&device->copy_queue);
}

void
destroy_graphics_device(GraphicsDevice* device)
{
	destroy_cmd_list_allocator(&device->graphics_cmd_allocator);
	destroy_cmd_list_allocator(&device->compute_cmd_allocator);
	destroy_cmd_list_allocator(&device->copy_cmd_allocator);

	destroy_cmd_queue(&device->graphics_queue);
	destroy_cmd_queue(&device->compute_queue);
	destroy_cmd_queue(&device->copy_queue);

	COM_RELEASE(device->d3d12);
	zero_memory(device, sizeof(GraphicsDevice));
}

static bool
is_depth_format(DXGI_FORMAT format)
{
	return format == DXGI_FORMAT_D32_FLOAT ||
	       format == DXGI_FORMAT_D16_UNORM ||
	       format == DXGI_FORMAT_D24_UNORM_S8_UINT ||
	       format == DXGI_FORMAT_D32_FLOAT_S8X24_UINT;
}

GpuImage
alloc_image_2D_no_heap(GraphicsDevice* device, GpuImageDesc desc, const wchar_t* name)
{
	GpuImage ret = {0};
	ret.desc = desc;

	D3D12_HEAP_PROPERTIES heap_props = CD3DX12_HEAP_PROPERTIES(get_d3d12_heap_type(GPU_HEAP_TYPE_LOCAL));
	D3D12_RESOURCE_DESC resource_desc;
	resource_desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	resource_desc.Format = desc.format;
	resource_desc.Alignment = D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT;
	resource_desc.Width = desc.width;
	resource_desc.Height = desc.height;
	resource_desc.DepthOrArraySize = 1;
	resource_desc.MipLevels = 1;
	resource_desc.SampleDesc.Count = 1;
	resource_desc.SampleDesc.Quality = 0;
	resource_desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	resource_desc.Flags = desc.flags;

	if (is_depth_format(desc.format))
	{
		resource_desc.Flags |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
	}

	HASSERT(device->d3d12->CreateCommittedResource(&heap_props,
	                                               D3D12_HEAP_FLAG_NONE,
	                                               &resource_desc,
	                                               desc.initial_state,
	                                               desc.clear_value ? &unwrap(desc.clear_value) : nullptr,
	                                               IID_PPV_ARGS(&ret.d3d12_image)));

	ret.d3d12_image->SetName(name);

	return ret;
}

void
free_image(GpuImage* image)
{
	COM_RELEASE(image->d3d12_image);
	zero_memory(image, sizeof(GpuImage));
}

static D3D12_DESCRIPTOR_HEAP_TYPE
get_d3d12_descriptor_type(DescriptorHeapType type)
{
	switch(type)
	{
		case DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV:
			return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
		case DESCRIPTOR_HEAP_TYPE_SAMPLER:
			return D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER;
		case DESCRIPTOR_HEAP_TYPE_RTV:
			return D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		case DESCRIPTOR_HEAP_TYPE_DSV:
			return D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		default:
			UNREACHABLE;
	}
	return D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
}

static bool
descriptor_type_is_shader_visible(DescriptorHeapType type)
{
	return type == DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV || type == DESCRIPTOR_HEAP_TYPE_SAMPLER;
}

DescriptorHeap
init_descriptor_heap(MEMORY_ARENA_PARAM, GraphicsDevice* device, u32 size, DescriptorHeapType type)
{
	DescriptorHeap ret = {0};
	ret.num_descriptors = size;
	ret.type = type;
	ret.free_descriptors = init_ring_queue<u32>(MEMORY_ARENA_FWD, size);

	D3D12_DESCRIPTOR_HEAP_DESC desc;
	desc.Type = get_d3d12_descriptor_type(type);
	desc.NumDescriptors = size;
	desc.NodeMask = 1;
	desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;

	bool is_shader_visible = descriptor_type_is_shader_visible(type);

	if (is_shader_visible)
	{
		desc.Flags |= D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
		ASSERT(size <= 2048);
	}

	HASSERT(device->d3d12->CreateDescriptorHeap(&desc, IID_PPV_ARGS(&ret.d3d12_heap)));
	ret.descriptor_size = device->d3d12->GetDescriptorHandleIncrementSize(desc.Type);
	ret.cpu_start = ret.d3d12_heap->GetCPUDescriptorHandleForHeapStart();
	if (is_shader_visible)
	{
		ret.gpu_start = ret.d3d12_heap->GetGPUDescriptorHandleForHeapStart();
	}

	for (u32 i = 0; i < size; i++)
	{
		ring_queue_push(&ret.free_descriptors, i);
	}

	return ret;
}

void
destroy_descriptor_heap(DescriptorHeap* heap)
{
	COM_RELEASE(heap->d3d12_heap);
	zero_memory(heap, sizeof(DescriptorHeap));
}

Descriptor
alloc_descriptor(DescriptorHeap* heap)
{
	u32 index = 0;
	ring_queue_pop(&heap->free_descriptors, &index);
	u64 offset = index * heap->descriptor_size;

	Descriptor ret = {0};
	ret.cpu_handle.ptr = heap->cpu_start.ptr + offset;
	ret.gpu_handle = None;
	ret.index = index;

	if (heap->gpu_start)
	{
		unwrap(ret.gpu_handle).ptr = unwrap(heap->gpu_start).ptr + offset;
	}

	ret.type = heap->type;

	return ret;
}

void
free_descriptor(DescriptorHeap* heap, Descriptor* descriptor)
{
	ASSERT(descriptor->cpu_handle.ptr >= heap->cpu_start.ptr);
	ASSERT(descriptor->index < heap->num_descriptors);
	ring_queue_push(&heap->free_descriptors, descriptor->index);
	zero_memory(descriptor, sizeof(Descriptor));
}


RenderTargetView
alloc_rtv(GraphicsDevice* device, DescriptorHeap* heap, const GpuImage* image)
{
	RenderTargetView ret = {0};
	ret.descriptor = alloc_descriptor(heap);

	device->d3d12->CreateRenderTargetView(image->d3d12_image,
	                                      nullptr,
	                                      ret.descriptor.cpu_handle);
	ret.image = image;

	return ret;
}

DepthStencilView
alloc_dsv(GraphicsDevice* device, DescriptorHeap* heap, const GpuImage* image)
{
	DepthStencilView ret = {0};
	ret.descriptor = alloc_descriptor(heap);

	device->d3d12->CreateDepthStencilView(image->d3d12_image,
	                                      nullptr,
	                                      ret.descriptor.cpu_handle);
	ret.image = image;

	return ret;
}

GpuShader
load_shader_from_file(GraphicsDevice* device, const wchar_t* path)
{
	GpuShader ret = {0};
	HASSERT(D3DReadFileToBlob(path, &ret.d3d12_shader));
	return ret;
}

void
destroy_shader(GpuShader* shader)
{
	COM_RELEASE(shader->d3d12_shader);
}

//GraphicsPipelineHash
//hash_pipeline_desc(GraphicsPipelineDesc desc)
//{
//	USE_SCRATCH_ARENA();
//	hash_u64();
//}

GraphicsPipeline
init_graphics_pipeline(GraphicsDevice* device,
                       GraphicsPipelineDesc desc,
                       const wchar_t* name)
{
	GraphicsPipeline ret = {0};

	D3D12_RENDER_TARGET_BLEND_DESC render_target_blend_desc;
	render_target_blend_desc.BlendEnable = FALSE;
	render_target_blend_desc.LogicOpEnable = FALSE;
	render_target_blend_desc.SrcBlend = D3D12_BLEND_SRC_ALPHA;
	render_target_blend_desc.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	render_target_blend_desc.BlendOp = D3D12_BLEND_OP_ADD;
	render_target_blend_desc.SrcBlendAlpha = D3D12_BLEND_ONE;
	render_target_blend_desc.DestBlendAlpha = D3D12_BLEND_ZERO;
	render_target_blend_desc.BlendOpAlpha = D3D12_BLEND_OP_ADD;
	render_target_blend_desc.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	D3D12_BLEND_DESC blend_desc;
	blend_desc.AlphaToCoverageEnable = FALSE;
	blend_desc.IndependentBlendEnable = FALSE;

	for(u32 i = 0; i < desc.render_targets.size; i++)
	{
		blend_desc.RenderTarget[i] = render_target_blend_desc;
	}

	D3D12_DEPTH_STENCIL_DESC depth_stencil_desc;
	depth_stencil_desc.DepthEnable = static_cast<bool>(desc.depth_stencil_view);
	depth_stencil_desc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
	depth_stencil_desc.DepthFunc = unwrap_or(desc.comparison_func, D3D12_COMPARISON_FUNC_NONE);
	depth_stencil_desc.StencilEnable = desc.stencil_enable;
	depth_stencil_desc.StencilReadMask = D3D12_DEFAULT_STENCIL_READ_MASK;
	depth_stencil_desc.StencilWriteMask = D3D12_DEFAULT_STENCIL_WRITE_MASK;


	D3D12_GRAPHICS_PIPELINE_STATE_DESC pso_desc;
//	pso_desc.pRootSignature = PipelineState::s_rootSignature.Get(),
	pso_desc.VS = CD3DX12_SHADER_BYTECODE(desc.vertex_shader.d3d12_shader);
	if (desc.pixel_shader)
	{
		pso_desc.PS = CD3DX12_SHADER_BYTECODE(unwrap(desc.pixel_shader).d3d12_shader);
	}
	pso_desc.BlendState = blend_desc,
	pso_desc.SampleMask = UINT32_MAX,
	pso_desc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT),
	pso_desc.DepthStencilState = depth_stencil_desc,
	pso_desc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE,
	pso_desc.NumRenderTargets = static_cast<u32>(desc.render_targets.size);

	pso_desc.DSVFormat = DXGI_FORMAT_UNKNOWN;
	if (desc.depth_stencil_view)
	{
		pso_desc.DSVFormat = unwrap(desc.depth_stencil_view).image->desc.format;
	}

	pso_desc.SampleDesc.Count = 1;
	pso_desc.SampleDesc.Quality = 0;
	pso_desc.NodeMask = 0;

	pso_desc.RasterizerState.FrontCounterClockwise = true;
	pso_desc.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;

	for (u32 i = 0; i < desc.render_targets.size; i++)
	{
		pso_desc.RTVFormats[i] = desc.render_targets[i].image->desc.format;
	}

	HASSERT(device->d3d12->CreateGraphicsPipelineState(&pso_desc, IID_PPV_ARGS(&ret.d3d12_pso)));

	ret.d3d12_pso->SetName(name);

	return ret;
}

void
destroy_graphics_pipeline(GraphicsPipeline* pipeline)
{
	COM_RELEASE(pipeline->d3d12_pso);
	zero_memory(pipeline, sizeof(GraphicsPipeline));
}

static void
alloc_back_buffers_from_swap_chain(const SwapChain* swap_chain,
                                   GpuImage** back_buffers,
                                   u32 num_back_buffers)
{
	GpuImageDesc desc = {0};
	desc.width = swap_chain->width;
	desc.height = swap_chain->height;
	desc.format = swap_chain->format;
	desc.initial_state = D3D12_RESOURCE_STATE_PRESENT;
	for (u32 i = 0; i < num_back_buffers; i++)
	{
		HASSERT(swap_chain->d3d12_swap_chain->GetBuffer(i, IID_PPV_ARGS(&back_buffers[i]->d3d12_image)));
		back_buffers[i]->desc = desc;
	}
}


SwapChain
init_swap_chain(MEMORY_ARENA_PARAM, HWND window, GraphicsDevice* device)
{
	auto* factory = init_factory();
	defer { COM_RELEASE(factory); };

	SwapChain ret = {0};

	RECT client_rect;
	GetClientRect(window, &client_rect);
	ret.width = client_rect.right - client_rect.left;
	ret.height = client_rect.bottom - client_rect.top;
	ret.format = DXGI_FORMAT_R8G8B8A8_UNORM;
	ret.tearing_supported = check_tearing_support(factory);


	DXGI_SWAP_CHAIN_DESC1 swap_chain_desc = { 0 };
	swap_chain_desc.Width = ret.width;
	swap_chain_desc.Height = ret.height;
	swap_chain_desc.Format = ret.format;
	swap_chain_desc.Stereo = FALSE;
	swap_chain_desc.SampleDesc = { 1, 0 };
	swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swap_chain_desc.BufferCount = ARRAY_LENGTH(ret.back_buffers);
	swap_chain_desc.Scaling = DXGI_SCALING_STRETCH;
	swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swap_chain_desc.AlphaMode = DXGI_ALPHA_MODE_UNSPECIFIED;
	swap_chain_desc.Flags = ret.tearing_supported ? DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING : 0;

	IDXGISwapChain1* swap_chain1 = nullptr;
	HASSERT(factory->CreateSwapChainForHwnd(device->graphics_queue.d3d12_queue,
	                                        window,
	                                        &swap_chain_desc,
	                                        nullptr,
	                                        nullptr,
	                                        &swap_chain1));

	HASSERT(factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER));
	HASSERT(swap_chain1->QueryInterface(IID_PPV_ARGS(&ret.d3d12_swap_chain)));
	COM_RELEASE(swap_chain1);

	ret.fence = init_fence(device);
	zero_memory(ret.frame_fence_values, sizeof(ret.frame_fence_values));

	for (u32 i = 0; i < ARRAY_LENGTH(ret.back_buffers); i++)
	{
		ret.back_buffers[i] = push_memory_arena<GpuImage>(MEMORY_ARENA_FWD);
	}
	ret.depth_buffer = push_memory_arena<GpuImage>(MEMORY_ARENA_FWD);

	alloc_back_buffers_from_swap_chain(&ret,
	                                   ret.back_buffers,
	                                   ARRAY_LENGTH(ret.back_buffers));
	ret.back_buffer_index = 0;

	GpuImageDesc desc = {0};
	desc.width = ret.width;
	desc.height = ret.height;
	desc.format = DXGI_FORMAT_D32_FLOAT;
	desc.initial_state = D3D12_RESOURCE_STATE_DEPTH_WRITE;
	D3D12_CLEAR_VALUE depth_clear_value;
	depth_clear_value.Format = desc.format;
	depth_clear_value.DepthStencil.Depth = 0.0f;
	depth_clear_value.DepthStencil.Stencil = 0;
	desc.clear_value = depth_clear_value;
	*ret.depth_buffer = alloc_image_2D_no_heap(device, desc, L"SwapChain Depth Buffer");

	ret.render_target_view_heap = init_descriptor_heap(MEMORY_ARENA_FWD,
	                                                   device,
	                                                   ARRAY_LENGTH(ret.back_buffers),
	                                                   DESCRIPTOR_HEAP_TYPE_RTV);
	ret.depth_stencil_view_heap = init_descriptor_heap(MEMORY_ARENA_FWD,
	                                                   device,
	                                                   1,
	                                                   DESCRIPTOR_HEAP_TYPE_DSV);

	for (u32 i = 0; i < ARRAY_LENGTH(ret.back_buffers); i++)
	{
		ret.back_buffer_views[i] = alloc_rtv(device, &ret.render_target_view_heap, ret.back_buffers[i]);
	}

	ret.depth_stencil_view = alloc_dsv(device, &ret.depth_stencil_view_heap, ret.depth_buffer);

	return ret;
}

void
destroy_swap_chain(SwapChain* swap_chain)
{
	destroy_descriptor_heap(&swap_chain->depth_stencil_view_heap);
	destroy_descriptor_heap(&swap_chain->render_target_view_heap);

	free_image(swap_chain->depth_buffer);
	for (auto* image : swap_chain->back_buffers)
	{
		free_image(image);
	}
	destroy_fence(&swap_chain->fence);
	COM_RELEASE(swap_chain->d3d12_swap_chain);
}


RenderTargetView*
swap_chain_acquire(SwapChain* swap_chain)
{
	u32 index = swap_chain->back_buffer_index;
	yield_for_fence_value(&swap_chain->fence,
	                      swap_chain->frame_fence_values[index]);

	return &swap_chain->back_buffer_views[index];
}

void
swap_chain_submit(SwapChain* swap_chain, GraphicsDevice* device, RenderTargetView* rtv)
{
	u32 index = swap_chain->back_buffer_index;
	ASSERT(&swap_chain->back_buffer_views[index] == rtv);

	FenceValue value = cmd_queue_signal(&device->graphics_queue, &swap_chain->fence);
	swap_chain->frame_fence_values[index] = value;

	u32 sync_interval = swap_chain->vsync ? 1 : 0;
	u32 present_flags = swap_chain->tearing_supported && !swap_chain->vsync ? DXGI_PRESENT_ALLOW_TEARING : 0;
	HASSERT(swap_chain->d3d12_swap_chain->Present(sync_interval, present_flags));

	swap_chain->back_buffer_index = swap_chain->d3d12_swap_chain->GetCurrentBackBufferIndex();
}

void
cmd_image_transition(CmdList* cmd,
                     const GpuImage* image,
                     D3D12_RESOURCE_STATES before,
                     D3D12_RESOURCE_STATES after)
{
	auto barrier = CD3DX12_RESOURCE_BARRIER::Transition(image->d3d12_image, before, after);
	cmd->d3d12_list->ResourceBarrier(1, &barrier);
}

void
cmd_clear_rtv(CmdList* cmd, RenderTargetView* rtv, Rgba clear_color)
{
	cmd->d3d12_list->ClearRenderTargetView(rtv->descriptor.cpu_handle, (f32*)&clear_color, 0, nullptr);
}

void
cmd_clear_dsv(CmdList* cmd, D3D12_CLEAR_FLAGS flags, DepthStencilView* dsv, f32 depth, u8 stencil)
{
	cmd->d3d12_list->ClearDepthStencilView(dsv->descriptor.cpu_handle, flags, depth, stencil, 0, nullptr);
}

void
cmd_set_viewport(CmdList* cmd, f32 top, f32 left, f32 width, f32 height)
{
	auto viewport = CD3DX12_VIEWPORT(top, left, width, height);
	cmd->d3d12_list->RSSetViewports(1, &viewport);
}

void
cmd_set_scissor(CmdList* cmd, u32 left, u32 top, u32 right, u32 bottom)
{
	auto viewport = CD3DX12_RECT(left, top, right, bottom);
	cmd->d3d12_list->RSSetScissorRects(1, &viewport);
}

void
cmd_set_render_targets(CmdList* cmd, const Array<RenderTargetView> render_targets, DepthStencilView dsv)
{
	USE_SCRATCH_ARENA();
	auto rtv_handles = init_array<D3D12_CPU_DESCRIPTOR_HANDLE>(SCRATCH_ARENA_PASS, render_targets.size);
	for (RenderTargetView rtv : render_targets)
	{
		*array_add(&rtv_handles) = rtv.descriptor.cpu_handle;
	}

	cmd->d3d12_list->OMSetRenderTargets(static_cast<u32>(render_targets.size),
	                                    &rtv_handles[0],
	                                    FALSE,
	                                    &dsv.descriptor.cpu_handle);
}

void
cmd_set_descriptor_heaps(CmdList* cmd, const DescriptorHeap* heaps, u32 num_heaps)
{
	USE_SCRATCH_ARENA();
	auto d3d12_heaps = init_array<ID3D12DescriptorHeap*>(SCRATCH_ARENA_PASS, num_heaps);
	for (u32 i = 0; i < num_heaps; i++)
	{
		*array_add(&d3d12_heaps) = heaps[i].d3d12_heap;
	}

	cmd->d3d12_list->SetDescriptorHeaps(num_heaps, &d3d12_heaps[0]);
}

#if 0
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

#endif