#pragma once
#include "Core/Foundation/types.h"
#include "Core/Foundation/math.h"
#include "Core/Foundation/memory.h"
#include "Core/Foundation/filesystem.h"
#include "Core/Foundation/assets.h"

#include "Core/Tools/AssetBuilder/material_importer.h"

namespace asset_builder
{
  struct ImportedModelSubsetLod
  {
    u32          num_vertices;
    u32          num_indices;

    VertexAsset* vertices;
    u16*         indices;

    f32          error;
  };

  struct ImportedModelSubset
  {
    AssetId                 material;

    ImportedModelSubsetLod* lods;

    // For the bounding sphere
    Vec3                    center;
    f32                     radius;
  };
  
  struct ImportedModel
  {
    AssetId              hash;
    char                 path[kMaxPathLength];

    ImportedModelSubset* model_subsets;
    u32                  num_model_subsets;

    u32                  lod_count;
  };
  
  DONT_IGNORE_RETURN bool import_model(
    AllocHeap heap,
    const char* path,
    const char* project_root,
    ImportedModel* out_imported_model,
    ImportedMaterial** out_materials,
    u32* out_material_count
  );

  void free_imported_model(ImportedModel* imported_model);

  DONT_IGNORE_RETURN bool write_model_to_asset(
    const char* project_root,
    const ImportedModel& model
  );
}

