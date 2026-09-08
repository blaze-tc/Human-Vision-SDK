"""Probe native initialization outside Unity, from an unrelated working directory."""
import argparse
import ctypes as c
import os
from pathlib import Path


class Config(c.Structure):
    _fields_ = [('size', c.c_int32), ('max_bodies', c.c_int32),
                ('detection', c.c_float), ('pose', c.c_float),
                ('interval', c.c_int32), ('tracking', c.c_int32),
                ('backend', c.c_int32), ('detector_path', c.c_char_p), ('pose_path', c.c_char_p)]


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('dll', type=Path)
    parser.add_argument('detector', type=Path)
    parser.add_argument('pose', type=Path)
    parser.add_argument('--preload-vendor', type=Path)
    args = parser.parse_args()
    library, detector, pose = args.dll.resolve(), args.detector.resolve(), args.pose.resolve()
    if args.preload_vendor:
        vendor = c.WinDLL(str(args.preload_vendor.resolve()))
    os.chdir(os.environ['TEMP'])
    sdk = c.CDLL(str(library))
    sdk.HV_Create.argtypes = [c.POINTER(Config), c.POINTER(c.c_void_p)]
    sdk.HV_Create.restype = c.c_int32
    sdk.HV_GetLastError.argtypes = [c.c_void_p]
    sdk.HV_GetLastError.restype = c.c_char_p
    sdk.HV_Destroy.argtypes = [c.c_void_p]
    config = Config(c.sizeof(Config), 4, .35, .3, 1, 1, 0,
                    str(detector).encode('utf-8'), str(pose).encode('utf-8'))
    handle = c.c_void_p()
    result = sdk.HV_Create(c.byref(config), c.byref(handle))
    if result != 0:
        raise RuntimeError(sdk.HV_GetLastError(handle))
    sdk.HV_Destroy(handle)
    print('PASS: native initialization and destroy from foreign cwd with vendor DLL preloaded')
