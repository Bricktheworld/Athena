import sys, os, re, tempfile, subprocess, socket, zlib, struct
from pathlib import Path
import argparse

kVertexShader = 0
kPixelShader = 1
kComputeShader = 2
kRayTracingShader = 3

kPacketMagicNumber = zlib.crc32(b"ATHENA_PACKET")
kPacketVersion = 0
kPacketType = zlib.crc32(b"HOT_RELOAD_SHADER_REQUEST")

u8 = 'B'
u32 = 'I'
u64 = 'Q'
PacketHotReloadShader = '<' + u32 + u32 + u32 + u32 + u64 + u64 + u32 + u32

kDxcShaderTypeString = ['vs_6_6', 'ps_6_6', 'cs_6_6', 'lib_6_6']
kEntryPointPrefix = ['VS_', 'PS_', 'CS_', 'RT_']

def sizeof(fmt):
  return struct.calcsize(fmt)


def main():
  parser = argparse.ArgumentParser(
    prog='reload_shader',
    description='Recompiles Athena shaders (.vsh, .psh, .csh, .rtsh) (vertex, pixel, compute, and ray-tracing shaders) using dxc and sends updated shaders to asset server.'
  )
  parser.add_argument('source', type=argparse.FileType('r'))
  parser.add_argument('entry_point', type=str)
  parser.add_argument('--asset_server', type=str, default='127.0.0.1:8000')
  parser.add_argument('--path_to_dxc', default='Code\\Core\\Bin\\dxc\\dxc.exe')
    
  args = parser.parse_args()

  extension = os.path.splitext(args.source.name)[1]

  with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    match = re.search(r"(.*):(.*)", args.asset_server)
    host, port = match.group(1), int(match.group(2))
    s.connect((host, port))


    with tempfile.NamedTemporaryFile('wb', delete_on_close=False) as tmp:
      tmp.close()
      entry_point_args = []

      shader_type = next((i for i, p in enumerate(kEntryPointPrefix) if args.entry_point.startswith(p)), -1)
      if shader_type != kRayTracingShader:
        entry_point_args = ['-E', args.entry_point]

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
          '-enable-16bit-types',
          '-Fo',
          tmp.name
        ],
      shell=True)

      if res.returncode != 0:
        sys.exit('Failed to compile {} ({})!'.format(args.entry_point, args.source.name))

      with open(tmp.name, "rb") as f:
        shader_buf = f.read()
        shader_size = len(shader_buf)
        name_buf = args.entry_point.encode('utf-8') + b"\0"
        name_size = len(name_buf)
        reload_shader_packet = struct.pack(
          PacketHotReloadShader,
          kPacketMagicNumber,
          kPacketVersion,
          kPacketType,
          sizeof(PacketHotReloadShader) + shader_size + name_size,
          sizeof(PacketHotReloadShader),
          sizeof(PacketHotReloadShader) + name_size,
          name_size,
          shader_size
        ) + name_buf + shader_buf

        s.sendall(reload_shader_packet)

      os.remove(tmp.name)
  
  print("Hot reloaded shader!")


if __name__ == "__main__":
  main()
