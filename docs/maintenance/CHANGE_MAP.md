# Change map

Run native filters with `pwsh -File tools/test/run_native_tests.ps1 -Filter FILTER`.
After focused checks, run the architecture guard.

| Need | Modify first / component README | Usually do not modify | Focused filter/check |
|---|---|---|---|
| Swap RTMO weights or compatible model | [pack](../../modelpacks/rtmo-t-416/README.md), then profile | Unity, Host, tracker | RtmoPlugin, ModelPack |
| Add pose algorithm | [pipeline](../../runtime/plugins/pipeline/rtmo/README.md) | Unity, common tracker | new plugin fixture + RuntimeSession |
| Add RKNN or another accelerator | [backend](../../runtime/plugins/backend/ort/README.md) | Unity, pose decoder | BackendPlugin, BackendFactory |
| Change identity association | [services](../../runtime/services/README.md), body_services.cpp | pipeline decoder | CommonServices |
| Change smoothing or prediction | [services](../../runtime/services/README.md), Sample/Observe | pipeline decoder | CommonServices |
| Change derived structural joints | [services](../../runtime/services/README.md), Derive | gameplay, observed model indices | CommonServices |
| Change native model index mapping | [pipeline](../../runtime/plugins/pipeline/simcc/README.md) | gameplay, common services | SimccPlugin |
| Change region masking/assignment | [services](../../runtime/services/README.md) | weights | CommonServices, RuntimeSession |
| Change skeleton appearance | [renderer](../../unity/HumanVisionDemo/Assets/HumanVision/Demo/README.md) | native recognition | managed compile, geometry tests |
| Add preset | [profiles](../../profiles/README.md) | source code | architecture guard + RuntimeSession |
| Change worker lifecycle | [Host](../../runtime/host/README.md) | model decoder | RuntimeHost |
| Release runtime | [release guide](RELEASE_GUIDE.md) | gameplay | full release gates |
