# pipeline.simcc

Consumes frames and validated ModelPack dimensions through the C plugin ABI.
Uses injected backend sessions; emits semantic observations with source timestamps.
No global identity, region assignment or Unity dependency.

RTMO uses centered affine letterboxing, BGR 0-255 input and [N,17,3] output.
SimCC uses RGB normalized affine crops, Body26 or Hand21 SimCC coordinates.
TopDown detector runs every five accepted frames or on pose loss; ROI updates use
current pose observations. Hands require caller-provided ROIs and preserve request IDs.
Handtip is index fingertip, thumb is thumb tip; palm is derived from measured roots.

Real-model regression tests are offline functional checks, not mobile FPS proof.
