# 2026-10-12 电脑摄像头 RTSP 低帧率定位与修正

## 结论

HP Wide Vision HD Camera 的驱动属性中，“照相机控制 → 低亮度补偿”处于勾选状态。
该控制允许自动曝光通过降低采集帧率提高暗光亮度。只关闭这一项、保留自动曝光、
分辨率和请求帧率不变，直接采集从约 15 FPS 恢复至 30 FPS，RTSP 恢复至约 29.6 FPS。
来源解释：[Microsoft UVC 自动曝光优先级](https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/ksproperty-cameracontrol-auto-exposure-priority)。

昨晚的约 10 FPS 与今天修正前约 15 FPS 属于不同时间/光照，不能当作严格 A/B；
本轮严格使用今天同样的 1280×720、MJPEG、请求 30 FPS，修改前后直接采集对照。
本轮证明此设置是当前采集低帧率的可修正原因；没有保存昨晚实际曝光值，不能复原
昨晚为什么恰好降到 10 而不是 15 FPS。

## 实测

| 路径 | 低亮度补偿开启 | 关闭后 |
|---|---:|---:|
| DirectShow 原始摄像头，720p MJPEG 请求 30 | 14.96 FPS | 30.05 FPS |
| 同一 RTSP，本机 TCP 接收 | 14.46 FPS | 29.60 FPS |
| OnePlus SDK 输入发布，最近 60 个约 1 秒窗口 | 昨晚仅预览 9.98 FPS（不同时间） | 28.45 FPS |

直接采集开启补偿：180 帧，首末 PTS 跨 11.968 秒。
关闭补偿：301 帧，首末 PTS 跨 9.984 秒。
RTSP 修正后：593 帧，首末 PTS 跨 20.000 秒。
统计 (N-1)/(末 PTS-首 PTS)，不是采用标称 FPS。录制使用 -c:v copy，没有输出补帧。
手机窗口 2026-10-12T01:00:45.6224440Z 至 2026-10-12T01:01:45.3058700Z，RTSP Vulkan 低档；输入均值
28.45、最小 20.79、最大 30.64 FPS。
Unity 均值 59.13 FPS。新 SDK 结果均值 27.09 FPS，
其中含无人/误检结果，不能作为逐人真实完整骨骼 FPS 验收。

## 实施范围与恢复

- 已在摄像头驱动属性关闭“低亮度补偿”，自动曝光仍开启。
- RTSP 服务测试时短暂停止以解除摄像头占用，测试后已恢复；VLC 未关闭。
- RtspServer 配置原文件与备份 SHA256 均为
  03a4fb18b0f3d4ae4ce619af0f0c132f19c40499e816c5f7cbaf4fbd16c8d23f，未改配置。
- 没有修改 SDK/APK；同一 APK 在手机端接收到接近 30 FPS 的输入。
- 手机当前草稿切换为 RTSP/Vulkan 低档用于验证，没有点击保存配置。
- 若需恢复原摄像头行为，在驱动属性 → 照相机控制中重新勾选“低亮度补偿”。
- 重插设备/驱动重置后设置能否保持，本轮未验证。暗光下关闭该项可能增加噪点或使
  图像偏暗，现场应同时保证光照和骨骼准确率，不用增益/插值虚增识别验收。

## 复现命令与证据

先停止占用摄像头的 RTSP 服务，再直接采集；完成后恢复服务。

```powershell
C:/ffmpeg/bin/ffmpeg.exe -hide_banner -nostdin -f dshow -video_size 1280x720 -framerate 30 -vcodec mjpeg -i "video=HP Wide Vision HD Camera" -t 12 -an -c:v copy direct-camera.mkv
```

摄像头属性入口是同样的 DirectShow 输入增加 `-show_video_device_dialog true`；
属性页提供低亮度补偿复选框。更改前/后截图分别为 camera-before.jpg / camera-after.jpg。
本机 RTSP 录制为 `-rtsp_transport tcp -i rtsp://127.0.0.1:554/videodevice -t 20 -an -c:v copy`。
使用 ffprobe `-select_streams v:0 -show_entries packet=pts_time -of json` 读取记录。
完整原始视频/命令输出位于工作树 out/rtsp-source-20261012/。
交付目录只复制必要摘要和配置前后截图，未公开发布摄像头录像。

此结论适用于当前 PC 摄像头链路。RK3588 现场网络摄像头需检查其各自曝光/慢快门设置，
不能假定同一个选项，也不能把本次输入提升当成 RK3588 NPU 提速证明。
