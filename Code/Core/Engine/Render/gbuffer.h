#pragma once
#include "Core/Engine/Render/render_graph.h"

struct GBuffer
{
  RgHandle<GpuTexture> material_id;
  RgHandle<GpuTexture> diffuse_metallic;
  RgHandle<GpuTexture> normal_roughness;
  RgHandle<GpuTexture> velocity;
  RgHandle<GpuTexture> depth;
};

struct ReadGBuffer
{
  RgTexture2D<uint>   material_id;
  RgTexture2D<float4> diffuse_metallic;
  RgTexture2D<float4> normal_roughness;
  RgTexture2D<float2> velocity;
  RgTexture2D<float>  depth;
};

constant u32 kGBufferReadCount = 5;

GBuffer     init_gbuffer(RgBuilder* builder);
void        init_gbuffer_static(AllocHeap heap, RgBuilder* builder, GBuffer* gbuffer);

ReadGBuffer read_gbuffer(RgPassBuilder* pass_builder, const GBuffer& gbuffer);

