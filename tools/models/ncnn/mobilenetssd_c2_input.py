"""Write MobileNetSSD's exact BGR CHW input from a real image.

Usage: python tools/models/ncnn/mobilenetssd_c2_input.py IMAGE OUTPUT
Matches the original repository's demo.py: BGR bilinear resize to 300 square,
subtract 127.5, multiply 0.007843, then NCHW float32 before Vulkan upload.
"""
import sys

import cv2
import numpy as np


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit("usage: mobilenetssd_c2_input.py IMAGE OUTPUT")
    image = cv2.imread(sys.argv[1])
    if image is None:
        raise SystemExit(f"image unreadable: {sys.argv[1]}")
    bgr = cv2.resize(image, (300, 300))
    tensor = (bgr.astype(np.float32) - 127.5) * 0.007843
    tensor.transpose(2, 0, 1).tofile(sys.argv[2])
