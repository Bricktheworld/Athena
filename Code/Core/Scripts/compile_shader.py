import sys, os, re, tempfile, subprocess
from pathlib import Path
import argparse

kVertexShader = 0
kPixelShader = 1
kComputeShader = 2
kRayTracingShader = 3

kDxcShaderTypeString = ['vs_6_6', 'ps_6_6', 'cs_6_6', 'lib_6_6']
kEntryPointPrefix = ['VS_', 'PS_', 'CS_', 'RT_']

def main():
  parser = argparse.ArgumentParser(
    prog='compile_shaders',
    description='Compiles Athena shaders (.vsh, .psh, .csh, .rtsh) (vertex, pixel, compute, and ray-tracing shaders) using dxc.'
  )
  parser.add_argument('source', type=argparse.FileType('r'))
  parser.add_argument('-o', '--output', type=argparse.FileType('w'), required=True)
  parser.add_argument('-b', '--binary', action='store_true')
  parser.add_argument('--path_to_dxc', default='Code/Core/Bin/dxc/dxc.exe')
    
  args = parser.parse_args()

  extension = os.path.splitext(args.source.name)[1]
  print(args.source.name)

  shader_type = -1

  entry_points = []
  match extension:
    case '.vsh':
      shader_type = kVertexShader
    case '.psh':
      shader_type = kPixelShader
    case '.csh':
      shader_type = kComputeShader
    case '.rtsh':
      shader_type = kRayTracingShader
      filename = Path(os.path.basename(args.source.name)).stem
      entry_point_name = 'RT_' + filename.replace('_', ' ').title().replace(' ', '')
      entry_points.append(entry_point_name)
    case _:
      sys.exit('Invalid shader input file {}'.format(args.source.name))
    
  if shader_type != kRayTracingShader:
    source_lines = ''.join(args.source.readlines())
    for entry_point in re.finditer(r'\w+[\n\r\s]*(' + kEntryPointPrefix[shader_type] + r'\w+)[\n\r\s]*\(', source_lines):
      entry_points.append(entry_point.group(1))

  output_header_lines = []
  for entry_point in entry_points:
    with tempfile.NamedTemporaryFile('w', delete_on_close=False) as tmp:
      tmp.close()
      entry_point_args = []
      if shader_type != kRayTracingShader:
        entry_point_args = ['-E', entry_point]
      res = subprocess.run(
        [
          args.path_to_dxc,
          '-T',
          kDxcShaderTypeString[shader_type],
        ] + 
        entry_point_args +
        [
          args.source.name,
          '-Zi',
          '-Qembed_debug',
          '-HV',
          '2021',
          '-Fh',
          tmp.name,
          '-Vn',
          '__kShaderSource__' + entry_point
        ],
      shell=True)

      if res.returncode != 0:
        sys.exit('Failed to compile {} ({})!'.format(entry_point, args.source.name))

      with open(tmp.name, 'r') as f:
        output_header_lines.extend(f.readlines())

  args.output.writelines(output_header_lines)
    

if __name__ == "__main__":
  main()
