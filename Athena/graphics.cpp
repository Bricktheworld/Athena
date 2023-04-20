#include "memory/memory.h"
#include "graphics.h"
#include <windows.h>
#include <d3d11_1.h>
#include <d3dcompiler.h>
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

GraphicsDevice init_graphics_device(HWND window)
{
	GraphicsDevice res = { 0 };

	RECT client_rect;
	GetClientRect(window, &client_rect);
	u32 client_width = client_rect.right - client_rect.left;
	u32 client_height = client_rect.bottom - client_rect.top;

	res.viewport.Width = static_cast<f32>(client_width);
	res.viewport.Height = static_cast<f32>(client_height);
	res.viewport.TopLeftX = 0.0f;
	res.viewport.TopLeftY = 0.0f;
	res.viewport.MinDepth = 0.0f;
	res.viewport.MaxDepth = 1.0f;

	DXGI_SWAP_CHAIN_DESC swapchain_desc = {0};
	swapchain_desc.BufferCount = 1;
	swapchain_desc.BufferDesc.Width = client_width;
	swapchain_desc.BufferDesc.Height = client_height;
	swapchain_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapchain_desc.BufferDesc.RefreshRate = {0, 1};
	swapchain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapchain_desc.OutputWindow = window;
	swapchain_desc.SampleDesc.Count = 1;
	swapchain_desc.SampleDesc.Quality = 0;
	swapchain_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
	swapchain_desc.Windowed = TRUE;

	UINT create_flags = 0;
#ifdef DEBUG
	create_flags = D3D11_CREATE_DEVICE_DEBUG;
#endif

	D3D_FEATURE_LEVEL feature_levels[] = 
	{
		D3D_FEATURE_LEVEL_11_1,
		D3D_FEATURE_LEVEL_11_0,
		D3D_FEATURE_LEVEL_10_1,
		D3D_FEATURE_LEVEL_10_0,
		D3D_FEATURE_LEVEL_9_3,
		D3D_FEATURE_LEVEL_9_2,
		D3D_FEATURE_LEVEL_9_1,
	};

	HRESULT hres = D3D11CreateDeviceAndSwapChain(nullptr,
	                                            D3D_DRIVER_TYPE_HARDWARE,
	                                            nullptr,
	                                            create_flags,
	                                            feature_levels,
	                                            ARRAY_LENGTH(feature_levels),
	                                            D3D11_SDK_VERSION,
	                                            &swapchain_desc,
	                                            &res.swap_chain,
	                                            &res.dev,
	                                            &res.feature_level,
	                                            &res.ctx);
	ASSERT(!FAILED(hres));

	ID3D11Texture2D* back_buffer = nullptr;
	hres = res.swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)&back_buffer);
	ASSERT(!FAILED(hres));

	hres = res.dev->CreateRenderTargetView(back_buffer, nullptr, &res.render_target_view);

	ASSERT(!FAILED(hres));

	back_buffer->Release();

	D3D11_TEXTURE2D_DESC depth_stencil_buffer_desc = { 0 };
	depth_stencil_buffer_desc.ArraySize = 1;
	depth_stencil_buffer_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
	depth_stencil_buffer_desc.CPUAccessFlags = 0;
	depth_stencil_buffer_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depth_stencil_buffer_desc.Width = client_width;
	depth_stencil_buffer_desc.Height = client_height;
	depth_stencil_buffer_desc.MipLevels = 1;
	depth_stencil_buffer_desc.SampleDesc.Count = 1;
	depth_stencil_buffer_desc.SampleDesc.Quality = 0;
	depth_stencil_buffer_desc.Usage = D3D11_USAGE_DEFAULT;

	hres = res.dev->CreateTexture2D(&depth_stencil_buffer_desc, nullptr, &res.depth_stencil_buffer);
	ASSERT(!FAILED(hres));

	hres = res.dev->CreateDepthStencilView(res.depth_stencil_buffer, nullptr, &res.depth_stencil_view);
	ASSERT(!FAILED(hres));

	res.ctx->RSSetViewports(1, &res.viewport);

	D3D11_RASTERIZER_DESC rasterizer_desc = { 0 };
	rasterizer_desc.AntialiasedLineEnable = FALSE;
	rasterizer_desc.CullMode = D3D11_CULL_NONE;
	rasterizer_desc.DepthBias = 0;
	rasterizer_desc.DepthBiasClamp = 0.0f;
	rasterizer_desc.DepthClipEnable = TRUE;
	rasterizer_desc.FillMode = D3D11_FILL_SOLID;
	rasterizer_desc.FrontCounterClockwise = FALSE;
	rasterizer_desc.MultisampleEnable = FALSE;
	rasterizer_desc.ScissorEnable = FALSE;
	rasterizer_desc.SlopeScaledDepthBias = 0.0f;

	hres = res.dev->CreateRasterizerState(&rasterizer_desc, &res.raster_state);
	ASSERT(!FAILED(hres));

	D3D11_DEPTH_STENCIL_DESC depth_stencil_desc = { 0 };
	depth_stencil_desc.DepthEnable = TRUE;
	depth_stencil_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	depth_stencil_desc.DepthFunc = D3D11_COMPARISON_GREATER;
	depth_stencil_desc.StencilEnable = FALSE;

	hres = res.dev->CreateDepthStencilState(&depth_stencil_desc, &res.depth_stencil_state);
	ASSERT(!FAILED(hres));


	D3D11_BUFFER_DESC vertex_buffer_desc = { 0 };

	vertex_buffer_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
	vertex_buffer_desc.ByteWidth = sizeof(VERTICES);
	vertex_buffer_desc.CPUAccessFlags = 0;
	vertex_buffer_desc.Usage = D3D11_USAGE_DEFAULT;

	D3D11_SUBRESOURCE_DATA resource_data = { 0 };
	resource_data.pSysMem = VERTICES;

	hres = res.dev->CreateBuffer(&vertex_buffer_desc, &resource_data, &res.vertex_buffer);
	ASSERT(!FAILED(hres));


	D3D11_BUFFER_DESC index_buffer_desc = { 0 };
	index_buffer_desc.BindFlags = D3D11_BIND_INDEX_BUFFER;
	index_buffer_desc.ByteWidth = sizeof(INDICES);
	index_buffer_desc.CPUAccessFlags = 0;
	index_buffer_desc.Usage = D3D11_USAGE_DEFAULT;

	resource_data.pSysMem = INDICES;
	hres = res.dev->CreateBuffer(&index_buffer_desc, &resource_data, &res.index_buffer);
	ASSERT(!FAILED(hres));

	D3D11_BUFFER_DESC cbuffer_desc = { 0 };

	cbuffer_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	cbuffer_desc.ByteWidth = sizeof(CBuffer);
	cbuffer_desc.CPUAccessFlags = 0;
	cbuffer_desc.Usage = D3D11_USAGE_DEFAULT;

	hres = res.dev->CreateBuffer(&cbuffer_desc, nullptr, &res.cbuffer);
	ASSERT(!FAILED(hres));

#ifdef DEBUG
	// TODO(Brandon): Aboslutely don't do this...
	const wchar_t* vs_src = L"C:\\Users\\Brand\\Dev\\Athena\\x64\\Debug\\test_vs_d.cso";
	const wchar_t* ps_src = L"C:\\Users\\Brand\\Dev\\Athena\\x64\\Debug\\test_ps_d.cso";
#else
	const wchar_t* vs_src = L"test_vs.cso";
	const wchar_t* ps_src = L"test_ps.cso";
#endif

	ID3DBlob* vs_blob = nullptr;
	hres = D3DReadFileToBlob(vs_src, &vs_blob);
	ASSERT(!FAILED(hres));

	hres = res.dev->CreateVertexShader(vs_blob->GetBufferPointer(),
	                                   vs_blob->GetBufferSize(),
	                                   nullptr,
	                                   &res.vertex_shader);
	ASSERT(!FAILED(hres));

	D3D11_INPUT_ELEMENT_DESC layout_desc[] =
	{
		{"POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex, position), D3D11_INPUT_PER_VERTEX_DATA, 0},
		{"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, offsetof(Vertex, color), D3D11_INPUT_PER_VERTEX_DATA, 0},
	};

	hres = res.dev->CreateInputLayout(layout_desc,
	                                  ARRAY_LENGTH(layout_desc),
	                                  vs_blob->GetBufferPointer(),
	                                  vs_blob->GetBufferSize(),
	                                  &res.input_layout);
	ASSERT(!FAILED(hres));

	vs_blob->Release();

	ID3DBlob* ps_blob = nullptr;
	hres = D3DReadFileToBlob(ps_src, &ps_blob);
	ASSERT(!FAILED(hres));

	hres = res.dev->CreatePixelShader(ps_blob->GetBufferPointer(),
	                                  ps_blob->GetBufferSize(),
	                                  nullptr,
	                                  &res.pixel_shader);
	ASSERT(!FAILED(hres));
	ps_blob->Release();

	res.proj = perspective_infinite_reverse_lh(PI / 4.0f,
	                                           static_cast<f32>(client_width) / static_cast<f32>(client_height),
	                                           0.1f);

	return res;
}

void destroy_graphics_device(GraphicsDevice* d)
{
	d->input_layout->Release();

	d->vertex_shader->Release();
	d->pixel_shader->Release();

	d->vertex_buffer->Release();
	d->index_buffer->Release();
	d->cbuffer->Release();

	d->raster_state->Release();
	d->depth_stencil_state->Release();

	d->depth_stencil_buffer->Release();

	d->depth_stencil_view->Release();
	d->render_target_view->Release();

	d->swap_chain->Release();
	d->ctx->Release();
	d->dev->Release();

	zero_memory(d, sizeof(GraphicsDevice));
}

void gd_update(GraphicsDevice* d)
{

	auto* ctx = d->ctx;

	Rgba clear_color(0.0, 0.0, 1.0, 1.0);
	ctx->ClearRenderTargetView(d->render_target_view, (f32*)&clear_color);
	ctx->ClearDepthStencilView(d->depth_stencil_view, D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 0.0f, 0);

	const u32 vertex_stride = sizeof(Vertex);
	const u32 offset = 0;
	ctx->IASetVertexBuffers(0, 1, &d->vertex_buffer, &vertex_stride, &offset);
	ctx->IASetInputLayout(d->input_layout);
	ctx->IASetIndexBuffer(d->index_buffer, DXGI_FORMAT_R16_UINT, 0);
	ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	CBuffer constants;
	constants.projection = d->proj;
	constants.view = look_at_lh(Vec3(0.0, 0.0, -10.0), Vec3(0.0, 0.0, 1.0), Vec3(0.0, 1.0, 0.0));
	constants.model = Mat4();
	ctx->UpdateSubresource(d->cbuffer, 0, nullptr, &constants, 0, 0);

	ctx->VSSetShader(d->vertex_shader, nullptr, 0);
	ctx->VSSetConstantBuffers(0, 1, &d->cbuffer);

	ctx->RSSetState(d->raster_state);
	ctx->RSSetViewports(1, &d->viewport);

	ctx->PSSetShader(d->pixel_shader, nullptr, 0);
	ctx->OMSetRenderTargets(1, &d->render_target_view, d->depth_stencil_view);
	ctx->OMSetDepthStencilState(d->depth_stencil_state, 1);

	ctx->DrawIndexed(ARRAY_LENGTH(INDICES), 0, 0);
}
void gd_present(GraphicsDevice* d)
{
	d->swap_chain->Present(0, 0);
}
