#include "Core/Foundation/context.h"

#include "Core/Tools/AssetBuilder/model_importer.h"

#include "Core/Tools/AssetBuilder/Vendor/assimp/Importer.hpp"
#include "Core/Tools/AssetBuilder/Vendor/assimp/scene.h"
#include "Core/Tools/AssetBuilder/Vendor/assimp/postprocess.h"

static constexpr u32 kModelAssetVersion = 1;

check_return bool
asset_builder::import_model(
  AllocHeap heap,
  const char* path,
  const char* project_root,
  ImportedModel* out_imported_model,
  AssetId* out_asset_id
) {
  AssetId asset_id = path_to_asset_id(path); // crc32(path, strlen(path));
  char full_path[512];
  snprintf(full_path, 512, "%s/%s", project_root, path);

  Assimp::Importer importer;
  const aiScene* assimp_model = importer.ReadFile(
    full_path,
    aiProcess_CalcTangentSpace      |
    aiProcess_Triangulate           |
    aiProcess_JoinIdenticalVertices |
    aiProcess_SortByPType           |
    aiProcess_PreTransformVertices  | // TODO(Brandon): We're gonna delete this soon and replace with prefab system
    aiProcess_GenBoundingBoxes
  );

  if (assimp_model == nullptr)
  {
    printf("Failed to import scene throguh assimp!\n");
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

  printf("%u mesh instances in model\n", assimp_model->mNumMeshes);
  printf("%u materials in model\n", assimp_model->mNumMaterials);

  for (u32 imaterial = 0; imaterial < assimp_model->mNumMaterials; imaterial++)
  {
    aiMaterial* assimp_material = assimp_model->mMaterials[imaterial];
    aiString material_name = assimp_material->GetName();
    printf("\tMaterial[%u] %s\n", imaterial, material_name.C_Str());

//    u32 num_textures = assimp_material->GetTextureCount();
//    printf("\t\tTextures: %lu\n", assimp_material->GetTextureCount());
  }

  ImportedModel imported_model = {0};
  imported_model.num_mesh_insts = assimp_model->mNumMeshes;

  imported_model.mesh_insts = HEAP_ALLOC(ImportedMeshInst, heap, imported_model.num_mesh_insts);

  for (u32 imesh = 0; imesh < assimp_model->mNumMeshes; imesh++)
  {
    aiMesh* assimp_mesh = assimp_model->mMeshes[imesh];
    u32 num_vertices = assimp_mesh->mNumVertices;
    u32 num_indices = assimp_mesh->mNumFaces * 3;

    ImportedMeshInst* mesh_inst = imported_model.mesh_insts + imesh;

    Vertex* vertices = HEAP_ALLOC(Vertex, heap, num_vertices);
    u32*    indices  = HEAP_ALLOC(u32,    heap, num_indices );


    const aiVector3D kAssimpZero3D(0.0f, 0.0f, 0.0f);
    for (u32 ivertex = 0; ivertex < assimp_mesh->mNumVertices; ivertex++)
    {
      const aiVector3D* position = assimp_mesh->mVertices + ivertex;
      const aiVector3D* normal   = assimp_mesh->mNormals  + ivertex;
      const aiVector3D* uv       = assimp_mesh->HasTextureCoords(0) ?
                                   assimp_mesh->mTextureCoords[0] + ivertex :
                                   &kAssimpZero3D;
      
      vertices[ivertex].position = Vec3(position->x, position->y, position->z);
      vertices[ivertex].normal   = Vec3(normal->x, normal->y, normal->z);
      vertices[ivertex].uv       = Vec2(uv->x, uv->y);
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

      indices[iindex + 0] = face->mIndices[0];
      indices[iindex + 1] = face->mIndices[1];
      indices[iindex + 2] = face->mIndices[2];
      iindex += 3;
    }
    num_indices = iindex;

    mesh_inst->num_vertices = num_vertices;
    mesh_inst->num_indices  = num_indices;
    mesh_inst->vertices     = vertices;
    mesh_inst->indices      = indices;

    // TODO(Brandon): We'll need to figure out how to link these correctly...
    mesh_inst->material.num_textures  = 0;
    mesh_inst->material.texture_paths = nullptr;

    const aiAABB* aabb  = &assimp_mesh->mAABB;
    mesh_inst->aabb.min = Vec3(aabb->mMin.x, aabb->mMin.y, aabb->mMin.z);
    mesh_inst->aabb.max = Vec3(aabb->mMax.x, aabb->mMax.y, aabb->mMax.z);
  }

  *out_asset_id       = asset_id;
  *out_imported_model = imported_model;

  return true;
}

void
asset_builder::dump_imported_model(ImportedModel model)
{
  dbgln("Model: %lu mesh insts", model.num_mesh_insts);
  for (u32 imesh_inst = 0; imesh_inst < model.num_mesh_insts; imesh_inst++)
  {
    const ImportedMeshInst* mesh_inst = model.mesh_insts + imesh_inst;
    dbgln(
      "\tMeshInst[%lu]: %lu vertices, %lu indices ",
      imesh_inst,
      mesh_inst->num_vertices,
      mesh_inst->num_indices
    );

    for (u32 ivertex = 0; ivertex < mesh_inst->num_vertices; ivertex++)
    {
      const Vertex* vertex = mesh_inst->vertices + ivertex;
      dbgln(
        "\t\t[%u]{position: (%f,%f,%f), normal: (%f,%f,%f), uv: (%f,%f)}",
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

    for (u32 iindex = 0; iindex < mesh_inst->num_indices; iindex++)
    {
      dbg("%u, ", mesh_inst->indices[iindex]);
    }
    dbg("\n");
  }
}

check_return bool 
asset_builder::write_model_to_asset(AssetId asset_id, const char* project_root, const ImportedModel& model)
{
  ScratchAllocator scratch_arena = alloc_scratch_arena();
  defer { free_scratch_arena(&scratch_arena); };
  u64 total_vertex_count = 0;
  u64 total_index_count  = 0;
  for (u32 imesh_inst = 0; imesh_inst < model.num_mesh_insts; imesh_inst++)
  {
    total_vertex_count += model.mesh_insts[imesh_inst].num_vertices;
    total_index_count  += model.mesh_insts[imesh_inst].num_indices;
  }

  size_t mesh_inst_size = sizeof(ModelAsset::MeshInst) * model.num_mesh_insts;

  size_t output_size    = sizeof(ModelAsset)                      +
                          mesh_inst_size                          +
                          sizeof(Vertex)     * total_vertex_count +
                          sizeof(u32)        * total_index_count;

  u8* buffer = HEAP_ALLOC(u8, scratch_arena, output_size);
  u32 offset = 0;


  ModelAsset model_asset = {0};
  model_asset.metadata.magic_number = kAssetMagicNumber;
  model_asset.metadata.version      = kModelAssetVersion;
  model_asset.metadata.asset_type   = AssetType::kModel,
  model_asset.metadata.asset_hash   = asset_id;
  model_asset.num_mesh_insts        = model.num_mesh_insts;
  model_asset.mesh_insts            = sizeof(ModelAsset);

  memcpy(buffer + offset, &model_asset, sizeof(ModelAsset)); offset += sizeof(ModelAsset);

  u64 vertex_index_dst = sizeof(ModelAsset) + mesh_inst_size;

  for (u32 imesh_inst = 0; imesh_inst < model.num_mesh_insts; imesh_inst++)
  {
    ImportedMeshInst* imported_mesh_inst = model.mesh_insts + imesh_inst;
    ModelAsset::MeshInst mesh_inst       = {0};

    mesh_inst.num_vertices = imported_mesh_inst->num_vertices;
    mesh_inst.num_indices  = imported_mesh_inst->num_indices;
    mesh_inst.material     = 0;

    size_t vertex_size     = sizeof(Vertex) * mesh_inst.num_vertices;
    size_t index_size      = sizeof(u32)    * mesh_inst.num_indices;

    mesh_inst.vertices     = vertex_index_dst; vertex_index_dst += vertex_size;
    mesh_inst.indices      = vertex_index_dst; vertex_index_dst += index_size;

    memcpy(buffer + offset, &mesh_inst, sizeof(mesh_inst)); offset += sizeof(mesh_inst);
    memcpy(buffer + mesh_inst.vertices, imported_mesh_inst->vertices, vertex_size);
    memcpy(buffer + mesh_inst.indices,  imported_mesh_inst->indices,  index_size);
  }

  char built_path[512]{0};
  snprintf(built_path, sizeof(built_path), "%s/Assets/Built/0x%08x.built", project_root, asset_id);
  printf("Writing model asset file to %s...\n", built_path);

  fs::FileStream new_file = fs::create_file(built_path, fs::FileCreateFlags::kCreateTruncateExisting);
  defer { fs::close_file(&new_file); };

  if (!fs::write_file(new_file, buffer, output_size))
  {
    printf("Failed to write output file!\n");
    return false;
  }

  return true;
}
