#include "Core/Engine/memory.h"

#include "Core/Engine/material_manager.h"

MaterialManager* g_MaterialManager = nullptr;

static constexpr u32 kMaxMaterialUploads = 128;

void
init_material_manager(void)
{
  g_MaterialManager = HEAP_ALLOC(MaterialManager, g_InitHeap, 1);
  zero_memory(g_MaterialManager, sizeof(MaterialManager));

  // 0 gets reserved for invalid GPU ID
  g_MaterialManager->next_gpu_id = 1;
  g_MaterialManager->material_upload_count = 0;
  g_MaterialManager->material_uploads      = HEAP_ALLOC(MaterialUploadCmd, g_InitHeap, kMaxMaterialUploads);
}

void
destroy_material_manager(void)
{
}

THREAD_SAFE u32
alloc_gpu_material(void)
{
  spin_acquire(&g_MaterialManager->spin_lock);
  defer { spin_release(&g_MaterialManager->spin_lock); };
  return g_MaterialManager->next_gpu_id++;
}

THREAD_SAFE bool
upload_gpu_material(u32 gpu_id, const MaterialGpu& mat)
{
  spin_acquire(&g_MaterialManager->spin_lock);
  defer { spin_release(&g_MaterialManager->spin_lock); };

  if (g_MaterialManager->material_upload_count >= kMaxMaterialUploads)
  {
    return false;
  }

  MaterialUploadCmd* dst = g_MaterialManager->material_uploads + g_MaterialManager->material_upload_count++;

  dst->material          = mat;
  dst->mat_gpu_id        = gpu_id;

  return true;
}
