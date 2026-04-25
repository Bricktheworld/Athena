#include "Core/Tools/AssetBuilder/model_importer.h"

#include "Core/Tools/AssetBuilder/Vendor/assimp/Importer.hpp"
#include "Core/Tools/AssetBuilder/Vendor/assimp/scene.h"
#include "Core/Tools/AssetBuilder/Vendor/assimp/postprocess.h"

#include "Core/Tools/AssetBuilder/Vendor/meshoptimizer/meshoptimizer.h"

DONT_IGNORE_RETURN bool
asset_builder::import_model(
  AllocHeap heap,
  const char* path,
  const char* project_root,
  ImportedModel* out_imported_model,
  ImportedMaterial** out_materials,
  u32* out_material_count
) {
  AssetId asset_id = path_to_asset_id(path);
  char full_path[512];
  snprintf(full_path, 512, "%s/%s", project_root, path);
  printf("Importing model with assimp (it is normal for this to take a second depending on how big the model is)...\n");

  Assimp::Importer importer;
  // Set the maximum number of indices to fit in a U16
  importer.SetPropertyInteger(AI_CONFIG_PP_SLM_TRIANGLE_LIMIT, (U16_MAX - 1) / 0x3);
  const aiScene* assimp_model = importer.ReadFile(
    full_path,
    aiProcess_CalcTangentSpace      |
    aiProcess_Triangulate           |
    aiProcess_JoinIdenticalVertices |
    aiProcess_SortByPType           |
    aiProcess_SplitLargeMeshes      | // Need this to handle large meshes that overflow u16
    aiProcess_PreTransformVertices    // TODO(Brandon): We're gonna delete this and replace with prefab system
  );

  if (assimp_model == nullptr)
  {
    printf("Failed to import scene through assimp!\n");
    return false;
  }

  if (assimp_model->mNumTextures > 0)
  {
    printf("Embedded textures not supported: ignoring.\n");
  }

  if (assimp_model->mNumCameras > 0)
  {
    // TODO(Brandon): Import cameras!
    printf("Found %u cameras in the scene\n", assimp_model->mNumCameras);
  }

  if (assimp_model->mNumLights > 0)
  {
    printf("Found %u lights in the scene\n", assimp_model->mNumLights);
  }

  printf("%u subsets in model\n",   assimp_model->mNumMeshes);
  printf("%u materials in model\n", assimp_model->mNumMaterials);

  u32               material_count = assimp_model->mNumMaterials;
  ImportedMaterial* materials      = HEAP_ALLOC(ImportedMaterial, heap, material_count);


  char parent_dir[kMaxPathLength];
  memcpy(parent_dir, path, kMaxPathLength);

  printf("%s\n", path);
  parent_dir[get_parent_dir(parent_dir, (u32)strlen(parent_dir))] = 0;
  printf("%s\n", parent_dir);

  for (u32 imaterial = 0; imaterial < assimp_model->mNumMaterials; imaterial++)
  {
    aiMaterial* assimp_material = assimp_model->mMaterials[imaterial];
    aiString    material_name   = assimp_material->GetName();

    snprintf(materials[imaterial].path, kMaxPathLength, "%s/%u.material", path, imaterial);
    printf("  Material[%u] %s\n", imaterial, materials[imaterial].path);

    materials[imaterial].hash         = path_to_asset_id(materials[imaterial].path);
    // This is hard coded for now where invalid slots will just be null asset ptrs.
    materials[imaterial].num_textures = 5;

    zero_memory(materials[imaterial].texture_paths, sizeof(materials[imaterial].texture_paths));

    aiColor4D base_color;
    if (assimp_material->Get(AI_MATKEY_COLOR_DIFFUSE, base_color) == AI_SUCCESS)
    {
      materials[imaterial].diffuse_base = Vec4(base_color.r, base_color.g, base_color.b, base_color.a);
    }
    else
    {
      materials[imaterial].diffuse_base = Vec4(1.0f);
    }

    // Kinda hacky, but trying to "coerce" assimp to give me the closest thing to base color
    aiTextureType kDiffuseTextureTypes[] = {aiTextureType_BASE_COLOR, aiTextureType_DIFFUSE};
    aiTextureType diffuse_texture_type = aiTextureType_BASE_COLOR;
    for (u32 i = 0; i < ARRAY_LENGTH(kDiffuseTextureTypes); i++)
    {
      if (assimp_material->GetTextureCount(kDiffuseTextureTypes[i]) > 0)
      {
        diffuse_texture_type = kDiffuseTextureTypes[i];
        break;
      }
    }

    u32 num_diffuse_textures = assimp_material->GetTextureCount(diffuse_texture_type);
    if (num_diffuse_textures > 0)
    {
      aiString texture_path;
      assimp_material->GetTexture(diffuse_texture_type, 0, &texture_path);
      snprintf(materials[imaterial].texture_paths[0], kMaxPathLength, "%s%s", parent_dir, texture_path.C_Str());
      printf("      Diffuse: %s\n", materials[imaterial].texture_paths[0]);
    }

    u32 num_normal_textures = assimp_material->GetTextureCount(aiTextureType_NORMALS);
    if (num_normal_textures > 0)
    {
      aiString texture_path;
      assimp_material->GetTexture(aiTextureType_NORMALS, 0, &texture_path);
      snprintf(materials[imaterial].texture_paths[1], kMaxPathLength, "%s%s", parent_dir, texture_path.C_Str());
      printf("      Normal: %s\n", materials[imaterial].texture_paths[1]);
    }

    u32 num_roughness_textures = assimp_material->GetTextureCount(aiTextureType_DIFFUSE_ROUGHNESS);
    if (num_roughness_textures > 0)
    {
      aiString texture_path;
      assimp_material->GetTexture(aiTextureType_DIFFUSE_ROUGHNESS, 0, &texture_path);
      snprintf(materials[imaterial].texture_paths[2], kMaxPathLength, "%s%s", parent_dir, texture_path.C_Str());
      printf("      Roughness: %s\n", materials[imaterial].texture_paths[2]);
    }

    u32 num_metalness_textures = assimp_material->GetTextureCount(aiTextureType_METALNESS);
    if (num_metalness_textures > 0)
    {
      aiString texture_path;
      assimp_material->GetTexture(aiTextureType_METALNESS, 0, &texture_path);
      snprintf(materials[imaterial].texture_paths[3], kMaxPathLength, "%s%s", parent_dir, texture_path.C_Str());
      printf("      Metalness: %s\n", materials[imaterial].texture_paths[3]);
    }

    u32 num_ao_textures = assimp_material->GetTextureCount(aiTextureType_AMBIENT_OCCLUSION);
    if (num_ao_textures > 0)
    {
      aiString texture_path;
      assimp_material->GetTexture(aiTextureType_AMBIENT_OCCLUSION, 0, &texture_path);
      snprintf(materials[imaterial].texture_paths[4], kMaxPathLength, "%s%s", parent_dir, texture_path.C_Str());
      printf("      Ambient Occlusion: %s\n", materials[imaterial].texture_paths[4]);
    }
  }

  // TODO(bshihabi): Make this configurable
  static constexpr u32 kModelLodCount = 4;

  u64 path_len = strlen(path);

  ImportedModel imported_model     = {0};
  imported_model.hash              = asset_id;
  imported_model.lod_count         = kModelLodCount;
  memcpy(imported_model.path, path, path_len + 1);

  imported_model.num_model_subsets = assimp_model->mNumMeshes;
  imported_model.model_subsets     = HEAP_ALLOC(ImportedModelSubset, heap, imported_model.num_model_subsets);

  for (u32 imesh = 0; imesh < assimp_model->mNumMeshes; imesh++)
  {
    aiMesh* assimp_mesh  = assimp_model->mMeshes[imesh];
    u32     num_vertices = assimp_mesh->mNumVertices;
    u32     num_indices  = assimp_mesh->mNumFaces * 3;

    // Too many triangles in one single model subset! You should split it up :)
    if (num_indices >= U16_MAX - 1)
    {
      printf("Found %u indices in model subset %u which is greater than the supported maximum of %u. Consider breaking up the model subset.\n", num_indices, imesh, U16_MAX - 1);
      continue;
    }

    ImportedModelSubset* model_subset = imported_model.model_subsets + imesh;
    model_subset->lods                = HEAP_ALLOC(ImportedModelSubsetLod, heap, imported_model.lod_count);

    struct UncompressedVertex
    {
      Vec3 position;
      Vec3 normal;
      Vec2 uv;
    };

    UncompressedVertex* uncompressed_vertices = HEAP_ALLOC(UncompressedVertex, GLOBAL_HEAP, num_vertices);
    u16*                indices               = HEAP_ALLOC(u16,                GLOBAL_HEAP, num_indices );

    const aiVector3D kAssimpZero3D(0.0f, 0.0f, 0.0f);
    for (u32 ivertex = 0; ivertex < assimp_mesh->mNumVertices; ivertex++)
    {
      const aiVector3D* position = assimp_mesh->mVertices + ivertex;
      const aiVector3D* normal   = assimp_mesh->mNormals  + ivertex;
      const aiVector3D* uv       = assimp_mesh->HasTextureCoords(0) ?
                                   assimp_mesh->mTextureCoords[0] + ivertex :
                                   &kAssimpZero3D;
      
      uncompressed_vertices[ivertex].position = Vec3(position->x, position->y, position->z);
      uncompressed_vertices[ivertex].normal   = Vec3(normal->x, normal->y, normal->z);
      uncompressed_vertices[ivertex].uv       = Vec2(uv->x, uv->y);
    }

    u32 iindex = 0;
    for (u32 iface = 0; iface < assimp_mesh->mNumFaces; iface++)
    {
      const aiFace* face = assimp_mesh->mFaces + iface;
      if (face->mNumIndices != 3)
      {
        printf("Found face with %u indices, skipping!\n", face->mNumIndices);
        continue;
      }

      indices[iindex + 0] = (u16)face->mIndices[0];
      indices[iindex + 1] = (u16)face->mIndices[1];
      indices[iindex + 2] = (u16)face->mIndices[2];
      iindex += 3;
    }
    num_indices = iindex;

    meshopt_Bounds bounds = meshopt_computeSphereBounds(&uncompressed_vertices[0].position.x, num_vertices, sizeof(UncompressedVertex), nullptr, 0);
    Vec3           center = Vec3(bounds.center[0], bounds.center[1], bounds.center[2]);
    f32            radius = bounds.radius;

    for (u32 ilod = 0; ilod < imported_model.lod_count; ilod++)
    {
      UncompressedVertex* simplified_uncompressed_vertices = HEAP_ALLOC(UncompressedVertex, GLOBAL_HEAP, num_vertices);
      u16*                simplified_indices               = HEAP_ALLOC(u16,                GLOBAL_HEAP, num_indices);

      defer { HEAP_FREE(GLOBAL_HEAP, simplified_uncompressed_vertices); };

      u32                 simplified_num_vertices = num_vertices;
      u32                 simplified_num_indices  = num_indices;
      // Simplify each LoD recursively based on the previous one
      if (ilod > 0)
      {
        // Bogus numbers I stole from the meshoptimizer demo. I'm sure we can improve these.
        f32 threshold          = powf(0.7, (f32)ilod);
        u32 target_index_count = (u32)(num_indices * threshold) / 3 * 3;
        f32 target_error       = 1e-2f;
        f32 result_error       = 0.0f;
        simplified_num_indices = (u32)meshopt_simplify<u16>(simplified_indices, indices, num_indices, &uncompressed_vertices[0].position.x, num_vertices, sizeof(UncompressedVertex), target_index_count, target_error, 0, &result_error);
      }
      else
      {
        memcpy(simplified_indices, indices, num_indices * sizeof(u16));
      }

      meshopt_optimizeVertexCache<u16>(simplified_indices, simplified_indices, simplified_num_indices, num_vertices);
      meshopt_optimizeOverdraw<u16>(simplified_indices, simplified_indices, simplified_num_indices, &uncompressed_vertices[0].position.x, num_vertices, sizeof(UncompressedVertex), 1.05f);
      simplified_num_vertices = (u32)meshopt_optimizeVertexFetch<u16>(simplified_uncompressed_vertices, simplified_indices, simplified_num_indices, uncompressed_vertices, num_vertices, sizeof(UncompressedVertex));

      VertexAsset* vertices = HEAP_ALLOC(VertexAsset, GLOBAL_HEAP, simplified_num_vertices);

      for (u32 ivertex = 0; ivertex < simplified_num_vertices; ivertex++)
      {
        Vec3    uncompressed_pos   = simplified_uncompressed_vertices[ivertex].position;
        Vec3    offset             = uncompressed_pos - center;
        Vec3    scaled_offset      = offset / radius;
        Vec3s16 quantized_position = Vec3s16(f32_to_snorm16(scaled_offset.x), f32_to_snorm16(scaled_offset.y), f32_to_snorm16(scaled_offset.z));

        Vec2    uncompressed_uv    = simplified_uncompressed_vertices[ivertex].uv;
        f32     uv_scale           = MAX(MAX(fabs(uncompressed_uv.x), fabs(uncompressed_uv.y)), 1.0f);
        Vec2    scaled_uvs         = uncompressed_uv / uv_scale;

        f16     quantized_uv_scale = f32_to_f16(uv_scale);

        vertices[ivertex].position = Vec4s16(quantized_position, *(s16*)&quantized_uv_scale);
        vertices[ivertex].normal   = simplified_uncompressed_vertices[ivertex].normal;
        vertices[ivertex].uv       = Vec2s16(f32_to_snorm16(scaled_uvs.x), f32_to_snorm16(scaled_uvs.y));
      }

      ImportedModelSubsetLod* lod = model_subset->lods + ilod;
      lod->num_vertices = simplified_num_vertices;
      lod->num_indices  = simplified_num_indices;
      lod->vertices     = vertices;
      lod->indices      = simplified_indices;
    }

    model_subset->material     = materials[assimp_mesh->mMaterialIndex].hash;
    model_subset->center       = center;
    model_subset->radius       = radius;
  }

  ASSERT_MSG_FATAL(path_to_asset_id(imported_model.path) == imported_model.hash, "Imported model path and hash do not match!");
  *out_imported_model = imported_model;
  *out_materials      = materials;
  *out_material_count = material_count;

  return true;
}

DONT_IGNORE_RETURN bool 
asset_builder::write_model_to_asset(const char* project_root, const ImportedModel& model)
{
  u64 total_vertex_count = 0;
  u64 total_index_count  = 0;
  for (u32 imodel_subset = 0; imodel_subset < model.num_model_subsets; imodel_subset++)
  {
    const ImportedModelSubset* subset = &model.model_subsets[imodel_subset];
    for (u32 ilod = 0; ilod < model.lod_count; ilod++)
    {
      const ImportedModelSubsetLod* lod = &subset->lods[ilod];
      total_vertex_count += lod->num_vertices;
      total_index_count  += lod->num_indices;
    }
  }

  size_t model_subsets_size = (sizeof(ModelAsset::ModelSubset) + sizeof(ModelAsset::ModelSubsetLod) * model.lod_count) * model.num_model_subsets;

  u64    vertices_size      = sizeof(VertexAsset) * total_vertex_count ;
  u64    indices_size       = sizeof(u16)         * total_index_count;
  size_t output_size        = sizeof(ModelAsset)  +
                              model_subsets_size  +
                              vertices_size       +
                              indices_size;


  u8* buffer = HEAP_ALLOC(u8, GLOBAL_HEAP, output_size);
  defer { HEAP_FREE(GLOBAL_HEAP, buffer); };

  u8* dst    = buffer;

  ModelAsset* model_asset = (ModelAsset*)ALLOC_OFF(dst, sizeof(ModelAsset));
  model_asset->metadata.magic_number    = kAssetMagicNumber;
  model_asset->metadata.version         = kModelAssetVersion;
  model_asset->metadata.asset_type      = AssetType::kModel,
  model_asset->metadata.asset_hash      = model.hash;
  model_asset->num_model_subsets        = model.num_model_subsets;
  model_asset->lod_count                = model.lod_count;
  model_asset->vertices_size            = vertices_size;
  model_asset->indices_size             = indices_size;

  auto* dst_subsets  = ALLOC_OFF(dst, sizeof(ModelAsset::ModelSubset   ) * model.num_model_subsets);
  auto* dst_lods     = ALLOC_OFF(dst, sizeof(ModelAsset::ModelSubsetLod) * model.num_model_subsets * model.lod_count);
  auto* dst_vertices = ALLOC_OFF(dst, sizeof(VertexAsset) * total_vertex_count);
  auto* dst_indices  = ALLOC_OFF(dst, sizeof(u16)         * total_index_count);

  model_asset->model_subsets = dst_subsets - buffer;
  model_asset->vertices      = dst_vertices - buffer;
  model_asset->indices       = dst_indices  - buffer;
  for (u32 imodel_subset = 0; imodel_subset < model.num_model_subsets; imodel_subset++)
  {
    const ImportedModelSubset* imported_model_subset = model.model_subsets + imodel_subset;
    auto* model_subset     = (ModelAsset::ModelSubset*)ALLOC_OFF(dst_subsets, sizeof(ModelAsset::ModelSubset));

    model_subset->material = imported_model_subset->material;
    model_subset->center   = imported_model_subset->center;
    model_subset->radius   = imported_model_subset->radius;
    model_subset->lods     = dst_lods - buffer;
    for (u32 ilod = 0; ilod < model.lod_count; ilod++)
    {
      const ImportedModelSubsetLod* imported_lod = imported_model_subset->lods + ilod;
      auto* lod         = (ModelAsset::ModelSubsetLod*)ALLOC_OFF(dst_lods, sizeof(ModelAsset::ModelSubsetLod));
      lod->num_vertices = imported_lod->num_vertices;
      lod->num_indices  = imported_lod->num_indices;

      auto* vertices    = ALLOC_OFF(dst_vertices, sizeof(VertexAsset) * lod->num_vertices);
      auto* indices     = ALLOC_OFF(dst_indices,  sizeof(u16)         * lod->num_indices );

      lod->vertices     = vertices - buffer;
      lod->indices      = indices  - buffer;

      memcpy(vertices, imported_lod->vertices, sizeof(VertexAsset) * lod->num_vertices);
      memcpy(indices,  imported_lod->indices,  sizeof(u16)         * lod->num_indices );
    }
  }

  char built_path[512]{0};
  snprintf(built_path, sizeof(built_path), "%s/Assets/Built/0x%08x.built", project_root, model.hash);
  printf("Writing model asset file to %s...\n", built_path);

  auto new_file = create_file(built_path, FileCreateFlags::kCreateTruncateExisting);
  if (!new_file)
  {
    printf("Failed to create output file!\n");
    return false;
  }

  defer { close_file(&new_file.value()); };

  ASSERT_MSG_FATAL((u64)(dst - buffer) == output_size, "Mismatched output size and written size! Expected %llu bytes but got %llu bytes", output_size, dst - buffer);
  if (!write_file(new_file.value(), buffer, output_size))
  {
    printf("Failed to write output file!\n");
    return false;
  }

  return true;
}

void
asset_builder::free_imported_model(ImportedModel* imported_model)
{
  for (u32 imodel_subset = 0; imodel_subset < imported_model->num_model_subsets; imodel_subset++)
  {
    ImportedModelSubset* imported_model_subset = imported_model->model_subsets + imodel_subset;
    for (u32 ilod = 0; ilod < imported_model->lod_count; ilod++)
    {
      ImportedModelSubsetLod* lod = imported_model_subset->lods + ilod;
      HEAP_FREE(GLOBAL_HEAP, lod->indices);
      HEAP_FREE(GLOBAL_HEAP, lod->vertices);
    }
  }
}
