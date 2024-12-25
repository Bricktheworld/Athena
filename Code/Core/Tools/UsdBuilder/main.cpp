#include "Core/Foundation/types.h"
#include "Core/Tools/AssetBuilder/model_importer.h"

#include "tinyusdz.hh"
#include "pprinter.hh"
#include "prim-pprint.hh"
#include "value-pprint.hh"
#include "tydra/scene-access.hh"

#ifdef _MSC_VER
#pragma warning(disable:4244)
#pragma warning(disable:4305)
#endif

#include "pxr/base/tf/weakBase.h"
#include "pxr/base/tf/weakPtr.h"
#include "pxr/usd/usd/notice.h"
#include "pxr/usd/usd/prim.h"
#include "pxr/usd/usd/primRange.h"
#include "pxr/usd/usd/stage.h"
#include "pxr/usd/usdGeom/xformable.h"
#include "pxr/usd/usdGeom/xform.h"
#include "pxr/usd/usdGeom/mesh.h"

PXR_NAMESPACE_USING_DIRECTIVE



class SceneProxy : public TfWeakBase         // in order to register for Tf events
{
  // For changes from UsdStage.
  TfNotice::Key _objectsChangedNoticeKey;
  UsdStageRefPtr stage;
public:

  ~SceneProxy();

  void _OnObjectsChanged(UsdNotice::ObjectsChanged const& notice, UsdStageWeakPtr const& sender);

  void create_new_stage(std::string const& path);
  void load_stage(std::string const& filePath);
  void save_stage();
};

SceneProxy::~SceneProxy()
{
  TfNotice::Revoke(_objectsChangedNoticeKey);
}

void SceneProxy::_OnObjectsChanged(UsdNotice::ObjectsChanged const& notice, UsdStageWeakPtr const& sender)
{
  printf("GetResyncedPaths\n");
  auto pathsToResync = notice.GetResyncedPaths();
  for (auto & i : pathsToResync)
  {
    printf("%s\n", i.GetString().c_str());
  }
  printf("GetChangedInfoOnlyPaths\n");
  auto infoPaths = notice.GetChangedInfoOnlyPaths();
  for (auto & i : infoPaths)
  {
    printf("%s\n", i.GetString().c_str());
  }
}

void SceneProxy::create_new_stage(std::string const& path)
{
  TfNotice::Revoke(_objectsChangedNoticeKey);

  stage = UsdStage::CreateNew(path);

  // Start listening for change notices from this stage.
  auto self = TfCreateWeakPtr(this);
  _objectsChangedNoticeKey = TfNotice::Register(self, &SceneProxy::_OnObjectsChanged, stage);

  // create a cube on the stage
  stage->DefinePrim(SdfPath("/Box"), TfToken("Cube"));
  UsdPrim cube = stage->GetPrimAtPath(SdfPath("/Box"));
  GfVec3f scaleVec = { 5.f, 5.f, 5.f };
  UsdGeomXformable cubeXf(cube);
  cubeXf.AddScaleOp().Set(scaleVec);
}

void SceneProxy::load_stage(std::string const& filePath)
{
  printf("\nLoad_Stage : %s\n", filePath.c_str());
  auto supported = UsdStage::IsSupportedFile(filePath);
  if (supported)
  {
    printf("File format supported\n");
  }
  else
  {
    fprintf(stderr, "%s : File format not supported\n", filePath.c_str());
    return;
  }

  UsdStageRefPtr loadedStage = UsdStage::Open(filePath);

  if (loadedStage)
  {
    auto pseudoRoot = loadedStage->GetPseudoRoot();
    printf("Pseudo root path: %s\n", pseudoRoot.GetPath().GetString().c_str());
    for (auto const& c : pseudoRoot.GetChildren())
    {
      printf("\tChild path: %s\n", c.GetPath().GetString().c_str());
    }
  }
  else
  {
    fprintf(stderr, "Stage was not loaded");
  }
}

void SceneProxy::save_stage()
{
  if (stage)
    stage->GetRootLayer()->Save();
}

bool load_single_model(std::string const& filePath, std::string const& relativePath)
{
  UsdStageRefPtr loadedStage = UsdStage::Open(filePath);

  if (loadedStage)
  {
    for (const UsdPrim& p : loadedStage->Traverse())
    {
      if (p.IsA<UsdGeomMesh>()) // Check if we are loading in a model that has a transform node
      {
        UsdGeomMesh mesh = UsdGeomMesh(p);
        /*
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

        /*
        VtArray<GfVec3f> points;
        std::vector<VertexAsset> tempPoints;
        if (mesh.GetPointsAttr().Get(&points))
        {
          usd_mesh.num_vertices = points.size();
          for (const GfVec3f& point : points)
          {
            VertexAsset vert;
            vert.position = Vec3(point[0], point[1], point[2]); // TODO fill in uv's/normals here, and duplicate verts
            tempPoints.push_back(vert); // TODO duplicate verts to have uv's on a per face basis
          }
          usd_mesh.vertices = tempPoints.data();
          printf("Mesh has %d points", points.size());
        }
        else
        {
          printf("ERROR: %s mesh is missing points from its USD file\n", relativePath);
        }

        VtArray<int> mesh_indices;
        std::vector<u32> tempIndices;
        if (mesh.GetFaceVertexIndicesAttr().Get(&mesh_indices))
        {
          usd_mesh.num_indices = mesh_indices.size();
          for (const int& index : mesh_indices)
          {
            tempIndices.push_back(index);
          }
          usd_mesh.indices = tempIndices.data();
        }
        else
        {
          printf("ERROR: %s mesh is missing index buffer in its USD file\n", relativePath);
        }
        */
        return true;
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
  //SceneProxy scene;
  //scene.create_new_stage("test.usda");
  //scene.save_stage();

  //SceneProxy scene2;
 // scene2.load_stage("test.usda");

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

  bool result = load_single_model(full_path, input_path);

  //tinyusdz::Stage stage;
  //bool result = tinyusdz::LoadUSDAFromFile(full_path, &stage, nullptr, nullptr);

  // call write_model_to_asset(const char* project_root, const ImportedModel& model)
  // ultimately create an ImportedModel
  // 
  
  printf("Successful %d\n", result);

  return 0;
}

