#include "Core/Foundation/types.h"
#include "Core/Tools/AssetBuilder/model_importer.h"
#include "Core/Foundation/context.h"

#ifdef _MSC_VER
#pragma warning(disable:4244)
#pragma warning(disable:4305)
#endif

#include <pxr/base/tf/weakBase.h>
#include <pxr/base/tf/weakPtr.h>
#include <pxr/usd/usd/notice.h>
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>
#include <pxr/usd/usdGeom/xformable.h>
#include <pxr/usd/usdGeom/xform.h>
#include <pxr/usd/usdGeom/mesh.h>

PXR_NAMESPACE_USING_DIRECTIVE

static AllocHeap g_InitHeap;

using namespace asset_builder;


DONT_IGNORE_RETURN bool
write_usd_model_to_asset(const char* project_root, const ImportedModel& model)
{
  ScratchAllocator scratch_arena = alloc_scratch_arena();
  defer{ free_scratch_arena(&scratch_arena); };
  u64 total_vertex_count = 0;
  u64 total_index_count = 0;
  for (u32 imodel_subset = 0; imodel_subset < model.num_model_subsets; imodel_subset++)
  {
    total_vertex_count += model.model_subsets[imodel_subset].num_vertices;
    total_index_count += model.model_subsets[imodel_subset].num_indices;
  }

  size_t model_subsets_size = sizeof(ModelAsset::ModelSubset) * model.num_model_subsets;

  size_t output_size = sizeof(ModelAsset) +
    model_subsets_size +
    sizeof(VertexAsset) * total_vertex_count +
    sizeof(u32) * total_index_count;

  u8* buffer = HEAP_ALLOC(u8, scratch_arena, output_size);
  u32 offset = 0;


  ModelAsset model_asset = { 0 };
  model_asset.metadata.magic_number = kAssetMagicNumber;
  model_asset.metadata.version = 1;
  model_asset.metadata.asset_type = AssetType::kModel,
    model_asset.metadata.asset_hash = model.hash;
  model_asset.num_model_subsets = model.num_model_subsets;
  model_asset.model_subsets = sizeof(ModelAsset);

  memcpy(buffer + offset, &model_asset, sizeof(ModelAsset)); offset += sizeof(ModelAsset);

  u64 vertex_index_dst = sizeof(ModelAsset) + model_subsets_size;

  for (u32 imodel_subset = 0; imodel_subset < model.num_model_subsets; imodel_subset++)
  {
    ImportedModelSubset* imported_model_subset = model.model_subsets + imodel_subset;
    ModelAsset::ModelSubset model_subset = { 0 };

    model_subset.num_vertices = imported_model_subset->num_vertices;
    model_subset.num_indices = imported_model_subset->num_indices;
    model_subset.material = imported_model_subset->material;

    size_t vertex_size = sizeof(VertexAsset) * model_subset.num_vertices;
    size_t index_size = sizeof(u32) * model_subset.num_indices;

    model_subset.vertices = vertex_index_dst; vertex_index_dst += vertex_size;
    model_subset.indices = vertex_index_dst; vertex_index_dst += index_size;

    memcpy(buffer + offset, &model_subset, sizeof(model_subset)); offset += sizeof(model_subset);
    memcpy(buffer + model_subset.vertices, imported_model_subset->vertices, vertex_size);
    memcpy(buffer + model_subset.indices, imported_model_subset->indices, index_size);
  }

  char built_path[512]{ 0 };
  snprintf(built_path, sizeof(built_path), "%s/Assets/Built/0x%08x.built", project_root, model.hash);
  printf("Writing model asset file to %s...\n", built_path);

  auto new_file = create_file(built_path, FileCreateFlags::kCreateTruncateExisting);
  if (!new_file)
  {
    printf("Failed to create output file!\n");
    return false;
  }

  defer{ close_file(&new_file.value()); };

  if (!write_file(new_file.value(), buffer, output_size))
  {
    printf("Failed to write output file!\n");
    return false;
  }

  return true;
}

bool load_single_model(std::string const& filePath, std::string const& relativePath, std::string const& projectRoot)
{
  UsdStageRefPtr loadedStage = UsdStage::Open(filePath);

  if (loadedStage)
  {
    for (const UsdPrim& p : loadedStage->Traverse())
    {
      if (p.IsA<UsdGeomMesh>()) // Check if we are loading in a model that has a transform node
      {
        UsdGeomMesh mesh = UsdGeomMesh(p);
        
        VtArray<int> faceVertexCounts;
        if (mesh.GetFaceVertexCountsAttr().Get(&faceVertexCounts))
        {
          bool flag = false;
          for (int x : faceVertexCounts)
          {
            if (x != 3)
            {
              flag = true;
              break;
            }
          }
          if (flag) // Mesh has a face that isn't a triangle, abort
          {
            printf("ERROR: Model isn't fully triangulated\n");
            return false;
          }
        }
        asset_builder::ImportedModel usd_model;
        asset_builder::ImportedModelSubset usd_mesh;
        usd_model.hash = path_to_asset_id(relativePath.c_str());
        relativePath.copy(usd_model.path, relativePath.size());
        usd_model.num_model_subsets = 1;
        usd_model.model_subsets = &usd_mesh;

        printf("Building the USD mesh: %s\n", relativePath.c_str());
        
        VtArray<GfVec3f> points;
        std::vector<VertexAsset> tempPoints;
        if (mesh.GetPointsAttr().Get(&points))
        {
          usd_mesh.num_vertices = (u32)points.size();
          for (const GfVec3f& point : points)
          {
            VertexAsset vert;
            vert.position = Vec3(point[0], point[1], point[2]); // TODO fill in uv's/normals here, and duplicate verts
            tempPoints.push_back(vert); // TODO duplicate verts to have uv's on a per face basis
          }
          usd_mesh.vertices = tempPoints.data();
          printf("Mesh has %zu points\n", points.size());
        }
        else
        {
          printf("ERROR: %s mesh is missing points from its USD file\n", relativePath.c_str());
        }

        VtArray<int> mesh_indices;
        std::vector<u32> tempIndices;
        if (mesh.GetFaceVertexIndicesAttr().Get(&mesh_indices))
        {
          usd_mesh.num_indices = (u32)mesh_indices.size();
          for (const int& index : mesh_indices)
          {
            tempIndices.push_back(index);
          }
          usd_mesh.indices = tempIndices.data();
        }
        else
        {
          printf("ERROR: %s mesh is missing index buffer in its USD file\n", relativePath.c_str());
        }
        
        return write_usd_model_to_asset(projectRoot.c_str(), usd_model);
      }
    }
    return true;
  }
  else
  {
    printf("ERROR: Model was not loaded\n");
  }

  return false;
}

// UsdBuilder.exe <input_path> <project_root_dir>
int main(int argc, const char** argv)
{
  
  static constexpr size_t kInitHeapSize = MiB(128);

  u8* init_memory = HEAP_ALLOC(u8, GLOBAL_HEAP, kInitHeapSize);
  LinearAllocator init_allocator = init_linear_allocator(init_memory, kInitHeapSize);

  g_InitHeap = init_allocator;

  init_context(g_InitHeap, GLOBAL_HEAP);

  if (argc != 3)
  {
    printf("Invalid arguments!\n");
    printf("AssetBuilder.exe <input_path> <project_root>\n");
    return 1;
  }

  argv++;
  argc--;

  std::string input_path   = argv[0];
  std::string project_root = argv[1];

  std::string full_path    = project_root + "/" + input_path;

  // Loads a single USD model in by creating a .built file for it
  bool result = load_single_model(full_path, input_path, project_root);
  
  if (result)
  {
    printf("Successful building USD!\n");
  }
  else
  {
    printf("Failed building USD!\n");
  }

  return (result) ? 0 : 1;
}

