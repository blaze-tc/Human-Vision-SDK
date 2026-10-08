# Unity API/settings final review (2026-10-08)

Fresh-context read-only reviewer confirmed the four findings were repaired:
CPU queries use the session observation clock; the exact published two-argument
texture submission overload remains; failed input cannot commit active options;
missing save files read scene Inspector defaults. Focused RED tests failed 0/4
before fixes; EditMode 41/41 and native PlayMode 8/8 passed afterward.

The additive SDK package retains every previously published managed/native/model
byte. New API/UI sources match canonical sources. Actual test-project CPU video,
region changes, screen/world mapping, safe stop and streaming destruction passed.
Offline imports, installed resource hashes, native initialization and final
Windows player build passed. No remaining new-code release blocker was found.

The broad regression is not a blanket pass: 226 PASS, 2 FAIL, 32 SKIP. The
missing-plugin test passes in its required native-absent installation (1/1).
The other failure is an unlocated Native Collection leak logged in a geometry
case; its source is not established. Legacy renderer runs encountered Unity
Camera.Render hangs/SIGSEGV. These records remain; their resolution is not claimed.
Physical-device, complete-hand and fresh per-person FPS acceptance remain pending.

## Preview 6 follow-up review

The user requested model-quality availability, device diagnostics and a smaller
main menu before publication. Independent read-only review raised four issues:
startup pruning before loading saved retention, compound URL tokens exposed in
logs, failed log-directory creation escaping to UI, and Android quality-profile
exceptions escaping capability discovery. All four were repaired. Startup does
not prune; loaded retention controls subsequent cleanup; compound tokens are
redacted; I/O failures remain visible without aborting settings; profile failures
return an explicit unavailable capability. Rotation retains configuration and
runtime headers and never deletes the current session.

Follow-up RED: 0/5 new contracts and 0/4 edge cases. Final packaged EditMode
50/50 and native PlayMode 9/9 passed, with no skips. Actual test-project CPU
video, 4-to-2 region/capacity changes, stop, streaming destruction and diagnostic
records passed. A fresh Windows player build returned exit 0. Real menu reflection
confirmed four main groups. The reviewer found no additional new-code blocker.

Preview 6 intentionally changes legacy Editor menu annotations only; native
payload, model assets, existing public managed signatures and GUIDs remain
preserved. Earlier broad-regression and physical-device limitations above remain.
