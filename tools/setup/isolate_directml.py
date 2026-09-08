"""Isolate the private ORT copy's DirectML delay import from Unity vendor DLLs.

The PE delay-import name and signature metadata are changed without resizing
sections. The publisher input remains unchanged; output is not publisher-signed.
"""
import argparse
import struct
from pathlib import Path


def isolate(data):
    image = bytearray(data)
    pe = struct.unpack_from('<I', image, 0x3c)[0]
    if image[pe:pe+4] != b'PE\0\0':
        raise ValueError('Not a PE file')
    sections, optional_size = struct.unpack_from('<H', image, pe+6)[0], struct.unpack_from('<H', image, pe+20)[0]
    optional = pe+24
    if struct.unpack_from('<H', image, optional)[0] != 0x20b:
        raise ValueError('Expected PE32+ runtime')
    table = optional+optional_size
    def offset(rva):
        for index in range(sections):
            start = table+index*40
            virtual_size, address, raw_size, raw = struct.unpack_from('<IIII', image, start+8)
            if address <= rva < address+max(virtual_size, raw_size):
                return raw+rva-address
        raise ValueError('RVA is outside PE sections')
    delay_rva, size = struct.unpack_from('<II', image, optional+112+13*8)
    if not delay_rva:
        raise ValueError('Missing delay-import table')
    start = offset(delay_rva)
    changed = 0
    for index in range(size//32):
        attributes, name_rva = struct.unpack_from('<II', image, start+index*32)
        if not name_rva:
            break
        if attributes != 1:
            raise ValueError('Expected RVA-based delay import')
        name = offset(name_rva)
        end = image.index(0, name)
        if bytes(image[name:end]).lower() == b'directml.dll':
            replacement = b'hv_dml.dll'
            image[name:end] = replacement.ljust(end-name, b'\0')
            changed += 1
    if changed != 1:
        raise ValueError('Expected exactly one DirectML delay import')
    # Invalidate the Authenticode directory in this private modified copy.
    struct.pack_into('<II', image, optional+112+4*8, 0, 0)
    struct.pack_into('<I', image, optional+64, 0)
    return bytes(image)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    args.output.write_bytes(isolate(args.source.read_bytes()))
