#include "Core/Foundation/memory.h"

#include "Core/Tools/AssetBuilder/material_importer.h"

void
asset_builder::dump_imported_material(ImportedMaterial material)
{
  dbgln("Material: %s", material.path);
  dbgln("  Textures: %u", material.num_textures);
  for (u32 itexture = 0; itexture < material.num_textures; itexture++)
  {
    dbgln("  Texture[%lu]: %s", itexture, material.texture_paths[itexture]);
  }
}

check_return bool 
asset_builder::write_material_to_asset(const char* project_root, const ImportedMaterial& material)
{
  size_t output_size = sizeof(MaterialAsset) + sizeof(AssetRef<TextureAsset>) * material.num_textures;

  u8* buffer = HEAP_ALLOC(u8, GLOBAL_HEAP, output_size);
  defer { HEAP_FREE(GLOBAL_HEAP, buffer); };
  u32 offset = 0;


  MaterialAsset material_asset = {0};
  material_asset.metadata.magic_number    = kAssetMagicNumber;
  material_asset.metadata.version         = kMaterialAssetVersion;
  material_asset.metadata.asset_type      = AssetType::kMaterial,
  material_asset.metadata.asset_hash      = material.hash;
  material_asset.num_textures             = material.num_textures;
  material_asset.textures                 = sizeof(MaterialAsset);

  memcpy(buffer + offset, &material_asset, sizeof(MaterialAsset)); offset += sizeof(MaterialAsset);

  for (u32 itexture = 0; itexture < material.num_textures; itexture++)
  {
    AssetRef<TextureAsset> texture = kNullAssetId;
    if (material.texture_paths[itexture] != 0)
    {
      texture = path_to_asset_id(material.texture_paths[itexture]);
    }
    memcpy(buffer + offset, &texture, sizeof(texture)); offset += sizeof(texture);
  }

  char built_path[kMaxPathLength]{0};
  snprintf(built_path, sizeof(built_path), "%s/Assets/Built/0x%08x.built", project_root, material.hash);
  printf("Writing material asset file %s to %s...\n", material.path, built_path);

  auto new_file = create_file(built_path, FileCreateFlags::kCreateTruncateExisting);
  if (!new_file)
  {
    printf("Failed to create output file!\n");
    return false;
  }

  defer { close_file(&new_file.value()); };

  if (!write_file(new_file.value(), buffer, output_size))
  {
    printf("Failed to write output file!\n");
    return false;
  }

  return true;
}
