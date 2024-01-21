#pragma once
#include "Core/Engine/Render/render_graph.h"

struct GBuffer
{
  RgHandle<GpuTexture> material_id;
  RgHandle<GpuTexture> diffuse_metallic;
  RgHandle<GpuTexture> normal_roughness;
  RgHandle<GpuTexture> depth;
};

struct ReadGBuffer
{
  RgReadHandle<GpuTexture> material_id;
  RgReadHandle<GpuTexture> diffuse_metallic;
  RgReadHandle<GpuTexture> normal_roughness;
  RgReadHandle<GpuTexture> depth;
};

constant u32 kGBufferReadCount = 4;

GBuffer     init_gbuffer(RgBuilder* builder);
void        init_gbuffer_static(AllocHeap heap, RgBuilder* builder, GBuffer* gbuffer);

ReadGBuffer read_gbuffer(RgPassBuilder* pass_builder, const GBuffer& gbuffer, ReadTextureAccessMask access);

