# RTMDet detector Android EP usability

- Model: `modelpacks/precision-t-26/detector.onnx`
- SHA-256: `a6e9039b481218cc0cfcccf1d03243b68af34c736f2cb774dcca39afce2e99c7`
- Checker package: ONNX Runtime 1.23.0
- NNAPI as-is coverage: 58/606 nodes (9.6%) across 12 partitions
- Nodes rejected because of dynamic shape: 517
- Checker fixed-shape estimate: 549/606 nodes (90.6%) across 19 partitions
- Official checker recommendation (as-is): NO
- Official checker recommendation (fixed-shape estimate): NO

The fixed-shape result is an analyzer estimate, not a converted model or device benchmark.
Partition count and coverage do not establish latency; the forced CPU/XNNPACK/NNAPI profiles
must still be measured on the same Android device and scene.

## NNAPI checker excerpt

```text
Checking NNAPI
12 partitions with a total of 58/606 nodes can be handled by the NNAPI EP.
Partition sizes: [10, 11, 11, 5, 4, 4, 1, 1, 3, 4, 1, 3]
Unsupported nodes due to operator=57
Unsupported ops: ai.onnx:ConstantOfShape,ai.onnx:Equal,ai.onnx:Expand,ai.onnx:HardSigmoid,ai.onnx:Less,ai.onnx:NonMaxSuppression,ai.onnx:Range,ai.onnx:ReduceMax,ai.onnx:Shape,ai.onnx:Tile,ai.onnx:TopK,ai.onnx:Where
Caveats that have not been checked and may result in a node not actually being supported:

     ai.onnx:Conv:Only 2D Conv is supported. Weights and bias should be constant.

     ai.onnx:Gather:Input indices should be constant if not int32 type.

     ai.onnx:GlobalAveragePool:Only 2D Pool is supported.

     ai.onnx:MaxPool:Only 2D Pool is supported.

     ai.onnx:Resize:Only 2D Resize is supported.

     ai.onnx:Unsqueeze:Input axes should be constant.
Unsupported nodes due to input having a dynamic shape=517
NNAPI is not recommended with this model as there are 12 partitions covering 9.6% of the nodes in the model. This will most likely result in worse performance than just using the CPU EP.
Model should perform well with NNAPI as is: NO
--------
Checking if model will perform better if the dynamic shapes are fixed...
Partition information if the model was updated to make the shapes fixed:
19 partitions with a total of 549/606 nodes can be handled by the NNAPI EP.
Partition sizes: [37, 45, 45, 41, 218, 24, 32, 7, 31, 11, 6, 19, 6, 4, 5, 7, 1, 6, 4]
Unsupported nodes due to operator=57
Unsupported ops: ai.onnx:ConstantOfShape,ai.onnx:Equal,ai.onnx:Expand,ai.onnx:HardSigmoid,ai.onnx:Less,ai.onnx:NonMaxSuppression,ai.onnx:Range,ai.onnx:ReduceMax,ai.onnx:Shape,ai.onnx:Tile,ai.onnx:TopK,ai.onnx:Where
Caveats that have not been checked and may result in a node not actually being supported:

     ai.onnx:Conv:Only 2D Conv is supported. Weights and bias should be constant.

     ai.onnx:Gather:Input indices should be constant if not int32 type.

     ai.onnx:GlobalAveragePool:Only 2D Pool is supported.

     ai.onnx:MaxPool:Only 2D Pool is supported.

     ai.onnx:Resize:Only 2D Resize is supported.

     ai.onnx:Unsqueeze:Input axes should be constant.
NNAPI is not recommended with this model as there are 19 partitions covering 90.6% of the nodes in the model. This will most likely result in worse performance than just using the CPU EP.
Model should perform well with NNAPI if modified to have fixed input shapes: NO
```
