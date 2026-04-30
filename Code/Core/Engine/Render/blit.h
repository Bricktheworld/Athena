#pragma once
#include "Core/Engine/Render/renderer.h"

struct BlitEntry
{
  RenderTarget* src         = nullptr;
  GpuTexture*   back_buffer = nullptr;
};

void render_handler_back_buffer_blit(const RenderEntry* entry, u32);
