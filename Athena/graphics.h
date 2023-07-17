#pragma once
#include "array.h"
#include "ring_buffer.h"
#include "hash_table.h"
#include "types.h"
#include "math/math.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

namespace gfx
{
	typedef Vec4 Rgba;
	typedef Vec3 Rgb;
	
	const Vec4 VERTICES[8] =
	{
		Vec4(-1.0f, -1.0f, -1.0f, 1.0f), // 0
		Vec4(-1.0f, 1.0f, -1.0f, 1.0f),  // 1
		Vec4(1.0f, 1.0f, -1.0f, 1.0f),   // 2
		Vec4(1.0f, -1.0f, -1.0f, 1.0f),  // 3
		Vec4(-1.0f, -1.0f, 1.0f, 1.0f),  // 4
		Vec4(-1.0f, 1.0f, 1.0f, 1.0f),   // 5
		Vec4( 1.0f, 1.0f, 1.0f, 1.0f),   // 6
		Vec4(1.0f, -1.0f, 1.0f, 1.0f)    // 7
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
	
	constant u8 kFramesInFlight = 2;
	constant u8 kMaxCommandListThreads = 8;
	constant u8 kCommandAllocators = kFramesInFlight * kMaxCommandListThreads;
	
	struct GraphicsDevice;
	typedef u64 FenceValue;
	struct Fence
	{
		ID3D12Fence* d3d12_fence = nullptr;
		FenceValue value = 0;
		FenceValue last_completed_value = 0;
		HANDLE cpu_event = nullptr;
	};
	
	Fence init_fence(const GraphicsDevice* device);
	void destroy_fence(Fence* fence);
	void yield_for_fence_value(Fence* fence, FenceValue value);
	void block_for_fence_value(Fence* fence, FenceValue value);
	
	enum CmdQueueType : u8
	{
		kCmdQueueTypeGraphics,
		kCmdQueueTypeCompute,
		kCmdQueueTypeCopy,
	
		kCmdQueueTypeCount,
	};
	
	struct CmdQueue
	{
		ID3D12CommandQueue* d3d12_queue = nullptr;
		CmdQueueType type = kCmdQueueTypeGraphics;
	};
	
	CmdQueue init_cmd_queue(const GraphicsDevice* device, CmdQueueType type);
	void destroy_cmd_queue(const CmdQueue* queue);
	
	void cmd_queue_gpu_wait_for_fence(const CmdQueue* queue, Fence* fence, FenceValue value);
	FenceValue cmd_queue_signal(const CmdQueue* queue, Fence* fence);
	
	struct CmdAllocator
	{
		ID3D12CommandAllocator* d3d12_allocator = 0;
		FenceValue fence_value = 0;
	};
	
	struct CmdListAllocator
	{
		ID3D12CommandQueue* d3d12_queue = nullptr;
	
		RingQueue<CmdAllocator> allocators;
		RingQueue<ID3D12GraphicsCommandList*> lists;
		Fence fence;
	};
	
	struct CmdList
	{
		ID3D12GraphicsCommandList* d3d12_list = nullptr;
		ID3D12CommandAllocator* d3d12_allocator = nullptr;
	};
	
	CmdListAllocator init_cmd_list_allocator(MEMORY_ARENA_PARAM,
																					const GraphicsDevice* device,
																					const CmdQueue* queue,
																					u16 pool_size);
	void destroy_cmd_list_allocator(CmdListAllocator* allocator);
	CmdList alloc_cmd_list(CmdListAllocator* allocator);
	FenceValue submit_cmd_lists(CmdListAllocator* allocator,
														  Span<CmdList> lists,
														  Option<Fence*> fence = None);
	
	struct GraphicsDevice
	{
		ID3D12Device* d3d12 = nullptr;
		CmdQueue graphics_queue;
		CmdListAllocator graphics_cmd_allocator;
		CmdQueue compute_queue;
		CmdListAllocator compute_cmd_allocator;
		CmdQueue copy_queue;
		CmdListAllocator copy_cmd_allocator;
	};
	
	GraphicsDevice init_graphics_device(MEMORY_ARENA_PARAM);
	void destroy_graphics_device(GraphicsDevice* device);
	
	void wait_for_device_idle(const GraphicsDevice* device);
	
	enum GpuHeapType : u8
	{
		// GPU only
		kGpuHeapTypeLocal,
		// CPU to GPU
		kGpuHeapTypeUpload,
	
		kGpuHeapTypeCount,
	};
	
	struct GpuResourceHeap
	{
		ID3D12Heap* d3d12_heap = nullptr;
		u64 size = 0;
		GpuHeapType type = kGpuHeapTypeLocal;
	};
	
	GpuResourceHeap init_gpu_resource_heap(const GraphicsDevice* device,
																				u64 size,
																				GpuHeapType type);
	void destroy_gpu_resource_heap(GpuResourceHeap* heap);
	
	struct GpuLinearAllocator
	{
		GpuResourceHeap heap;
		u64 pos = 0;
	};
	
	GpuLinearAllocator init_gpu_linear_allocator(const GraphicsDevice* device,
																							u64 size,
																							GpuHeapType type);
	void destroy_gpu_linear_allocator(GpuLinearAllocator* allocator);
	inline void
	reset_gpu_linear_allocator(GpuLinearAllocator* allocator)
	{
		allocator->pos = 0;
	}
	
	
	struct GpuImageDesc
	{
		u32 width = 0;
		u32 height = 0;
	
		// TODO(Brandon): Eventually make these less verbose and platform agnostic.
		DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
		D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COMMON;
	
		D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
		Option<D3D12_CLEAR_VALUE> clear_value = None;
	};
	
	struct GpuImage
	{
		GpuImageDesc desc;
		ID3D12Resource* d3d12_image = nullptr;
		D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
	};
	
	GpuImage alloc_gpu_image_2D_no_heap(const GraphicsDevice* device,
																			GpuImageDesc desc,
																			const wchar_t* name);
	void free_gpu_image(GpuImage* image);
	
	GpuImage alloc_gpu_image_2D(const GraphicsDevice* device,
															GpuLinearAllocator* allocator,
															GpuImageDesc desc,
															const wchar_t* name);
	
	struct GpuBufferDesc
	{
		u64 size = 0;
		D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
	};
	
	struct GpuBuffer
	{
		GpuBufferDesc desc;
		ID3D12Resource* d3d12_buffer = nullptr;
		u64 gpu_addr = 0;
	
		Option<void*> mapped = None;
		D3D12_RESOURCE_STATES state = D3D12_RESOURCE_STATE_COMMON;
	};
	GpuBuffer alloc_gpu_buffer_no_heap(const GraphicsDevice* device,
																		GpuBufferDesc desc,
																		GpuHeapType type,
																		const wchar_t* name);
	void free_gpu_buffer(GpuBuffer* buffer);
	
	GpuBuffer alloc_gpu_buffer(const GraphicsDevice* device,
														GpuLinearAllocator* allocator,
														GpuBufferDesc desc,
														const wchar_t* name);
	
	struct GpuUploadRange
	{
		u64 size = 0;
		FenceValue fence_value = 0;
	};
	
	struct GpuUploadRingBuffer
	{
		GpuBuffer gpu_buffer;
		u64 write = 0;
		u64 read = 0;
		u64 size = 0;
	
		RingQueue<GpuUploadRange> upload_fence_values;
		Fence fence;
		HANDLE cpu_event;
	};
	
	GpuUploadRingBuffer alloc_gpu_ring_buffer(MEMORY_ARENA_PARAM,
																						const GraphicsDevice* device,
																						u64 size);
	void free_gpu_ring_buffer(GpuUploadRingBuffer* gpu_upload_ring_buffer);
	FenceValue block_gpu_upload_buffer(CmdListAllocator* cmd_allocator,
																		GpuUploadRingBuffer* ring_buffer,
																		GpuBuffer* dst,
																		u64 dst_offset,
																		const void* src,
																		u64 size,
																		u64 alignment);
	
	enum DescriptorType : u8
	{
		kDescriptorTypeCbv     = 0x1,
		kDescriptorTypeSrv     = 0x2,
		kDescriptorTypeUav     = 0x4,
		kDescriptorTypeSampler = 0x8,
		kDescriptorTypeRtv     = 0x10,
		kDescriptorTypeDsv     = 0x20,
	};
	
	enum DescriptorHeapType : u8
	{
		kDescriptorHeapTypeCbvSrvUav = kDescriptorTypeCbv | kDescriptorTypeSrv | kDescriptorTypeUav,
		kDescriptorHeapTypeSampler   = kDescriptorTypeSampler,
		kDescriptorHeapTypeRtv       = kDescriptorTypeRtv,
		kDescriptorHeapTypeDsv       = kDescriptorTypeDsv,
	};
	
	struct DescriptorPool
	{
		ID3D12DescriptorHeap* d3d12_heap = nullptr;
		RingQueue<u32> free_descriptors;
		u64 descriptor_size = 0;
	
		D3D12_CPU_DESCRIPTOR_HANDLE cpu_start = {0};
		Option<D3D12_GPU_DESCRIPTOR_HANDLE> gpu_start = None;
	
		u32 num_descriptors = 0;
		DescriptorHeapType type = kDescriptorHeapTypeCbvSrvUav;
	};
	
	DescriptorPool init_descriptor_pool(MEMORY_ARENA_PARAM,
																			const GraphicsDevice* device,
																			u32 size,
																			DescriptorHeapType type);
	
	void destroy_descriptor_pool(DescriptorPool* pool);

	struct DescriptorLinearAllocator
	{
		ID3D12DescriptorHeap* d3d12_heap = nullptr;
		u32 pos = 0;
		u32 num_descriptors = 0;

		u64 descriptor_size = 0;

		D3D12_CPU_DESCRIPTOR_HANDLE cpu_start = {0};
		Option<D3D12_GPU_DESCRIPTOR_HANDLE> gpu_start = None;

		DescriptorHeapType type = kDescriptorHeapTypeCbvSrvUav;
	};

	DescriptorLinearAllocator init_descriptor_linear_allocator(const GraphicsDevice* device,
	                                                           u32 size,
	                                                           DescriptorHeapType type);
	
	void reset_descriptor_linear_allocator(DescriptorLinearAllocator* allocator);
	void destroy_descriptor_linear_allocator(DescriptorLinearAllocator* allocator);
	
	struct Descriptor
	{
		D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = {0};
		Option<D3D12_GPU_DESCRIPTOR_HANDLE> gpu_handle = None;
		u32 index = 0;
		DescriptorHeapType type = kDescriptorHeapTypeCbvSrvUav;
	};
	
	Descriptor alloc_descriptor(DescriptorPool* pool);
	void free_descriptor(DescriptorPool* heap, Descriptor* descriptor);

	Descriptor alloc_descriptor(DescriptorLinearAllocator* allocator);

	
	void init_buffer_cbv(const GraphicsDevice* device,
                       Descriptor* descriptor,
                       const GpuBuffer* buffer,
                       u64 offset,
                       u32 size);
	
	void init_buffer_srv(const GraphicsDevice* device,
                       Descriptor* descriptor,
                       const GpuBuffer* buffer,
                       u32 first_element,
                       u32 num_elements,
                       u32 stride);
	
	void init_buffer_uav(const GraphicsDevice* device,
                       Descriptor* descriptor,
                       const GpuBuffer* buffer,
                       u32 first_element,
                       u32 num_elements,
                       u32 stride);
	
	void init_rtv(const GraphicsDevice* device, Descriptor* descriptor, const GpuImage* image);
	void init_dsv(const GraphicsDevice* device, Descriptor* descriptor, const GpuImage* image);
	void init_image_2D_srv(const GraphicsDevice* device, Descriptor* descriptor, const GpuImage* image);
	void init_image_2D_uav(const GraphicsDevice* device, Descriptor* descriptor, const GpuImage* image);

	void init_sampler(const GraphicsDevice* device, Descriptor* descriptor);
	
	
	struct GpuShader
	{
		ID3DBlob* d3d12_shader = nullptr;
	};
	
	GpuShader load_shader_from_file(const GraphicsDevice* device, const wchar_t* path);
	void destroy_shader(GpuShader* shader);
	
	struct GraphicsPipelineDesc
	{
		GpuShader vertex_shader;
		Option<GpuShader> pixel_shader = None;
		Option<D3D12_COMPARISON_FUNC> comparison_func = None;
		Array<DXGI_FORMAT, 8> rtv_formats;
		Option<DXGI_FORMAT> dsv_format = None;
		bool stencil_enable;
	};
	
	struct PipelineState
	{
		ID3D12PipelineState* d3d12_pso = nullptr;
	};
	PipelineState init_graphics_pipeline(const GraphicsDevice* device,
																			 GraphicsPipelineDesc desc,
																			 const wchar_t* name);
	void destroy_pipeline_state(PipelineState* pipeline);
	
	struct SwapChain
	{
		u32 width = 0;
		u32 height = 0;
		DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
	
		IDXGISwapChain4* d3d12_swap_chain = nullptr;
		Fence fence;
		FenceValue frame_fence_values[kFramesInFlight] = {0};
	
		GpuImage* back_buffers[kFramesInFlight] = {0};
		u32 back_buffer_index = 0;
	//	RenderTargetView back_buffer_views[kFramesInFlight] = {0};
	
	//	GpuImage* depth_buffer;
	//	DepthStencilView depth_stencil_view;
	
	//	DescriptorHeap render_target_view_heap;
	//	DescriptorHeap depth_stencil_view_heap;
	
		bool vsync = false;
		bool tearing_supported = false;
		bool fullscreen = false;
	};
	
	SwapChain init_swap_chain(MEMORY_ARENA_PARAM, HWND window, const GraphicsDevice* device);
	void destroy_swap_chain(SwapChain* swap_chain);
	
	const GpuImage* swap_chain_acquire(SwapChain* swap_chain);
	void swap_chain_submit(SwapChain* swap_chain, const GraphicsDevice* device, const GpuImage* rtv);
	
	void cmd_image_transition(CmdList* cmd,
														const GpuImage* image,
														D3D12_RESOURCE_STATES before,
														D3D12_RESOURCE_STATES after);
	void cmd_clear_rtv(CmdList* cmd, Descriptor rtv, Vec4 clear_color);
	void cmd_clear_dsv(CmdList* cmd,
										D3D12_CLEAR_FLAGS flags,
										Descriptor dsv,
										f32 depth,
										u8 stencil);
	void cmd_set_viewport(CmdList* cmd, f32 top, f32 left, f32 width, f32 height);
	void cmd_set_scissor(CmdList* cmd,
											u32 left = 0,
											u32 top = 0,
											u32 right = LONG_MAX,
											u32 bottom = LONG_MAX);
	void cmd_set_render_targets(CmdList* cmd, Span<Descriptor> render_targets, Option<Descriptor> dsv);
	void cmd_set_descriptor_heaps(CmdList* cmd, const DescriptorPool* heaps, u32 num_heaps);
	void cmd_set_descriptor_heaps(CmdList* cmd, Span<const DescriptorLinearAllocator*> heaps);
	void cmd_set_pipeline(CmdList* cmd, const PipelineState* pipeline);
	void cmd_set_index_buffer(CmdList* cmd, const GpuBuffer* buffer, u32 start_index, u32 num_indices);
	void cmd_set_primitive_topology(CmdList* cmd);
	void cmd_set_graphics_root_signature(CmdList* cmd);
	void cmd_set_graphics_32bit_constants(CmdList* cmd, const void* data);
	void cmd_draw_indexed(CmdList* cmd, u32 num_indices);
	void cmd_draw(CmdList* cmd, u32 num_vertices);
	
	void init_imgui_ctx(MEMORY_ARENA_PARAM,
											const GraphicsDevice* device,
											const SwapChain* swap_chain,
											HWND window,
											DescriptorPool* cbv_srv_uav_heap);
	void destroy_imgui_ctx();
	void imgui_begin_frame();
	void imgui_end_frame();
	void cmd_imgui_render(CmdList* cmd);
}


