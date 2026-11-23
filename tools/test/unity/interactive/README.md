# RuntimePrefabAcceptance interactive controls

These payloads extend the existing private `Human-Vision-SDK-Test` Demo. They do
not create another SDK owner or replace the public UPM sample. Source selection,
configuration and lifecycle actions use the existing `view.Sdk` and `Run` owner.

Apply from the SDK worktree:

```powershell
py -3.13 -B tools/test/patch_runtime_prefab_interactive.py `
  --project E:/UnityProject/Human-Vision-SDK-Test `
  --backup E:/UnityProject/Human-Vision-SDK-Test/Builds/IndependentRuntime-20261123/interactive-before `
  --apply
```

Use a fresh backup directory for a later reviewed candidate. The helper validates
all source hashes, unique anchors, payload hashes and existing backups before
writing any asset. Recorded previous payload hashes support controlled upgrades;
unrecognized user edits are rejected. Existing `.meta` files are retained.

Without a fixture the Demo exposes interactive backend/model choices. A local
Editor fixture has this format:

```text
rtsp://127.0.0.1:18573/Exercise-11.mp4
1800
manual
```

`manual` keeps the controls visible and connects after preparation. The existing
two-line fixture retains the fixed automatic acceptance campaign. The runtime
probe file can still request native execution, preview and a Unity target FPS.

The **基础配置 / 输入源** panel edits RTSP, video and camera settings independently,
including input requests, mirror, execution preference, preview, confidence,
result ages, 1–8 people and equal/custom normalized regions. Connect, restart,
stop, unload, skeleton visibility, fullscreen and logs retain the existing Demo
actions. Settings are written to `Application.persistentDataPath` under
`RuntimePrefabAcceptance/interactive-settings.json`; they do not overwrite the
SDK Settings scene's configuration file.

Runtime limits remain visible:

- Video/camera use the existing Unity compatibility drivers. Their selection
  does not overwrite the remembered RTSP independent-execution preference.
- Windows native RTSP rejects mirror before changing the running SDK. NPU is
  unavailable on Windows.
- Windows CPU/DirectML are validated serially on the same SDK without opening
  input. Each probe waits for shutdown before the next. Installed model files
  alone do not claim a working Windows provider.
- Android Graphics is resource-qualified but marked as awaiting actual input
  validation: a closed compatibility context cannot certify native Vulkan.
  Android CPU/Neural retain their initialization probes. No device acceptance is
  implied.
- Windows independent input has no original-resolution exact photograph in this
  candidate. The photo button states that limit; compatibility mode retains the
  existing exact capture path.
- Preview FPS counts distinct preview frame identities. Native input and fresh
  result rates remain separate; unavailable statistics display as unknown.

Checks:

```powershell
py -3.13 -B -m unittest discover -s tools/test/tests -p test_patch_runtime_prefab_interactive.py
```

The staged Editor fixture is
`HumanVision.Tests.RuntimePrefabInteractiveAcceptanceTests`. Static C# compilation
and patch safety tests do not replace an actual Unity or physical-device run.

Editor verification uses the public string property `EditorVerificationCommand`
on the same component, only under `UNITY_EDITOR`. It accepts strict JSON up to
4096 characters. Optional `id` (at most 64 characters) correlates reports; it does
not deduplicate commands. Commands call the same GUI handlers and reject another
request while the existing owner is busy or a basic-settings draft is open.

```json
{"id":"gpu-high","action":"compute","mode":"gpu","quality":"high"}
{"action":"source","kind":"video","address":"E:/fixtures/Exercise-11.mp4"}
{"action":"fps","value":15}
{"action":"connect"}
{"action":"stop"}
{"action":"restart"}
{"action":"unload"}
{"action":"preview","enabled":false}
{"action":"capacity","value":8}
{"action":"regions","enabled":false}
{"action":"regions","enabled":true,"text":"0,0,0.5,1\n0.5,0,0.5,1"}
{"action":"skeleton","enabled":true}
{"action":"fullscreen","enabled":true}
{"action":"display","confidence":0.99,"ageMs":1000,"handAgeMs":200}
{"action":"save"}
{"action":"load"}
```

`compute` accepts cpu/gpu/npu and low/medium/high/ultra-low, subject to real
availability. `source` accepts rtsp/video/camera; camera permits an empty device
name for the default device. Unknown actions, extra or duplicate fields and wrong
types are rejected. Capacity accepts integers 1–8. Preview and regions apply
through the basic settings GUI path; capacity uses the existing `SetCapacity`
handler. Optional region text uses the SDK's normalized assignment rectangles
and validation; it does not crop analysis input. Skeleton and fullscreen reuse
the existing visibility and presentation behavior.
`display` requires all three numeric fields and applies through the same validated
settings path. `save` saves the current settings; `load` reads and applies saved
settings using the existing load and apply handlers, including the saved Unity
FPS cap.
The getter reports id/action/result/busy/sdkState/error and
redacted uiStatus/uiStatusChanged without returning an input address. `accepted`
means dispatched, not completed. Wait for busy=false, then inspect actual SDK
state, error, UI status and runtime diagnostics before judging acceptance.
