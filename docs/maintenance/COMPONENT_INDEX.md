# Component boundaries

Exact IDs, dependencies and test targets are generated in
[the component catalog](GENERATED_COMPONENT_CATALOG.md).

| Component | Inputs → outputs | Primary implementation | Allowed / forbidden dependencies | Dependents and first symptom |
|---|---|---|---|---|
| Plugin ABI | versioned C tables → validated contracts | runtime/include/humanvision_plugin.h | V1/V2 C types / STL across ABI forbidden | every plugin; load/layout failure |
| Host | config, copied frame → observation snapshot | runtime/host/runtime_host.cpp | ABI, frame slot / concrete algorithms forbidden | composition; queue/lifecycle errors |
| Registry/packs/profiles | IDs, JSON, hashes → compatible modules | runtime/host/*manager.cpp, plugin_registry.cpp | configuration utility / Unity forbidden | composition; missing pack or incompatible preset |
| Composition | semantic config/frame → canonical raw/sampled output | runtime/composition/session.cpp, runtime_c.cpp | registered plugins, Host, services / decoder logic forbidden | Unity; whole-session configuration failure |
| Backends | tensors → tensors + actual provider info | runtime/plugins/backend/ort/ort_plugin.cpp | runtime inference implementation / joints and tracking forbidden | pipelines; slow execution/fallback |
| RTMO pipeline | RGB frame → semantic body observations | runtime/plugins/pipeline/rtmo/rtmo_pipeline.cpp | backend service, preprocessing / Unity and global IDs forbidden | Host; boxes/joints geometrically wrong |
| TopDown/Hand pipelines | frame/ROIs → body/hand observations | runtime/plugins/pipeline/simcc/simcc_pipeline.cpp | backend service / global tracks or region ownership forbidden | Host; hand points absent or crop errors |
| Legacy adapter | frame → semantic legacy observations | runtime/plugins/legacy/legacy_pipeline.cpp | legacy model helpers, backend service / Unity forbidden | regression; old golden mismatch |
| Common services | semantic observations → identities, regions, sampled bodies | runtime/services/body_services.cpp, region_mask.cpp | ABI/frame utilities / tensor decoders forbidden | composition; ID swaps, stale points, excluded area detected |
| ModelPack | manifest + model bytes → validated assets | modelpacks/*/manifest.json | data only / executable source forbidden | profiles; hash/contract errors |
| Unity Runtime | semantic C ABI → reusable managed bodies | unity/.../Runtime/HumanVisionRuntimeSession.cs | Unity primitives, private interop / model filenames in gameplay forbidden | renderer/game; incorrect marshaling or result state |
| Renderer/demo | sampled canonical points + preview rect → one UI mesh | unity/.../Demo/Live/HumanVisionSkeletonGraphic.cs | stable Unity API, UGUI / inference libraries forbidden | scenes; line size, clipping or orientation mismatch |

Native test executables: humanvision_native_tests (real models and C API),
humanvision_plugin_tests (Host/services/registry), humanvision_plugin_c_contract
(C compilation/layout). V1 API contract tests remain mandatory.
