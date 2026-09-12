"""Independent OpenCV/ORT reference for the pinned Hand21 affine crop."""
import hashlib
import json
from pathlib import Path
import cv2
import numpy as np
import onnxruntime as ort

root = Path(__file__).resolve().parents[2]
model = root/'modelpacks/hand21/hand.onnx'
image = root/'tests/testdata/d0_1_human_pose.bgr'
pixels = np.frombuffer(image.read_bytes(), dtype=np.uint8).reshape(346,218,3)
# TopDown affine: center of [0,105,70,70], 1.25 padding, square input.
center = np.array([35.,140.], dtype=np.float32)
scale = 87.5
matrix = np.array([[256/scale,0,128-center[0]*256/scale], [0,256/scale,128-center[1]*256/scale]],dtype=np.float32)
crop = cv2.warpAffine(pixels,matrix,(256,256),flags=cv2.INTER_LINEAR,borderMode=cv2.BORDER_CONSTANT)
value = ((crop[:,:,::-1].astype(np.float32)-[123.675,116.28,103.53])/[58.395,57.12,57.375]).astype(np.float32).transpose(2,0,1)[None]
session = ort.InferenceSession(str(model),providers=['CPUExecutionProvider'])
outputs = dict(zip([o.name for o in session.get_outputs()],session.run(None,{session.get_inputs()[0].name:value})))
x,y = outputs['simcc_x'][0], outputs['simcc_y'][0]
points = np.stack([x.argmax(1),y.argmax(1)],axis=1)*.5*scale/256 + center-scale*.5
result = {'model_sha256':hashlib.sha256(model.read_bytes()).hexdigest(), 'image_sha256':hashlib.sha256(image.read_bytes()).hexdigest(),
          'roi':[0,105,70,70], 'landmarks':points.tolist(), 'thumb':points[4].tolist(), 'fingertip':points[8].tolist(),
          'palm':points[[0,5,9,13,17]].mean(0).tolist(), 'confidence':np.minimum(x.max(1),y.max(1)).tolist()}
target=root/'tests/testdata/hand040_golden.json'
target.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
print(json.dumps({key:result[key] for key in ('thumb','fingertip','palm')}))
