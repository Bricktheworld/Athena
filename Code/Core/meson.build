include_dir = include_directories(
  '../',
)

fs = import('fs')
cc = meson.get_compiler('cpp')

lib_directory = '/Lib/Debug/'
if get_option('buildtype') == 'debug'
  engine_lib_dirs = '/Lib/Debug/'
else
  engine_lib_dirs = '/Lib/Release/'
endif

#########################################################################################

foundation_src = [
  'Foundation/assets.cpp',
  'Foundation/assets.h',
  'Foundation/context.cpp',
  'Foundation/context.h',
  'Foundation/filesystem.cpp',
  'Foundation/filesystem.h',
  'Foundation/hash.cpp',
  'Foundation/hash.h',
  'Foundation/math.h',
  'Foundation/memory.cpp',
  'Foundation/memory.h',
  'Foundation/pool_allocator.cpp',
  'Foundation/pool_allocator.h',
  'Foundation/profiling.cpp',
  'Foundation/profiling.h',
  'Foundation/threading.cpp',
  'Foundation/threading.h',
  'Foundation/types.h',

  'Foundation/Containers/array.h',
  'Foundation/Containers/error_or.h',
  'Foundation/Containers/hash_table.h',
  'Foundation/Containers/iterator.h',
  'Foundation/Containers/option.h',
  'Foundation/Containers/option.h',
  'Foundation/Containers/ring_buffer.cpp',
  'Foundation/Containers/ring_buffer.h',

  'Foundation/Vendor/xxhash/xxhash.cpp',
  'Foundation/Vendor/xxhash/xxhash.h',
]

foundation = shared_library(
  'Foundation',
  foundation_src,
  include_directories: include_dir,
  cpp_args: ['-DFOUNDATION_EXPORT', '-DUNICODE'],
)

#########################################################################################

python = find_program(meson.project_source_root() + '/Bin/python-3.12.0/python.exe')
dxc_path = meson.project_source_root() + '/Code/Core/Bin/dxc/dxc.exe'

shader_compiler = generator(
  python,
  output: '@PLAINNAME@.built.h',
  arguments: [meson.project_source_root() + '/Code/Core/Scripts/compile_shader.py', '@INPUT@', '-o', '@OUTPUT@', '--path_to_dxc', dxc_path],
)

engine_shaders = [
  'Engine/Shaders/PostProcessing/dof.csh',
  'Engine/Shaders/PostProcessing/tonemapping.psh',

  'Engine/Shaders/Vertex/basic.vsh',
  'Engine/Shaders/Vertex/fullscreen.vsh',

  'Engine/Shaders/Pixel/fullscreen.psh',

  'Engine/Shaders/Lighting/standard_brdf.rtsh',

  'Engine/Shaders/Materials/basic_normal_gloss.psh',

  'Engine/Shaders/DDGI/ddgi.csh',
  'Engine/Shaders/DDGI/probe_trace.rtsh',
]

compiled_engine_shaders = []
foreach shader : engine_shaders
  compiled_engine_shaders += shader_compiler.process(shader)
endforeach

engine_shader_table = custom_target(
  input: [compiled_engine_shaders],
  output: ['shader_table.h', 'shader_table.cpp'],
  command: [
    python,
    meson.project_source_root() + '/Code/Core/Scripts/generate_shader_table.py',
    '--output_header',
    '@OUTPUT0@',
    '--output_source',
    '@OUTPUT1@',
    '--inputs',
    '@INPUT@'
  ]
)

engine_src = [
  # 'Engine/fibers.masm',
  'Engine/asset_streaming.cpp',
  'Engine/asset_streaming.h',
  'Engine/job_system.cpp',
  'Engine/job_system.h',
  'Engine/main.cpp',
  'Engine/memory.cpp',
  'Engine/memory.h',

  'Engine/Render/graphics.cpp',
  'Engine/Render/graphics.h',
  'Engine/Render/render_graph.cpp',
  'Engine/Render/render_graph.h',
  'Engine/Render/renderer.cpp',
  'Engine/Render/renderer.h',

  'Engine/Vendor/imgui/imgui.cpp',
  'Engine/Vendor/imgui/imgui_demo.cpp',
  'Engine/Vendor/imgui/imgui_draw.cpp',
  'Engine/Vendor/imgui/imgui_impl_dx11.cpp',
  'Engine/Vendor/imgui/imgui_impl_dx12.cpp',
  'Engine/Vendor/imgui/imgui_impl_win32.cpp',
  'Engine/Vendor/imgui/imgui_tables.cpp',
  'Engine/Vendor/imgui/imgui_widgets.cpp',

  engine_shader_table,
  # compiled_engine_shaders,
]

engine_dlls = [
]

engine_libs = [
  'DirectXTK',
]

engine_lib_dir = meson.project_source_root() + '/Code/Core/Engine' + lib_directory

# TODO(Brandon): This is ugly as shit because fs.copyfile creates an _entire_ vsln project...
# I think the correct solution here is to treat them like
engine_dependencies = []
foreach dll : engine_dlls
  engine_dependencies += cc.find_library(dll, dirs: engine_lib_dir)
  fs.copyfile(engine_lib_dir + dll + '.dll', dll + '.dll')
  fs.copyfile(engine_lib_dir + dll + '.pdb', dll + '.pdb')
endforeach

foreach lib : engine_libs
  engine_dependencies += cc.find_library(lib, dirs: engine_lib_dir)
endforeach

windows = import('windows')
resources = windows.compile_resources('Engine/Resources/athena.rc', depend_files: 'Engine/Resources/athena_512x512.ico')

engine = executable(
  'Engine',
  engine_src,
  resources,

  include_directories: include_dir,
  link_with: foundation,
  dependencies: engine_dependencies,
  cpp_args: ['-DUNICODE'],
  win_subsystem: 'windows',
)

#########################################################################################

asset_builder_src = [
  'Tools/AssetBuilder/main.cpp',
  'Tools/AssetBuilder/model_importer.cpp',
  'Tools/AssetBuilder/model_importer.h',
  'Tools/AssetBuilder/material_importer.cpp',
  'Tools/AssetBuilder/material_importer.h',
  'Tools/AssetBuilder/texture_importer.cpp',
  'Tools/AssetBuilder/texture_importer.h',
]

asset_builder_dlls = [
  'assimp-vc143-mtd',
]

asset_builder_libs = [
]

asset_builder_lib_dir = meson.project_source_root() + '/Code/Core/Tools/AssetBuilder' + lib_directory

# TODO(Brandon): This is ugly as shit because fs.copyfile creates an _entire_ vsln project...
# I think the correct solution here is to treat them like
asset_builder_dependencies = []
foreach dll : asset_builder_dlls
  asset_builder_dependencies += cc.find_library(dll, dirs: asset_builder_lib_dir)
  fs.copyfile(asset_builder_lib_dir + dll + '.dll', dll + '.dll')
  fs.copyfile(asset_builder_lib_dir + dll + '.pdb', dll + '.pdb')
endforeach

foreach lib : asset_builder_libs
  asset_builder_dependencies += cc.find_library(lib, dirs: asset_builder_lib_dir)
endforeach

asset_builder = executable(
  'AssetBuilder',
  asset_builder_src,

  include_directories: include_directories('../', 'Tools/AssetBuilder/Vendor'),
  link_with: foundation,
  dependencies: asset_builder_dependencies,
  cpp_args: ['-DUNICODE'],
)

#########################################################################################

additional_files = {
  'Engine/Lib/D3D12Core.dll':      'D3D12Core.dll',
  'Engine/Lib/D3D12Core.pdb':      'D3D12Core.pdb',
  'Engine/Lib/D3D12SDKLayers.dll': 'D3D12SDKLayers.dll',
  'Engine/Lib/D3D12SDKLayers.pdb': 'D3D12SDKLayers.pdb',
}

foreach src, dst : additional_files
  fs.copyfile(meson.project_source_root() + '/Code/Core/' + src, dst)
endforeach

#########################################################################################