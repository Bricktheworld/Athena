#pragma once
#include "Core/Engine/Render/render_graph.h"

struct GBufferStaticParams
{
};

struct GBuffer
{
  RgHandle<GpuImage> material_id;
  RgHandle<GpuImage> world_pos;
  RgHandle<GpuImage> diffuse_metallic;
  RgHandle<GpuImage> normal_roughness;
  RgHandle<GpuImage> depth;
};

struct ReadGBuffer
{
  RgReadHandle<GpuImage> material_id;
  RgReadHandle<GpuImage> world_pos;
  RgReadHandle<GpuImage> diffuse_metallic;
  RgReadHandle<GpuImage> normal_roughness;
  RgReadHandle<GpuImage> depth;
};

constant u32 kGBufferReadCount = 5;

GBuffer     init_gbuffer(RgBuilder* builder);
void        init_gbuffer_static(AllocHeap heap, RgBuilder* builder, GBuffer* gbuffer);
void        render_handler_gbuffer_static(RenderContext* render_context, const void* data);

ReadGBuffer read_gbuffer(RgPassBuilder* pass_builder, const GBuffer& gbuffer, ReadTextureAccessMask access);

