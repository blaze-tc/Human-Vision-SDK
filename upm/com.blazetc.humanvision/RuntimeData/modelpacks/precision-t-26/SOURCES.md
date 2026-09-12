# Model sources

Official OpenMMLab model archive: https://download.openmmlab.com/mmpose/v1/projects/rtmposev1/onnx_sdk/rtmpose-t_simcc-body7_pt-body7-halpe26_700e-256x192-6020f8a6_20230605.zip

Code license: https://github.com/open-mmlab/mmpose/blob/main/LICENSE
Model accuracy and dataset usage terms remain those of the upstream model card.
No license or device-performance guarantee is inferred from the download.

Detector checkpoint: https://download.openmmlab.com/mmpose/v1/projects/rtmpose/rtmdet_nano_8xb32-100e_coco-obj365-person-05d8511e.pth
Exported with pinned MMDeploy/reference environment, input 320x320.
The exporter produced a float -1 sentinel on the int64 labels Where branch.
The sentinel was converted to int64 without changing its value; the unused labels
output was removed from this person-only graph. ORT loading and inference passed.
