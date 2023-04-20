#pragma once
#include "types.h"
#include "math/math.h"
#include <d3d11_1.h>

typedef Vec4 Rgba;
typedef Vec3 Rgb;

struct CBuffer
{
	Mat4 projection;
	Mat4 view;
	Mat4 model;
};

struct GraphicsDevice
{
	ID3D11Device* dev = nullptr;
	ID3D11DeviceContext* ctx = nullptr;
	IDXGISwapChain* swap_chain = nullptr;

	ID3D11Texture2D* depth_stencil_buffer = nullptr;

	ID3D11RenderTargetView* render_target_view = nullptr;
	ID3D11DepthStencilView* depth_stencil_view = nullptr;

	D3D11_VIEWPORT viewport = { 0 };
	ID3D11DepthStencilState* depth_stencil_state = nullptr;
	ID3D11RasterizerState* raster_state = nullptr;

	D3D_FEATURE_LEVEL feature_level;

	ID3D11Buffer* vertex_buffer = nullptr;
	ID3D11Buffer* index_buffer = nullptr;

	ID3D11VertexShader* vertex_shader = nullptr;
	ID3D11PixelShader* pixel_shader = nullptr;

	ID3D11Buffer* cbuffer = nullptr;

	ID3D11InputLayout* input_layout = nullptr;

	Mat4 proj;
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

