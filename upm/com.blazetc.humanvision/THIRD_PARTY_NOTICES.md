# Third-party notices and model provenance

This notice records upstream licenses and pinned bytes. The SDK's repository
license is unchanged. Model approval is unchanged: YOLO packs are local evaluation
only, distribution rights unestablished, and distribution_qualified=false.

- ncnn: Tencent BSD 3-Clause, statically included in the Android SDK native
  runtime. Bundled Licenses/ncnn-LICENSE.txt is copied byte-for-byte from this
  checkout's pinned third_party/ncnn/LICENSE (SHA-256 7c974bac98848df46be1af5bdaa3c3c9c01f6082a90f55caeb7f60c6208aa255).
  Official source: https://github.com/Tencent/ncnn ; official license URL:
  https://raw.githubusercontent.com/Tencent/ncnn/master/LICENSE.txt . This upstream
  notice also identifies ncnn's third-party components and their licenses.
- YOLOv8 pose: Ultralytics origin. Official AGPL 3.0 text is bundled as
  Licenses/Ultralytics-AGPL-3.0.txt, fetched 2026-10-05 from https://raw.githubusercontent.com/ultralytics/ultralytics/main/LICENSE,
  pinned SHA-256 0d96a4ff68ad6d4b6f1f30f713b18d5184912ba8dd389f86aa7710db079abcb0. The converted param/bin graph comes from
  https://github.com/nihui/ncnn-android-yolov8/tree/f1ac75ec54ccb3817a9eba620fe51da8bdcf87ca .
  ModelPack metadata records graph, weights and profile hashes, FP32 raw-tensor
  conversion contract, letterbox preprocessing and rectangle 512×288/640×384/960×576
  eligibility. Including AGPL text is not a grant of model redistribution or
  commercial license approval; retained restrictions remain authoritative.
- ONNX Runtime 1.23.0: MIT, https://github.com/microsoft/onnxruntime/tree/v1.23.0 .
  See Licenses/ONNXRuntime.txt. Windows private ORT uses the hv_dml.dll delay-import
  name and is unsigned. Android and Windows runtime payloads are indexed by hash.
- DirectML 1.15.4: Microsoft redistributable terms/code notices, see
  Licenses/DirectML-LICENSE.txt, DirectML-LICENSE-CODE.txt and
  DirectML-ThirdPartyNotices.txt; https://www.nuget.org/packages/Microsoft.AI.DirectML/1.15.4 .
- FFmpeg 7.1: LGPL 2.1-or-later, dynamically linked and owned by the Input package.
  See Licenses/FFmpeg-LGPL-2.1.txt and Input native-payload.json. Source:
  https://ffmpeg.org/releases/ffmpeg-7.1.tar.xz ; binary build provenance:
  https://github.com/bytedeco/javacpp-presets/tree/1.5.11/ffmpeg . No FFmpeg JNI
  library is included. A source-code license does not by itself establish complete
  redistributor compliance; this release does not claim that clearance.
- OpenMMLab model/reference sources: Apache 2.0 code notices in mmpose.txt and
  mmdetection.txt; https://github.com/open-mmlab/mmpose and
  https://github.com/open-mmlab/mmdetection . RTMO model provenance and conversion
  are recorded in RuntimeData/modelpacks/rtmo-t-416/SOURCES.md. Weight rights remain
  subject to their original provenance; code notices do not relicense models.

The source snapshot receipt pins every packaged file and importer metadata.
Performance/hardware acceptance and genuine hand endpoint inference are separate
from these factual source-license notices.

## Statically linked source notices

- glslang: Licenses/glslang-LICENSE.txt, SHA-256 17e70c676e1521ff3e4686f04a2053d93a7e28a33be8de7ec37ab0ff72feb677.
  Source URL: https://github.com/KhronosGroup/glslang/blob/main/LICENSE.txt
  Byte-exact notice from the pinned ncnn20260526 full-source archive; archive provenance is third_party/ncnn/provenance.json.
- picosha2: Licenses/PicoSHA2-MIT.txt, SHA-256 937e3f1485e6fba313c4cf0e1279834d6d54916e22ea7b2c064b06dc6c28f8f1.
  Source URL: https://raw.githubusercontent.com/okdshin/PicoSHA2/161cb3fc4170fa7a3eca9e582cebd27cc4d1fe29/picosha2.h
  Complete MIT comment including copyright and disclaimer, extracted byte-exactly from lines1-23 of the pinned header. Full header SHA-256 b13c180161ffac8d0adc81e033e493c409457c4d1258ab9781ac80579ba3bdd8.
- unity-plugin-api: Licenses/Unity-Plugin-API-LICENSE.md, SHA-256 dc34d09c3c3a5057244ba4e6cbb871105ec42cd1130617aceeddb9219da922d1.
  Source URL: https://unity.com/legal/licenses/unity-companion-license
  Pinned Unity2021.3.45f1 PluginAPI copyright and Unity Companion License notice, copied byte-exactly; source release https://unity.com/releases/editor/whats-new/2021.3.45f1.
- nlohmann-json: Licenses/nlohmann-json-MIT.txt, SHA-256 86b998c792894ccb911a1cb7994f7a9652894e7a094c0b5e45be2f553f45cf14.
  Source URL: https://raw.githubusercontent.com/nlohmann/json/v3.11.3/LICENSE.MIT
  Complete MIT license fetched from immutable v3.11.3 tag and verified against third_party/json/provenance.json; not only the header SPDX identifier.

These are source notices for the SDK runtime. The Input runtime also uses the
Unity Plugin API notice, which is included separately in Input/Licenses. Input
native sources do not include nlohmann JSON or PicoSHA2. No runtime/model/native
code or repository license was changed by adding these notices.
