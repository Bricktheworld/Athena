#pragma once
#include "Core/Engine/Render/render_graph.h"

#include "Core/Engine/Render/ddgi.h"

#include "Core/Engine/Shaders/Include/ddgi_common.hlsli"

struct DiffuseGiResources
{
  RgHandle<GpuTexture> probe_page_table;
  RgHandle<GpuBuffer>  probe_buffer;
};

struct ReadDiffuseGi
{
  RgStructuredBuffer<DiffuseGiProbe>    diffuse_probes;
  RgTexture2DArray<u32>                 page_table;
};

DiffuseGiResources init_rt_diffuse_gi(AllocHeap heap, RgBuilder* builder);
ReadDiffuseGi      read_diffuse_gi(RgPassBuilder* pass, const DiffuseGiResources& resources);
