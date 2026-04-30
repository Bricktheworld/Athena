#pragma once
#include "Core/Engine/Render/renderer.h"
#include "Core/Engine/Shaders/Include/gbuffer_common.hlsli"

struct GBufferGenerateMultiDrawArgsEntry
{
  u32 phase = 0;
};

struct GBufferOpaqueEntry
{
  bool should_clear_targets = false;
};

void render_handler_gbuffer_generate_multidraw_args(const RenderEntry* entries, u32 entry_count);
void render_handler_gbuffer_opaque(const RenderEntry* entries, u32 entry_count);
void render_handler_generate_hzb  (const RenderEntry* entries, u32 entry_count);
