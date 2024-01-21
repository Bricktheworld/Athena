#pragma once
#include "Core/Engine/Render/render_graph.h"

struct GBuffer
{
  RgHandle<GpuTexture> material_id;
  RgHandle<GpuTexture> world_pos;
  RgHandle<GpuTexture> diffuse_metallic;
  RgHandle<GpuTexture> normal_roughness;
  RgHandle<GpuTexture> depth;
};

struct ReadGBuffer
{
  RgReadHandle<GpuTexture> material_id;
  RgReadHandle<GpuTexture> world_pos;
  RgReadHandle<GpuTexture> diffuse_metallic;
  RgReadHandle<GpuTexture> normal_roughness;
  RgReadHandle<GpuTexture> depth;
};

constant u32 kGBufferReadCount = 5;

GBuffer     init_gbuffer(RgBuilder* builder);
void        init_gbuffer_static(AllocHeap heap, RgBuilder* builder, GBuffer* gbuffer);

ReadGBuffer read_gbuffer(RgPassBuilder* pass_builder, const GBuffer& gbuffer, ReadTextureAccessMask access);

