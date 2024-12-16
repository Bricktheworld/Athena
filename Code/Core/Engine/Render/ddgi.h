#pragma once
#include "Core/Engine/Render/render_graph.h"

#include "Core/Engine/Render/ddgi.h"

struct Ddgi
{
  RgHandle<GpuBuffer>  desc;
  RgHandle<GpuTexture> irradiance;
};

struct ReadDdgi
{
  RgConstantBuffer<DDGIVolDesc> desc;
  RgTexture2DArray<float4>      irradiance;
};

Ddgi     init_ddgi(AllocHeap heap, RgBuilder* builder);
ReadDdgi read_ddgi(RgPassBuilder* pass_builder, const Ddgi& ddgi);

