#include "Core/Foundation/types.h"
#include "Core/Foundation/context.h"

#include "Core/Tools/AssetBuilder/model_importer.h"

static AllocHeap g_InitHeap;
static FreeHeap  g_OSHeap;

static check_return bool
build_asset(const char* input_path, const char* project_root)
{
  asset_builder::ImportedModel imported_model;
  AssetId asset_id;
  bool res = asset_builder::import_model(g_InitHeap, input_path, project_root, &imported_model, &asset_id);
  if (!res)
  {
    return false;
  }

  res = asset_builder::write_model_to_asset(asset_id, project_root, imported_model);
  if (!res)
  {
    return false;
  }

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

