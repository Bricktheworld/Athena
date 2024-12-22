#include "Core/Foundation/types.h" 

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

  const char* input_path   = argv[0];
  const char* project_root = argv[1];

  printf("Hello world!\n");

  return 0;
}

