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

  u64 path_len = strlen(path);

  ImportedModel imported_model     = {0};
  imported_model.hash              = asset_id;
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

    meshopt_optimizeVertexCache<u16>(indices, indices, num_indices, num_vertices);
    meshopt_optimizeOverdraw<u16>(indices, indices, num_indices, &uncompressed_vertices[0].position.x, num_vertices, sizeof(UncompressedVertex), 1.05f);
    num_vertices = (u32)meshopt_optimizeVertexFetch<u16>(uncompressed_vertices, indices, num_indices, uncompressed_vertices, num_vertices, sizeof(UncompressedVertex));

    meshopt_Bounds bounds = meshopt_computeSphereBounds(&uncompressed_vertices[0].position.x, num_vertices, sizeof(UncompressedVertex), nullptr, 0);

    VertexAsset* vertices = HEAP_ALLOC(VertexAsset, GLOBAL_HEAP, num_vertices);

    Vec3 center = Vec3(bounds.center[0], bounds.center[1], bounds.center[2]);
    f32  radius = bounds.radius;
    for (u32 ivertex = 0; ivertex < num_vertices; ivertex++)
    {
      Vec3    uncompressed_pos   = uncompressed_vertices[ivertex].position;
      Vec3    offset             = uncompressed_pos - center;
      Vec3    scaled_offset      = offset / radius;
      Vec3s16 quantized_position = Vec3s16(f32_to_snorm16(scaled_offset.x), f32_to_snorm16(scaled_offset.y), f32_to_snorm16(scaled_offset.z));

      vertices[ivertex].position = quantized_position;
      vertices[ivertex].normal   = uncompressed_vertices[ivertex].normal;
      vertices[ivertex].uv       = uncompressed_vertices[ivertex].uv;
    }

    model_subset->num_vertices = num_vertices;
    model_subset->num_indices  = num_indices;
    model_subset->vertices     = vertices;
    model_subset->indices      = indices;
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

void
asset_builder::dump_imported_model(ImportedModel model)
{
  ASSERT_MSG_FATAL(path_to_asset_id(model.path) == model.hash, "Imported model path and hash do not match!");
  dbgln("Model(0x%x): %s", model.hash, model.path);
  dbgln("  Subset: %lu", model.num_model_subsets);
  for (u32 imodel_subset = 0; imodel_subset < model.num_model_subsets; imodel_subset++)
  {
    const ImportedModelSubset* model_subset = model.model_subsets + imodel_subset;
    dbgln(
      "  ModelSubset[%lu]: %lu vertices, %lu indices ",
      imodel_subset,
      model_subset->num_vertices,
      model_subset->num_indices
    );

    for (u32 ivertex = 0; ivertex < model_subset->num_vertices; ivertex++)
    {
      const VertexAsset* vertex = model_subset->vertices + ivertex;
      dbgln(
        "    [%u]{position: (%f,%f,%f), normal: (%f,%f,%f), uv: (%f,%f)}",
        ivertex,
        vertex->position.x,
        vertex->position.y,
        vertex->position.z,
        vertex->normal.x,
        vertex->normal.y,
        vertex->normal.z,
        vertex->uv.x,
        vertex->uv.y
      );
    }

    for (u32 iindex = 0; iindex < model_subset->num_indices; iindex++)
    {
      dbg("%u, ", model_subset->indices[iindex]);
    }
    dbg("\n");
  }
}

DONT_IGNORE_RETURN bool 
asset_builder::write_model_to_asset(const char* project_root, const ImportedModel& model)
{
  u64 total_vertex_count = 0;
  u64 total_index_count  = 0;
  for (u32 imodel_subset = 0; imodel_subset < model.num_model_subsets; imodel_subset++)
  {
    total_vertex_count += model.model_subsets[imodel_subset].num_vertices;
    total_index_count  += model.model_subsets[imodel_subset].num_indices;
  }

  size_t model_subsets_size = sizeof(ModelAsset::ModelSubset) * model.num_model_subsets;

  u64    vertices_size      = sizeof(VertexAsset) * total_vertex_count ;
  u64    indices_size       = sizeof(u16)         * total_index_count;
  size_t output_size        = sizeof(ModelAsset)  +
                              model_subsets_size  +
                              vertices_size       +
                              indices_size;

  u64 vertex_dst           = sizeof(ModelAsset) + model_subsets_size;
  u64 index_dst            = vertex_dst + vertices_size;

  u8* buffer = HEAP_ALLOC(u8, GLOBAL_HEAP, output_size);
  defer { HEAP_FREE(GLOBAL_HEAP, buffer); };
  u32 offset = 0;


  ModelAsset model_asset = {0};
  model_asset.metadata.magic_number    = kAssetMagicNumber;
  model_asset.metadata.version         = kModelAssetVersion;
  model_asset.metadata.asset_type      = AssetType::kModel,
  model_asset.metadata.asset_hash      = model.hash;
  model_asset.num_model_subsets        = model.num_model_subsets;
  model_asset.model_subsets            = sizeof(ModelAsset);
  model_asset.vertices                 = vertex_dst;
  model_asset.indices                  = index_dst;
  model_asset.vertices_size            = vertices_size;
  model_asset.indices_size             = indices_size;

  memcpy(buffer + offset, &model_asset, sizeof(ModelAsset)); offset += sizeof(ModelAsset);


  for (u32 imodel_subset = 0; imodel_subset < model.num_model_subsets; imodel_subset++)
  {
    ImportedModelSubset* imported_model_subset = model.model_subsets + imodel_subset;
    ModelAsset::ModelSubset model_subset       = {0};

    model_subset.num_vertices = imported_model_subset->num_vertices;
    model_subset.num_indices  = imported_model_subset->num_indices;
    model_subset.material     = imported_model_subset->material;
    model_subset.center       = imported_model_subset->center;
    model_subset.radius       = imported_model_subset->radius;

    size_t vertex_size        = sizeof(VertexAsset) * model_subset.num_vertices;
    size_t index_size         = sizeof(u16)         * model_subset.num_indices;

    model_subset.vertices     = vertex_dst; vertex_dst += vertex_size;
    model_subset.indices      = index_dst;  index_dst  += index_size;

    memcpy(buffer + offset, &model_subset, sizeof(model_subset)); offset += sizeof(model_subset);
    memcpy(buffer + model_subset.vertices, imported_model_subset->vertices, vertex_size);
    memcpy(buffer + model_subset.indices,  imported_model_subset->indices,  index_size);
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
    HEAP_FREE(GLOBAL_HEAP, imported_model_subset->indices);
    HEAP_FREE(GLOBAL_HEAP, imported_model_subset->vertices);
  }
}
