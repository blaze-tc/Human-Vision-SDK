# RTMO body Android EP usability

- Model: `modelpacks/rtmo-t-416/body.onnx`
- SHA-256: `20aad6e2e42359cac1c5b4a0b2da00e29bfe91a72a782fdcf287d273a04c1b24`
- Checker package: ONNX Runtime 1.23.0
- NNAPI as-is coverage: 336/656 nodes (51.2%) across 25 partitions
- Nodes rejected because of dynamic shape: 268
- Checker fixed-shape estimate: 596/656 nodes (90.9%) across 23 partitions
- Official checker recommendation (as-is): NO
- Official checker recommendation (fixed-shape estimate): NO

The fixed-shape result is an analyzer estimate, not a converted model or device benchmark.
Partition count and coverage do not establish latency; the forced CPU/XNNPACK/NNAPI profiles
must still be measured on the same Android device and scene.

## NNAPI checker excerpt

```text
Checking NNAPI
25 partitions with a total of 336/656 nodes can be handled by the NNAPI EP.
Partition sizes: [7, 10, 28, 17, 21, 17, 103, 65, 4, 3, 3, 4, 1, 1, 3, 2, 1, 6, 6, 8, 8, 2, 4, 6, 6]
Unsupported nodes due to operator=60
Unsupported ops: ai.onnx:ConstantOfShape,ai.onnx:Cos,ai.onnx:Equal,ai.onnx:Erf,ai.onnx:Expand,ai.onnx:Less,ai.onnx:NonMaxSuppression,ai.onnx:Range,ai.onnx:ReduceMax,ai.onnx:ReduceSum,ai.onnx:Shape,ai.onnx:Tile,ai.onnx:TopK,ai.onnx:Where
Caveats that have not been checked and may result in a node not actually being supported:

     ai.onnx:Conv:Only 2D Conv is supported. Weights and bias should be constant.

     ai.onnx:Gather:Input indices should be constant if not int32 type.

     ai.onnx:Gemm:If input B is not constant, transB should be 1.

     ai.onnx:MaxPool:Only 2D Pool is supported.

     ai.onnx:Resize:Only 2D Resize is supported.

     ai.onnx:Split:Number of splits must evenly divide split axis size. Input split should be constant if provided.

     ai.onnx:Unsqueeze:Input axes should be constant.
Unsupported nodes due to input having a dynamic shape=268
NNAPI is not recommended with this model as there are 25 partitions covering 51.2% of the nodes in the model. This will most likely result in worse performance than just using the CPU EP.
Model should perform well with NNAPI as is: NO
--------
Checking if model will perform better if the dynamic shapes are fixed...
Partition information if the model was updated to make the shapes fixed:
23 partitions with a total of 596/656 nodes can be handled by the NNAPI EP.
Partition sizes: [130, 18, 33, 18, 143, 74, 19, 5, 6, 7, 23, 5, 4, 5, 3, 1, 2, 11, 39, 34, 4, 6, 6]
Unsupported nodes due to operator=60
Unsupported ops: ai.onnx:ConstantOfShape,ai.onnx:Cos,ai.onnx:Equal,ai.onnx:Erf,ai.onnx:Expand,ai.onnx:Less,ai.onnx:NonMaxSuppression,ai.onnx:Range,ai.onnx:ReduceMax,ai.onnx:ReduceSum,ai.onnx:Shape,ai.onnx:Tile,ai.onnx:TopK,ai.onnx:Where
Caveats that have not been checked and may result in a node not actually being supported:

     ai.onnx:Conv:Only 2D Conv is supported. Weights and bias should be constant.

     ai.onnx:Gather:Input indices should be constant if not int32 type.

     ai.onnx:Gemm:If input B is not constant, transB should be 1.

     ai.onnx:MaxPool:Only 2D Pool is supported.

     ai.onnx:Resize:Only 2D Resize is supported.

     ai.onnx:Split:Number of splits must evenly divide split axis size. Input split should be constant if provided.

     ai.onnx:Unsqueeze:Input axes should be constant.
NNAPI is not recommended with this model as there are 23 partitions covering 90.9% of the nodes in the model. This will most likely result in worse performance than just using the CPU EP.
Model should perform well with NNAPI if modified to have fixed input shapes: NO
```
