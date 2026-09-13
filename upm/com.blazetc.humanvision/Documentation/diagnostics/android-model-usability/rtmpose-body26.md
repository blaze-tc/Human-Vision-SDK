# RTMPose Body26 Android EP usability

- Model: `modelpacks/precision-t-26/body.onnx`
- SHA-256: `7d0075fbc44b2e696af21ac4555333fbb43405bf8dcc8d44c17c98d23818c710`
- Checker package: ONNX Runtime 1.23.0
- NNAPI as-is coverage: 35/168 nodes (20.8%) across 6 partitions
- Nodes rejected because of dynamic shape: 131
- Checker fixed-shape estimate: 161/168 nodes (95.8%) across 8 partitions
- Official checker recommendation (as-is): NO
- Official checker recommendation (fixed-shape estimate): NO

The fixed-shape result is an analyzer estimate, not a converted model or device benchmark.
Partition count and coverage do not establish latency; the forced CPU/XNNPACK/NNAPI profiles
must still be measured on the same Android device and scene.

## NNAPI checker excerpt

```text
Checking NNAPI
6 partitions with a total of 35/168 nodes can be handled by the NNAPI EP.
Partition sizes: [2, 1, 2, 5, 2, 23]
Unsupported nodes due to operator=7
Unsupported ops: ai.onnx:HardSigmoid,ai.onnx:ReduceSum,ai.onnx:Shape
Caveats that have not been checked and may result in a node not actually being supported:

     ai.onnx:Conv:Only 2D Conv is supported. Weights and bias should be constant.

     ai.onnx:GlobalAveragePool:Only 2D Pool is supported.

     ai.onnx:MaxPool:Only 2D Pool is supported.

     ai.onnx:Split:Number of splits must evenly divide split axis size. Input split should be constant if provided.

     ai.onnx:Squeeze:Input axes should be constant.

     ai.onnx:Unsqueeze:Input axes should be constant.
Unsupported nodes due to input having a dynamic shape=131
NNAPI is not recommended with this model as there are 6 partitions covering 20.8% of the nodes in the model. This will most likely result in worse performance than just using the CPU EP.
Model should perform well with NNAPI as is: NO
--------
Checking if model will perform better if the dynamic shapes are fixed...
Partition information if the model was updated to make the shapes fixed:
8 partitions with a total of 161/168 nodes can be handled by the NNAPI EP.
Partition sizes: [31, 26, 26, 35, 5, 4, 8, 26]
Unsupported nodes due to operator=7
Unsupported ops: ai.onnx:HardSigmoid,ai.onnx:ReduceSum,ai.onnx:Shape
Caveats that have not been checked and may result in a node not actually being supported:

     ai.onnx:Conv:Only 2D Conv is supported. Weights and bias should be constant.

     ai.onnx:GlobalAveragePool:Only 2D Pool is supported.

     ai.onnx:MaxPool:Only 2D Pool is supported.

     ai.onnx:Split:Number of splits must evenly divide split axis size. Input split should be constant if provided.

     ai.onnx:Squeeze:Input axes should be constant.

     ai.onnx:Unsqueeze:Input axes should be constant.
NNAPI is not recommended with this model as there are 8 partitions covering 95.8% of the nodes in the model. This will most likely result in worse performance than just using the CPU EP.
Model should perform well with NNAPI if modified to have fixed input shapes: NO
```
