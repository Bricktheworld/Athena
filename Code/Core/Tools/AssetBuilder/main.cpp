#include "Core/Foundation/types.h"
#include "Core/Foundation/context.h"

#include "Core/Tools/AssetBuilder/model_importer.h"
#include "Core/Tools/AssetBuilder/texture_importer.h"

#include <d3d12.h>
#include <dxgidebug.h>
#include <dxgi1_6.h>
#include <wrl.h>

static AllocHeap g_InitHeap;
static FreeHeap  g_OSHeap;

static check_return bool
build_asset(const char* model_path, const char* project_root)
{
  asset_builder::ImportedModel     imported_model;

  asset_builder::ImportedMaterial* imported_materials      = nullptr;
  u32                              imported_material_count = 0;

  bool res = asset_builder::import_model(
    g_InitHeap,
    model_path,
    project_root,
    &imported_model,
    &imported_materials,
    &imported_material_count
  );

  if (!res)
  {
    printf("Failed to import model!\n");
    return false;
  }

  res = asset_builder::write_model_to_asset(project_root, imported_model);
  if (!res)
  {
    printf("Failed to write model to asset!\n");
    return false;
  }

  for (u32 imaterial = 0; imaterial < imported_material_count; imaterial++)
  {
    const asset_builder::ImportedMaterial* mat = imported_materials + imaterial;
    res = asset_builder::write_material_to_asset(project_root, *mat);
    if (!res)
    {
      printf("Failed to write material to asset!\n");
      return false;
    }
  }

  u32 texture_allocator_size = MiB(512);
  LinearAllocator texture_allocator = init_linear_allocator(g_OSHeap, texture_allocator_size);

  ID3D12Device* device = nullptr;
  HASSERT(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)));

  defer { COM_RELEASE(device); };

  for (u32 imaterial = 0; imaterial < imported_material_count; imaterial++)
  {
    const asset_builder::ImportedMaterial* mat = imported_materials + imaterial;
    for (u32 itexture = 0; itexture < mat->num_textures; itexture++)
    {
      reset_linear_allocator(&texture_allocator);
      const char* texture_path = mat->texture_paths[itexture];
      if (texture_path[0] == 0)
      {
        printf("Skipping empty texture slot!\n");
        continue;
      }

      asset_builder::ImportedTexture imported_texture;
      res = asset_builder::import_texture(texture_allocator, texture_path, project_root, &imported_texture);
      if (!res)
      {
        printf("Failed to import texture!\n");
        return false;
      }

      res = asset_builder::write_texture_to_asset(device, project_root, imported_texture);
      if (!res)
      {
        printf("Failed to write texture to asset!\n");
        return false;
      }
    }
  }
  destroy_linear_allocator(&texture_allocator);

  return true;
}


// AssetBuilder.exe <input_path> <project_root_dir>
int main(int argc, const char** argv)
{
  static constexpr size_t kInitHeapSize = MiB(128);

  if (argc != 3)
  {
    printf("Invalid arguments!\n");
    printf("AssetBuilder.exe <input_path> <project_root>\n");
    return 1;
  }

  argv++;
  argc--;

  const char* input_path   = argv[0];
  const char* project_root = argv[1];


  OSAllocator     os_allocator   = init_os_allocator();

  g_OSHeap                       = os_allocator;

  u8* init_memory                = HEAP_ALLOC(u8, g_OSHeap, kInitHeapSize);
  LinearAllocator init_allocator = init_linear_allocator(init_memory, kInitHeapSize);

  g_InitHeap                     = init_allocator;

  init_context(g_InitHeap, g_OSHeap);

  bool res = build_asset(input_path, project_root);
  if (!res)
  {
    printf("Asset builder failed!\n");
    return 1;
  }

  return 0;
}

