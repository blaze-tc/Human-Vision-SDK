# Start here

0.4 keeps the V1 API and adds a semantic runtime API. Device performance remains
manual acceptance. The current implementation evidence is in
[DEVELOPMENT_STATUS](../DEVELOPMENT_STATUS.md).

1. Read this file.
2. Use [CHANGE_MAP](CHANGE_MAP.md) to identify the component.
3. Read only that component README and its focused tests.
4. Modify that bounded component.
5. Run its focused tests.
6. Run `python tools/maintenance/check_architecture_boundaries.py`.
7. Run full regression/package checks only when preparing a release.

For weights, begin at [ModelPack guide](MODEL_PACK_GUIDE.md); do not modify Unity,
Host or tracking. For a new algorithm, begin at [plugin guide](PLUGIN_DEVELOPMENT.md);
do not expose tensors to gameplay. For drawing, begin at the
[Unity renderer README](../../unity/HumanVisionDemo/Assets/HumanVision/Demo/README.md);
do not alter native inference. Each row in CHANGE_MAP links directly to the
component README and its test filter, so the destination is at most two hops away.

Caches and user archives are retained by user instruction. Do not clean them as
part of a maintenance change. [Release guide](RELEASE_GUIDE.md) owns publication.
