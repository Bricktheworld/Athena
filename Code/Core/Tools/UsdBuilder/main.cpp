#include "Core/Foundation/types.h" 

#include "tinyusdz.hh"
#include "pprinter.hh"
#include "prim-pprint.hh"
#include "value-pprint.hh"
#include "tydra/scene-access.hh"

// UsdBuilder.exe <input_path> <project_root_dir>
int main(int argc, const char** argv)
{
  static constexpr size_t kInitHeapSize = MiB(128);

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

  tinyusdz::Stage stage;
  bool result = tinyusdz::LoadUSDAFromFile(full_path, &stage, nullptr, nullptr);

  printf("Successful %d\n", result);

  return 0;
}

