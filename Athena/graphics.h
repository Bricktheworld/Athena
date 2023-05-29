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

Fence init_fence(GraphicsDevice* device);
void destroy_fence(Fence* fence);
void yield_for_fence_value(Fence* fence, FenceValue value);

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

CmdQueue init_cmd_queue(GraphicsDevice* device, CmdListType type);
void destroy_cmd_queue(CmdQueue* queue);

void cmd_queue_gpu_wait_for_fence(CmdQueue* queue, Fence* fence, FenceValue value);
FenceValue cmd_queue_signal(CmdQueue* queue, Fence* fence);

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
                                         GraphicsDevice* device,
                                         CmdQueue* queue,
                                         u16 pool_size);
void destroy_cmd_list_allocator(CmdListAllocator* allocator);
CmdList alloc_cmd_list(CmdListAllocator* allocator);
void submit_cmd_list(CmdListAllocator* allocator, CmdList* list);

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

void wait_for_device_idle(GraphicsDevice* device);

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

GpuResourceHeap init_gpu_resource_heap(GraphicsDevice* device,
                                       u64 size,
                                       GpuHeapType type);
void destroy_gpu_resource_heap(GpuResourceHeap* heap);

enum MemoryLocation : u8
{
	MEMORY_LOCATION_GPU_PRIVATE,
	MEMORY_LOCATION_GPU_SHARED,
	MEMORY_LOCATION_CPU_SHARED,

	MEMORY_LOCATION_COUNT,
};

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

GpuImage alloc_image_2D_no_heap(GraphicsDevice* device,
                                GpuImageDesc desc,
                                const wchar_t* name);
void free_image(GpuImage* image);

struct GpuBufferDesc
{
	u64 size = 0;
	MemoryLocation memory_location = MEMORY_LOCATION_COUNT;
	u64 alignment = 0;
};

struct GpuBuffer
{
	GpuBufferDesc desc;
	ID3D12Resource* resource = nullptr;
};

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
                                    GraphicsDevice* device,
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

RenderTargetView alloc_rtv(GraphicsDevice* device,
                           DescriptorHeap* heap,
                           const GpuImage* image);

struct DepthStencilView
{
	Descriptor descriptor;
	const GpuImage* image = nullptr;
};

DepthStencilView alloc_dsv(GraphicsDevice* device,
                           DescriptorHeap* heap,
                           const GpuImage* image);

struct GpuShader
{
	ID3DBlob* d3d12_shader = nullptr;
};

GpuShader load_shader_from_file(GraphicsDevice* device, const wchar_t* path);
void destroy_shader(GpuShader* shader);

typedef u64 GraphicsPipelineHash;
struct GraphicsPipelineDesc
{
	GpuShader vertex_shader;
	Option<GpuShader> pixel_shader = None;
	Option<D3D12_COMPARISON_FUNC> comparison_func = None;
	bool stencil_enable;

	Array<RenderTargetView> render_targets;
	Option<DepthStencilView> depth_stencil_view;
};

GraphicsPipelineHash hash_pipeline_desc(GraphicsPipelineDesc desc);

struct GraphicsPipeline
{
	ID3D12PipelineState* d3d12_pso = nullptr;
	GraphicsPipelineHash hash = 0;
};
GraphicsPipeline init_graphics_pipeline(GraphicsDevice* device,
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

SwapChain init_swap_chain(MEMORY_ARENA_PARAM, HWND window, GraphicsDevice* device);
void destroy_swap_chain(SwapChain* swap_chain);

RenderTargetView* swap_chain_acquire(SwapChain* swap_chain);
void swap_chain_submit(SwapChain* swap_chain, GraphicsDevice* device, RenderTargetView* rtv);

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
                            const Array<RenderTargetView> render_targets,
                            DepthStencilView dsv);
void cmd_set_descriptor_heaps(CmdList* cmd, const DescriptorHeap* heaps, u32 num_heaps);


#if 0
struct CBuffer
{
	Mat4 projection;
	Mat4 view;
	Mat4 model;
};

typedef u64 FenceValue;

struct CmdAllocatorPool
{
	ID3D12CommandAllocator** free_allocators = nullptr;
	u32 free_allocator_count = 0;
	ID3D12CommandAllocator** submitted_allocators = nullptr;
	u32 submitted_allocator_count = 0;
};

struct Fence
{
	ID3D12Fence* fence = nullptr;
	u64 value = 0;
};

struct GraphicsDevice
{
	Mat4 proj;

	ID3D12Device2* dev = nullptr;
	ID3D12CommandQueue* cmd_queue = nullptr;
	IDXGISwapChain4* swap_chain = nullptr;
	ID3D12GraphicsCommandList* cmd_list = nullptr;
	ID3D12CommandAllocator* cmd_allocators[FRAMES_IN_FLIGHT] = {0};

	ID3D12DescriptorHeap* rtv_descriptor_heap = nullptr;
	u32 rtv_descriptor_size = 0;

	ID3D12Resource* back_buffers[FRAMES_IN_FLIGHT] = {0};
	u32 back_buffer_index = 0;

	ID3D12Fence* fence = nullptr;
	u64 fence_value = 0;
	u64 frame_fence_values[FRAMES_IN_FLIGHT] = {0};
	HANDLE fence_event;

	bool vsync = true;
	bool tearing_supported = false;
	bool fullscreen = false;

	ID3D12Resource* depth_buffer = nullptr;
	ID3D12DescriptorHeap* dsv_heap  = nullptr;

	ID3D12Resource* vertex_buffer = nullptr;
	D3D12_VERTEX_BUFFER_VIEW vertex_buffer_view = {0};

	ID3D12Resource* index_buffer = nullptr;
	D3D12_INDEX_BUFFER_VIEW index_buffer_view = {0};

	ID3D12RootSignature* root_signature = nullptr;
	ID3D12PipelineState* pipeline_state = nullptr;

	D3D12_VIEWPORT viewport = {0};
	D3D12_RECT scissor_rect = {0};
};


// Should only be accessed by one thread.
struct UploadContext
{
	ID3D12Device2* dev = nullptr;
	CmdAllocatorPool cmd_allocator_pool = {0};
};

UploadContext init_upload_context(MEMORY_ARENA_PARAM, GraphicsDevice* d, u32 max_allocators);
void destroy_upload_context(UploadContext* upload_context);

struct UploadBuffer
{
	ID3D12Resource* resource = nullptr;
	D3D12_GPU_VIRTUAL_ADDRESS gpu_addr = 0;
	size_t size = 0;
	void* mapped = nullptr;
};

UploadBuffer alloc_upload_buffer(GraphicsDevice* dev, size_t size);
void free_upload_buffer(UploadBuffer* upload_buffer);

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
#endif

