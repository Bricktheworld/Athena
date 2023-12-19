import sys, os, re 
import argparse

def main():
  parser = argparse.ArgumentParser(
    prog='generate_shader_table',
    description='Combines intermediary shader headers into single shader table header and cpp source file.'
  )
  # parser.add_argument('source', type=argparse.FileType('r'))
  parser.add_argument('--output_header', type=argparse.FileType('w'), required=True)
  parser.add_argument('--output_source', type=argparse.FileType('w'), required=True)
  parser.add_argument('--inputs', nargs='+', type=argparse.FileType('r'))
    
  args = parser.parse_args()

  source_lines_list = []
  for input in args.inputs:
    source_lines_list.extend(input.readlines())

  source_lines = ''.join(source_lines_list)
  shader_binaries = []
  for shader_binary in re.finditer(r'__kShaderSource__(\w+)', source_lines):
    shader_binaries.append(shader_binary.group(1))
  

  shader_enums = ''
  shader_binary_variables = ''
  shader_binary_sizes = ''
  for shader_name in shader_binaries:
    shader_enums += '  k' + shader_name + ',\n'
    shader_binary_variables += '  __kShaderSource__' + shader_name + ',\n'
    shader_binary_sizes += '  sizeof(__kShaderSource__' + shader_name + '),\n'
  
  output_header_lines = '#pragma once\nenum EngineShaderIndex\n{\n' + shader_enums + '  kEngineShaderCount,\n};\nextern const unsigned char* kEngineShaderBinSrcs[];\nextern const size_t kEngineShaderBinSizes[];'

  local_header_name = os.path.basename(args.output_header.name)
  
  output_source_lines = '#include "' + local_header_name +'"\n' + source_lines + '\nconst unsigned char* kEngineShaderBinSrcs[] = \n{\n' + shader_binary_variables + '};\nconst size_t kEngineShaderBinSizes[] = \n{\n' + shader_binary_sizes + '};\n'
  args.output_header.write(output_header_lines)
  args.output_source.write(output_source_lines)

if __name__ == "__main__":
  main()