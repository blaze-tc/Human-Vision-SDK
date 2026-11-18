# Android RTSP AVC surface comparison

This standalone diagnostic uses the Input package's pinned Android FFmpeg
libraries and the synchronous NDK MediaCodec APIs. It is not part of either UPM
package and does not test Unity texture import or pose inference.

Publish the original Exercise-11 video over RTSP/TCP with `-ss 20 -c:v copy`,
ensure the phone can reach the URL, and build the diagnostic:

```powershell
pwsh -NoProfile -File tools/test/android_rtsp_surface_ab/build.ps1
adb install -r out/diagnostics/hlk-rtsp-20261118/probe/SurfaceAB.apk
adb reverse tcp:18566 tcp:18566
adb shell am start -W -n com.blazetc.humanvision.surfaceab/.ProbeActivity --es url rtsp://127.0.0.1:18566/Exercise-11 --es variants private,surface
```

The app captures one set of keyframe-aligned access units from FFmpeg without
decoding. It replays the same bytes and timestamps for both output choices with
the same decoder, dimensions, SDP extradata and packet schedule. `private` is
the released PRIVATE + GPU sampled reader with four acquired images allowed;
`surface` uses the visible Activity's ordinary SurfaceView. Capture the
`HVSurfaceAB`, `ACodec` and `MediaCodec` log tags. Keep the Activity visible for
the full replay. The reported outputs are startup evidence: this diagnostic
does not send EOS or count the codec's buffered tail.

Restart the Activity process and pass
`--es variants private,private-max3,private-max2,private-max1` to compare reader
capacities without changing the decoder surface type. See the
[HLK-AL00 device report](../../../docs/reports/2026-11-18-hlk-al00-rtsp-first-frame.md)
for the specific run and native candidate.
