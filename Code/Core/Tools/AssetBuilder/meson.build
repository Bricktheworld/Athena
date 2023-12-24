include_dir = include_directories(
  '../../../',
  'Vendor', # TODO(Brandon): This is a giant hack that I am putting in place to get assimp to compile...
)

src = [
  'main.cpp',
  'mesh_importer.cpp',
  'mesh_importer.h',
]

engine = executable(
  'AssetBuilder',
  src,

  include_directories: include_dir,
  link_with: foundation,
  cpp_args: ['-DUNICODE'],
  # win_subsystem: 'windows',
)