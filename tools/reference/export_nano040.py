from pathlib import Path
import sys,json,hashlib,urllib.request
root=Path.cwd();sys.path.insert(0,str(root))
from tools.reference.common import MMPOSE_DIR,MMDEPLOY_DIR,REFERENCE_IMAGE
from mmengine import Config
from mmdeploy.apis import torch2onnx
work=root/'out/models040-preflight/nano320';work.mkdir(parents=True,exist_ok=True)
url='https://download.openmmlab.com/mmpose/v1/projects/rtmpose/rtmdet_nano_8xb32-100e_coco-obj365-person-05d8511e.pth'
checkpoint=work/'checkpoint.pth'
if not checkpoint.exists(): urllib.request.urlretrieve(url,checkpoint)
deploy=Config.fromfile(str(MMDEPLOY_DIR/'configs/mmdet/detection/detection_onnxruntime_static.py'))
deploy.onnx_config.input_shape=[320,320]
deploy.onnx_config.dynamic_axes={'input':{0:'batch'},'dets':{0:'batch'},'labels':{0:'batch'}}
deploy.dump(str(work/'deploy.py'))
torch2onnx(str(REFERENCE_IMAGE),str(work),'end2end.onnx',str(work/'deploy.py'),str(MMPOSE_DIR/'projects/rtmpose/rtmdet/person/rtmdet_nano_320-8xb32_coco-person.py'),str(checkpoint),'cpu')
import onnx
model=onnx.load(str(work/'end2end.onnx'));onnx.checker.check_model(model)
(work/'provenance.json').write_text(json.dumps({'source':url,'checkpoint_sha256':hashlib.sha256(checkpoint.read_bytes()).hexdigest(),'onnx_sha256':hashlib.sha256((work/'end2end.onnx').read_bytes()).hexdigest()},indent=2))
print('Nano320 export complete')
