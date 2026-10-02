"""Compile input shader only with the already qualified NDK23 compiler."""
import hashlib,json,pathlib,subprocess,struct,sys
ROOT=pathlib.Path(__file__).resolve().parents[2]
NDK=pathlib.Path('D:/Developer/2022.3.61t4/Editor/Data/PlaybackEngines/AndroidPlayer/NDK')
COMPILER=NDK/'shader-tools/windows-x86_64/glslc.exe'
COMPILER_SHA='3e8857d3cc37849e2b2d29bd9244bbe11100081231de5dd09623f32638129e50'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
if 'Pkg.Revision = 23.1.7779620' not in (NDK/'source.properties').read_text() or sha(COMPILER)!=COMPILER_SHA:raise SystemExit('Unqualified input shader compiler')
source=ROOT/'native/input/shaders/input_yuv_to_rgba.comp'
output=ROOT/'native/input/shaders/input_yuv_to_rgba.comp.spv'
args=[str(COMPILER),'--target-env=vulkan1.1','-O',str(source),'-o',str(output)]
subprocess.run(args,check=True)
words=struct.unpack('<'+'I'*(output.stat().st_size//4),output.read_bytes())
header=ROOT/'native/input/src/android/input_yuv_shader.h'
header.write_text('#pragma once\n#include <cstdint>\nnamespace hvinput {\nstatic constexpr uint32_t input_yuv_shader[] = {\n'+','.join('0x%08xu'%w for w in words)+'\n};\n}\n')
(source.parent/'input_yuv_to_rgba.build.json').write_text(json.dumps({'ndk_revision':'23.1.7779620','compiler_sha256':sha(COMPILER),'compiler_version':subprocess.check_output([str(COMPILER),'--version'],text=True),'arguments':args,'source_sha256':sha(source),'spirv_sha256':sha(output),'header_sha256':sha(header)},indent=2)+'\n')
print('Input shader qualified',sha(output))
