import onnx,numpy as np,onnxruntime as ort
from pathlib import Path
p=Path('out/models040-preflight/nano320/end2end.onnx');m=onnx.load(str(p))
n=next(n for n in m.graph.node if n.name=='/Where_2');c=next(x for x in m.graph.node if n.input[2] in x.output)
a=next(a for a in c.attribute if a.name=='value');value=onnx.numpy_helper.to_array(a.t)
assert value.dtype==np.float32 and np.all(value==-1)
a.t.CopyFrom(onnx.numpy_helper.from_array(value.astype(np.int64)))
# Person-only model; downstream detector consumes dets, not class labels.
for output in list(m.graph.output):
 if output.name=='labels':m.graph.output.remove(output)
onnx.checker.check_model(m);target=p.with_name('person.onnx');onnx.save(m,str(target))
s=ort.InferenceSession(str(target),providers=['CPUExecutionProvider']);print([(x.name,x.shape) for x in s.get_outputs()]);print(s.run(None,{'input':np.zeros((1,3,320,320),np.float32)})[0].shape)
