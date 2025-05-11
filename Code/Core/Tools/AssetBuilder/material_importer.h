#pragma once
#include "Core/Foundation/types.h"
#include "Core/Foundation/assets.h"

namespace asset_builder
{
  static constexpr u32 kMaxTextures = 32;

  struct ImportedMaterial
  {
    AssetId hash;
    char    path[kMaxPathLength];

    Vec4    diffuse_base;
    // NOTE(Brandon): We're not setting a shader here because I think that's something that the model
    // importer won't be able to know how to handle... we're gonna hard-code it to a pre-defined PBR
    // shader I think...
    u32     num_textures;
    char    texture_paths[kMaxTextures][kMaxPathLength];
  };

  void dump_imported_material(ImportedMaterial material);

  DONT_IGNORE_RETURN bool write_material_to_asset(
    const char* project_root,
    const ImportedMaterial& material
  );
}