"""Verify the actual embedded color-copy SPIR-V preserves exact texel copies."""
import re
import struct
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[2]
class ColorCopyShader(unittest.TestCase):
    def test_embedded_shader_matches_binary_and_contains_texel_fetch(self):
        base=ROOT/'runtime/gpu/android/shaders'
        data=(base/'copy_rgba.frag.spv').read_bytes()
        header=(base/'embedded_shaders.h').read_text()
        body=header.split('kCopyRgbaFragmentSpv[] = {',1)[1].split('};',1)[0]
        words=[int(x,16) for x in re.findall(r'0x([0-9a-fA-F]+)u',body)]
        self.assertEqual(data,struct.pack('<%dI'%len(words),*words))
        opcodes=[];i=5
        while i<len(words):
            count=words[i]>>16;self.assertGreater(count,0)
            opcodes.append(words[i]&0xffff);i+=count
        self.assertIn(95,opcodes,'OpImageFetch required for exact same-extent pixels')
        self.assertIn(87,opcodes,'OpImageSampleImplicitLod retained for existing scaling path')

if __name__=='__main__': unittest.main()
