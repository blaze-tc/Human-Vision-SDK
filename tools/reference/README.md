# D0.1 OpenMMLab reference and ONNX contract

This directory reproduces the locked D0.1 RTMDet-tiny and RTMPose-s references. It does not ship with the Unity runtime.

## Locked sources

- MMDetection `v3.2.0`, commit `fe3f809a0a514189baf889aa358c498d51ee36cd`
- MMPose `v1.3.2`, commit `5408bc76f5b848cf925a0d1857899011d8c5b497`
- MMDeploy `v1.3.1`, commit `bc75c9d6c8940aa03d0e1e5b5962bd930478ba77`
- Detector config: `rtmdet_tiny_8xb32-300e_coco`
- Pose config: `rtmpose-s_8xb256-420e_coco-256x192`

The official MMDeploy `human-pose.jpg` is used for both models so the detector reference contains a real person and the pose reference consumes its detected ROI.

## Environment

Create a Python 3.10 environment at `.venv-reference`. The verified environment uses Python 3.10.21, Torch 2.1.0 CPU, MMCV 2.1.0, MMDetection 3.2.0, MMPose 1.3.2 and MMDeploy 1.3.1. See `environment.lock.txt` for the complete resolved environment.

The core environment was installed with these commands (run from the repository root):

```powershell
py -3.13 -m pip install --user uv==0.12.8
py -3.13 -m uv python install 3.10
py -3.13 -m uv venv --seed --python 3.10 .venv-reference
.venv-reference/Scripts/python.exe -m pip install torch==2.1.0+cpu torchvision==0.16.0+cpu --index-url https://download.pytorch.org/whl/cpu
.venv-reference/Scripts/python.exe -m pip install numpy==1.23.5 onnx==1.15.0 onnxruntime==1.23.2 mmengine==0.10.7 mmdet==3.2.0 mmdeploy==1.3.1
.venv-reference/Scripts/python.exe -m pip install https://download.openmmlab.com/mmcv/dist/cpu/torch2.1.0/mmcv-2.1.0-cp310-cp310-win_amd64.whl
.venv-reference/Scripts/python.exe -m pip install --no-build-isolation chumpy==0.70
.venv-reference/Scripts/python.exe -m pip install mmpose==1.3.2
```

`environment.lock.txt` is the exact resolved snapshot, rather than a cross-platform requirements file. The explicit commands above preserve the required CPU Torch and Windows MMCV wheel sources.

`chumpy==0.70`, required by MMPose, has an undeclared build-time dependency on pip and imports NumPy aliases removed in NumPy 1.24. Install it with build isolation disabled and pin NumPy 1.23.5. This is reference-tool compatibility only; it is not a runtime SDK dependency.

Python 3.10 cannot install ONNX Runtime 1.29.0 because that release has no `cp310` Windows wheel. The verified reference environment therefore uses the final official Windows `cp310` wheel, ONNX Runtime 1.23.2. The native D0.2 target remains ONNX Runtime 1.29.0.

## Prepare official sources and checkpoints

```powershell
New-Item -ItemType Directory -Force tools/reference/vendor, tools/reference/downloads | Out-Null
git clone --depth 1 --branch v3.2.0 https://github.com/open-mmlab/mmdetection.git tools/reference/vendor/mmdetection
git clone --depth 1 --branch v1.3.2 https://github.com/open-mmlab/mmpose.git tools/reference/vendor/mmpose
git clone --depth 1 --branch v1.3.1 https://github.com/open-mmlab/mmdeploy.git tools/reference/vendor/mmdeploy

curl.exe -L --fail --output tools/reference/downloads/rtmdet_tiny_8xb32-300e_coco_20220902_112414-78e30dcc.pth https://download.openmmlab.com/mmdetection/v3.0/rtmdet/rtmdet_tiny_8xb32-300e_coco/rtmdet_tiny_8xb32-300e_coco_20220902_112414-78e30dcc.pth
curl.exe -L --fail --output tools/reference/downloads/rtmpose-s_simcc-coco_pt-aic-coco_420e-256x192-8edcf0d7_20230127.pth https://download.openmmlab.com/mmpose/v1/projects/rtmposev1/rtmpose-s_simcc-coco_pt-aic-coco_420e-256x192-8edcf0d7_20230127.pth
```

## Run D0.1

Run from the repository root:

```powershell
.venv-reference/Scripts/python.exe -m unittest discover -s tests/reference -v
.venv-reference/Scripts/python.exe -m tools.reference.run_reference --device cpu
.venv-reference/Scripts/python.exe -m tools.reference.export_models --model all --device cpu
.venv-reference/Scripts/python.exe -m tools.reference.compare_onnx
```

`export_models.py` calls the official `mmdeploy.apis.torch2onnx` API. This avoids the unrelated visualization step in `tools/deploy.py`; on this pinned Windows stack the CLI exports a valid ONNX file, then its MMDetection visualizer rejects float labels. Direct API export produces the same ONNX graph without that post-export visualization failure.

The ONNX binaries and source checkpoints are ignored by Git. Committed `model_info.json` files contain exact source URLs, source commits, tensor contracts, preprocessing/postprocessing, and ONNX SHA-256 values. The committed golden JSON files contain reference outputs and comparison metrics.
