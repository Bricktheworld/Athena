#pragma once
#include "Core/Foundation/types.h"
#include "Core/Foundation/math.h"
#include "Core/Foundation/memory.h"
#include "Core/Foundation/filesystem.h"
#include "Core/Foundation/assets.h"

namespace asset_builder
{      
  struct ImportedMaterial
  {
    // NOTE(Brandon): We're not setting a shader here because I think that's something that the model
    // importer won't be able to know how to handle... we're gonna hard-code it to a pre-defined PBR
    // shader I think...
    u32    num_textures;
    char** texture_paths;
  };
  
  struct ImportedModelSubset
  {
    u32              num_vertices;
    u32              num_indices;

    VertexAsset*     vertices;
    u32*             indices;

    ImportedMaterial material;
    Aabb3d           aabb;
  };
  
  struct ImportedModel
  {
    ImportedModelSubset* model_subsets;
    u32                  num_model_subsets;
  };
  
  check_return bool import_model(
    AllocHeap heap,
    const char* path,
    const char* project_root,
    ImportedModel* out_imported_model,
    AssetId* out_asset_id
  );

  void dump_imported_model(ImportedModel model);

  check_return bool write_model_to_asset(
    AssetId asset_id,
    const char* project_root,
    const ImportedModel& model
  );
}

