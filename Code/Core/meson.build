include_dir = include_directories(
  '../',
  'Engine/Vendor', # TODO(Brandon): This is a giant hack that I am putting in place to get assimp to compile...
)

fs = import('fs')
cc = meson.get_compiler('cpp')

foundation_src = [
  'Foundation/context.cpp',
  'Foundation/context.h',
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
  'Engine/job_system.cpp',
  'Engine/job_system.h',
  'Engine/main.cpp',

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
  'assimp-vc143-mtd',
]

engine_libs = [
  'DirectXTK',
]

engine_additional_files = {
  'Engine/Lib/D3D12Core.dll':      'D3D12Core.dll',
  'Engine/Lib/D3D12Core.pdb':      'D3D12Core.pdb',
  'Engine/Lib/D3D12SDKLayers.dll': 'D3D12SDKLayers.dll',
  'Engine/Lib/D3D12SDKLayers.pdb': 'D3D12SDKLayers.pdb',
}


engine_lib_dirs = []
if get_option('buildtype') == 'debug'
  engine_lib_dirs += meson.project_source_root() + '/Code/Core/Engine/Lib/Debug/'
else
  engine_lib_dirs += meson.project_source_root() + '/Code/Core/Engine/Lib/Release/'
endif

# TODO(Brandon): This is ugly as shit because fs.copyfile creates an _entire_ vsln project...
# I think the correct solution here is to treat them like
engine_dependencies = []
foreach dll : engine_dlls
  engine_dependencies += cc.find_library(dll, dirs: engine_lib_dirs)
  fs.copyfile(engine_lib_dirs[0] + dll + '.dll', dll + '.dll')
  fs.copyfile(engine_lib_dirs[0] + dll + '.pdb', dll + '.pdb')
endforeach

foreach lib : engine_libs
  engine_dependencies += cc.find_library(lib, dirs: engine_lib_dirs)
endforeach

foreach src, dst : engine_additional_files
  fs.copyfile(meson.project_source_root() + '/Code/Core/' + src, dst)
endforeach

engine = executable(
  'Engine',
  engine_src,

  include_directories: include_dir,
  link_with: foundation,
  dependencies: engine_dependencies,
  cpp_args: ['-DUNICODE'],
  win_subsystem: 'windows',
)