# Preview.11 启动配置、示例与 Android 输入验证

SDK 0.4.0-preview.11 / Input 0.1.0-preview.6。启动优先级为显式参数、场景资产、项目默认资产、内嵌参数。
项目默认资产由 HumanVision → Create Default SDK Configuration 创建。Runtime Prefab Acceptance 是可选导入的 Package Manager Sample，场景保留实时图像、按区域 index 取骨骼及同帧重绘。

Android RTSP 在 GPU 和 RK3588 NPU 上共用解码、图像转换与发布。MediaCodec 按输出顺序释放，间隔至少 24 ms。
转换器等待 Unity 观察已完成纹理及输出槽可用，等待期间保留最新待转换图像。RKNN 仍使用既有读回与推理通道。

## 验证口径

- Unity 2021.3.45f1。启动预设 EditMode 14/14，通过 Package Manager 实际导入两个可选样例。Editor 播放 Exercise2 RTSP，核对项目默认资产的自定义区域、空区域以及有效区域中的关节点。
- Android ARM64/API 26 输入插件编译通过，帧槽和独立输出调度测试 9/9，40 个 Android Input C 导出一致。
- OnePlus 9 Pro Android 14，IL2CPP、Vulkan、1280×720 图像，YOLOv8n-pose 低档 512×288。两轮使用本地 RTSP TCP。帧身份先在 Prefab 发布时核对，再在游戏 Update 核对同一份已发布纹理和区域数据。

| 素材 | 配置人数 | 输入 FPS | 配对 FPS | 审核配对 | 错帧 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Exercise2 | 4 | 30.00 | 29.18 | 1,301 | 0 |
| DancingTest | 5 | 29.70 | 28.58 | 1,755 | 0 |

帧率取录屏结束后的可用稳定窗口，分别为 11.10 和 27.33 秒。DancingTest 按站位设置五个区域，共有 736 帧同时识别到五人。两段约 20 秒录像用于观察实际画面，单帧图像匹配仅作辅助。两个短测的采样热状态为 0，热机长期性能需单独测量。

私有 RK3588 补充包重新绑定了本次 Input SHA-256，模型和驱动字节沿用既有本地版本。已构建包含 RKNN 的双模式本地 APK。RK3588 设备的识别效率及驱动表现留给硬件验收。

## 复现命令

```powershell
ctest --test-dir out/android-input-release/host-v180 -C Release -R 'input.(OutputPump|FrameRing)' --output-on-failure
py -3.13 tools/package/test_package_release_snapshot.py
py -3.13 tools/maintenance/check_architecture_boundaries.py
py -3.13 tools/maintenance/generate_component_catalog.py --check
py -3.13 tools/test/verify_upm_git_newlines.py
py -3.13 tools/package/package_release_snapshot.py --authority tools/package/release-preview11-authority.json --verify-only
```

公共附件沿用评估资格和分发边界。设备录屏、测试 APK、RKNN 模型和驱动留在本地。
