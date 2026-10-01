# Third-party provenance

ONNX Runtime 1.23.0: https://github.com/microsoft/onnxruntime/tree/v1.23.0
Windows private ORT copy changes DirectML delay import to hv_dml.dll and is unsigned.
DirectML 1.15.4: https://www.nuget.org/packages/Microsoft.AI.DirectML/1.15.4
FFmpeg 7.1 binaries: ByteDeco JavaCPP presets 1.5.11, non-GPL classifiers.
Build scripts: https://github.com/bytedeco/javacpp-presets/tree/1.5.11/ffmpeg
FFmpeg source: https://ffmpeg.org/releases/ffmpeg-7.1.tar.xz
These are dynamically linked replaceable libraries. No FFmpeg JNI library is used.
Model origins/export parameters/hashes are included in Documentation.
OpenMMLab: https://github.com/open-mmlab/mmdetection and https://github.com/open-mmlab/mmpose
This is a user-testing preview, not a claim of model redistribution clearance or hardware acceptance.
