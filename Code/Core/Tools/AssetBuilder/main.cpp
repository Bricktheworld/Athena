#include "Core/Foundation/types.h"
#include "Core/Foundation/context.h"

#include "Core/Tools/AssetBuilder/model_importer.h"

static check_return bool
build_asset(MEMORY_ARENA_PARAM, const char* input_path, const char* project_root)
{
  asset_builder::ImportedModel imported_model;
  AssetId asset_id;
  bool res = asset_builder::import_model(MEMORY_ARENA_FWD, input_path, project_root, &imported_model, &asset_id);
  if (!res)
  {
    return false;
  }

  res = asset_builder::write_model_to_asset(MEMORY_ARENA_FWD, asset_id, project_root, imported_model);
  if (!res)
  {
    return false;
  }

  return true;
}

// AssetBuilder.exe <input_path> <project_root_dir>
int main(int argc, const char** argv)
{
  static constexpr size_t kHeapSize = MiB(128);

  if (argc != 3)
  {
    printf("Invalid arguments!\n");
    printf("AssetBuilder.exe <input_path> <output_build_directory>\n");
    return 1;
  }

  argv++;
  argc--;

  const char* input_path   = argv[0];
  const char* project_root = argv[1];

  init_application_memory(kHeapSize);
  defer { destroy_application_memory(); };

  MemoryArena arena         = alloc_memory_arena(kHeapSize);
  MemoryArena scratch_arena = sub_alloc_memory_arena(&arena, DEFAULT_SCRATCH_SIZE);
  init_context(scratch_arena);

  bool res = build_asset(&arena, input_path, project_root);
  if (!res)
  {
    printf("Asset builder failed!\n");
    return 1;
  }

  return 0;
}

