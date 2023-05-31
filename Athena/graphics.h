#pragma once
#include "array.h"
#include "ring_buffer.h"
#include "hash_table.h"
#include "types.h"
#include "math/math.h"
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl.h>

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

static constexpr u8 FRAMES_IN_FLIGHT = 3;
static constexpr u8 MAX_COMMAND_LIST_THREADS = 8;
static constexpr u8 COMMAND_ALLOCATORS = FRAMES_IN_FLIGHT * MAX_COMMAND_LIST_THREADS;

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

enum CmdListType : u8
{
	CMD_LIST_TYPE_GRAPHICS,
	CMD_LIST_TYPE_COMPUTE,
	CMD_LIST_TYPE_COPY,

	CMD_LIST_TYPE_COUNT,
};

struct CmdQueue
{
	ID3D12CommandQueue* d3d12_queue = nullptr;
	CmdListType type = CMD_LIST_TYPE_GRAPHICS;
};

CmdQueue init_cmd_queue(const GraphicsDevice* device, CmdListType type);
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
FenceValue submit_cmd_list(CmdListAllocator* allocator,
                           CmdList* list,
                           Option<Fence*> fence = None);

struct GraphicsDevice
{
	ID3D12Device2* d3d12 = nullptr;
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
	GPU_HEAP_TYPE_LOCAL,
	// CPU to GPU
	GPU_HEAP_TYPE_UPLOAD,

	GPU_HEAP_TYPE_COUNT,
};

struct GpuResourceHeap
{
	ID3D12Heap* d3d12_heap = nullptr;
	u64 size = 0;
	GpuHeapType type = GPU_HEAP_TYPE_LOCAL;
};

GpuResourceHeap init_gpu_resource_heap(const GraphicsDevice* device,
                                       u64 size,
                                       GpuHeapType type);
void destroy_gpu_resource_heap(GpuResourceHeap* heap);

struct GpuImageDesc
{
	u32 width = 0;
	u32 height = 0;

	// TODO(Brandon): Eventually make these less verbose and platform agnostic.
	DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
	D3D12_RESOURCE_STATES initial_state = D3D12_RESOURCE_STATE_COPY_DEST;

	D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
	Option<D3D12_CLEAR_VALUE> clear_value = None;
};

struct GpuImage
{
	GpuImageDesc desc;
	ID3D12Resource* d3d12_image = nullptr;
};

GpuImage alloc_image_2D_no_heap(const GraphicsDevice* device,
                                GpuImageDesc desc,
                                const wchar_t* name);
void free_image_no_heap(GpuImage* image);

struct GpuBufferDesc
{
	u64 size = 0;

	GpuHeapType heap_type = GPU_HEAP_TYPE_LOCAL;
	D3D12_RESOURCE_FLAGS flags = D3D12_RESOURCE_FLAG_NONE;
};

struct GpuBuffer
{
	GpuBufferDesc desc;
	ID3D12Resource* d3d12_buffer = nullptr;
	u64 gpu_addr = 0;

	Option<void*> mapped = None;
};
GpuBuffer alloc_buffer_no_heap(const GraphicsDevice* device,
                               GpuBufferDesc desc,
                               const wchar_t* name);
void free_buffer_no_heap(GpuBuffer* buffer);

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

enum DescriptorHeapType
{
	DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV,
	DESCRIPTOR_HEAP_TYPE_SAMPLER,
	DESCRIPTOR_HEAP_TYPE_RTV,
	DESCRIPTOR_HEAP_TYPE_DSV,

	DESCRIPTOR_HEAP_TYPE_COUNT,
};

struct DescriptorHeap
{
	ID3D12DescriptorHeap* d3d12_heap = nullptr;
	RingQueue<u32> free_descriptors;
	u64 descriptor_size = 0;

	D3D12_CPU_DESCRIPTOR_HANDLE cpu_start = {0};
	Option<D3D12_GPU_DESCRIPTOR_HANDLE> gpu_start = None;

	u32 num_descriptors = 0;
	DescriptorHeapType type = DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
};

DescriptorHeap init_descriptor_heap(MEMORY_ARENA_PARAM,
                                    const GraphicsDevice* device,
                                    u32 size,
                                    DescriptorHeapType type);

void destroy_descriptor_heap(DescriptorHeap* heap);

struct Descriptor
{
	D3D12_CPU_DESCRIPTOR_HANDLE cpu_handle = {0};
	Option<D3D12_GPU_DESCRIPTOR_HANDLE> gpu_handle = None;
	u32 index = 0;
	DescriptorHeapType type = DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
};

Descriptor alloc_descriptor(DescriptorHeap* heap);
void free_descriptor(DescriptorHeap* heap, Descriptor* descriptor);

struct RenderTargetView
{
	Descriptor descriptor;
	const GpuImage* image = nullptr;
};

RenderTargetView alloc_rtv(const GraphicsDevice* device,
                           DescriptorHeap* heap,
                           const GpuImage* image);

struct DepthStencilView
{
	Descriptor descriptor;
	const GpuImage* image = nullptr;
};

DepthStencilView alloc_dsv(const GraphicsDevice* device,
                           DescriptorHeap* heap,
                           const GpuImage* image);

struct BufferSrv
{
	Descriptor descriptor;
	const GpuBuffer* buffer = nullptr;
	u32 first_element = 0;
	u32 num_elements = 0;
	u32 stride = 0;
};

BufferSrv alloc_buffer_srv(const GraphicsDevice* device,
                           DescriptorHeap* heap,
                           const GpuBuffer* buffer,
                           u32 first_element,
                           u32 num_elements,
                           u32 stride);

struct BufferCbv
{
	Descriptor descriptor;
	const GpuBuffer* buffer = nullptr;
	u64 offset = 0;
	u32 size = 0;
};

BufferCbv alloc_buffer_cbv(const GraphicsDevice* device,
                           DescriptorHeap* heap,
                           const GpuBuffer* buffer,
                           u64 offset,
                           u32 size);

struct GpuShader
{
	ID3DBlob* d3d12_shader = nullptr;
};

GpuShader load_shader_from_file(const GraphicsDevice* device, const wchar_t* path);
void destroy_shader(GpuShader* shader);

typedef u64 GraphicsPipelineHash;
struct GraphicsPipelineDesc
{
	GpuShader vertex_shader;
	Option<GpuShader> pixel_shader = None;
	Option<D3D12_COMPARISON_FUNC> comparison_func = None;
	bool stencil_enable;

	DXGI_FORMAT rtv_formats[8];
	u8 num_render_targets = 0;
	Option<DXGI_FORMAT> dsv_format = None;
};

GraphicsPipelineHash hash_pipeline_desc(GraphicsPipelineDesc desc);

struct GraphicsPipeline
{
	ID3D12PipelineState* d3d12_pso = nullptr;
	GraphicsPipelineHash hash = 0;
};
GraphicsPipeline init_graphics_pipeline(const GraphicsDevice* device,
                                        GraphicsPipelineDesc desc,
                                        const wchar_t* name);
void destroy_graphics_pipeline(GraphicsPipeline* pipeline);

struct SwapChain
{
	u32 width = 0;
	u32 height = 0;
	DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;

	IDXGISwapChain4* d3d12_swap_chain = nullptr;
	Fence fence;
	FenceValue frame_fence_values[FRAMES_IN_FLIGHT] = {0};

	GpuImage* back_buffers[FRAMES_IN_FLIGHT] = {0};
	u32 back_buffer_index = 0;
	RenderTargetView back_buffer_views[FRAMES_IN_FLIGHT] = {0};

	GpuImage* depth_buffer;
	DepthStencilView depth_stencil_view;

	DescriptorHeap render_target_view_heap;
	DescriptorHeap depth_stencil_view_heap;

	bool vsync = true;
	bool tearing_supported = false;
	bool fullscreen = false;
};

SwapChain init_swap_chain(MEMORY_ARENA_PARAM, HWND window, const GraphicsDevice* device);
void destroy_swap_chain(SwapChain* swap_chain);

RenderTargetView* swap_chain_acquire(SwapChain* swap_chain);
void swap_chain_submit(SwapChain* swap_chain, const GraphicsDevice* device, RenderTargetView* rtv);

void cmd_image_transition(CmdList* cmd,
                          const GpuImage* image,
                          D3D12_RESOURCE_STATES before,
                          D3D12_RESOURCE_STATES after);
void cmd_clear_rtv(CmdList* cmd, RenderTargetView* rtv, Vec4 clear_color);
void cmd_clear_dsv(CmdList* cmd,
                   D3D12_CLEAR_FLAGS flags,
                   DepthStencilView* dsv,
                   f32 depth,
                   u8 stencil);
void cmd_set_viewport(CmdList* cmd, f32 top, f32 left, f32 width, f32 height);
void cmd_set_scissor(CmdList* cmd,
                     u32 left = 0,
                     u32 top = 0,
                     u32 right = LONG_MAX,
                     u32 bottom = LONG_MAX);
void cmd_set_render_targets(CmdList* cmd,
                            const RenderTargetView* render_targets,
                            u8 num_render_targets,
                            Option<DepthStencilView> dsv);
void cmd_set_descriptor_heaps(CmdList* cmd, const DescriptorHeap* heaps, u32 num_heaps);
void cmd_set_pipeline(CmdList* cmd, const GraphicsPipeline* pipeline);
void cmd_set_index_buffer(CmdList* cmd, const GpuBuffer* buffer, u32 start_index, u32 num_indices);
void cmd_set_primitive_topology(CmdList* cmd);
void cmd_set_graphics_root_signature(CmdList* cmd);
void cmd_set_graphics_32bit_constants(CmdList* cmd, const void* data);
void cmd_draw_indexed(CmdList* cmd, u32 num_indices);
void cmd_draw(CmdList* cmd, u32 num_vertices);

