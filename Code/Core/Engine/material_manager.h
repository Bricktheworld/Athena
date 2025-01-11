#pragma once
#include "Core/Foundation/memory.h"
#include "Core/Foundation/threading.h"

#include "Core/Engine/Render/graphics.h"
#include "Core/Engine/Render/render_graph.h"

struct MaterialManager;
extern MaterialManager* g_MaterialManager;


struct MaterialManager
{
  SpinLock                 spin_lock;

  MaterialUploadCmd*       material_uploads;
  u32                      material_upload_count = 0;

  u32                      next_gpu_id           = 0;
};

void init_material_manager(void);
void destroy_material_manager(void);
THREAD_SAFE u32  alloc_gpu_material(void);

THREAD_SAFE bool upload_gpu_material(u32 gpu_id, const MaterialGpu& mat);

