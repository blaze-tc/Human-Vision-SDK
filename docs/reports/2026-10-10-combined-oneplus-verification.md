# 联合优化包与 OnePlus 验证

最终 APK：`HumanVisionSettingsDemo-Combined.apk`，SHA256：
`34f7b6f0afb42f8750f0c092ffe496ab9e12d5d6a672052842fdcf70f1d8d859`。
Unity 2021.3.45f1 Android ARM64 构建成功，0 错误、8 警告，APK CRC 通过；
已安装到连接的 OnePlus。RK3588 未连接，不能据此承诺 NPU 20–25 FPS。

## 最终界面与交互验证

- 左侧预览保留比例并扩大；顶部模式字可见，右侧硬件/日志字体已放大。
- 左下 FPS 改为不同输入发布帧和新来源帧结果计数，重复帧、插值和显示帧不计数。
  最终包截图 `final-vulkan-fps.png` 显示输入 24.6、新结果 23.6 FPS，视频与骨骼正常。
  这是即时全局结果更新率，不能替代逐人完整骨骼验收。
- 自动对照锁定配置时给出明确提示，右下按钮为“停止对照 / 解锁设置”。
  实机验证锁定期间可复制日志目录；13:40:16 UTC 有 `ui.copy.logPath` 记录，
  13:40:19 有 `interactive cancelled`，恢复原 Vulkan 视频并成功展开下拉框。
- 最终包日志：`session-20261010-133210-214-95ebe2b5`。
  截图 `final-lock-notice.png`、`final-dropdown-unlocked.png` 保存交互证据。
- FPS 去重/停流/重置测试 `2575772b` 1/1；组件退出解锁测试 `c4583b2f` 1/1。
  Unity 控制台错误 0；架构/公开接口检查及 Git 差异空白检查通过。

## 联合管线对照证据与限制

在最终 HUD/交互修正前、同一原生库和输入管线的包 `f00827c2…` 上，
`session-20261010-125040-886-c824db2b` 于 13:06:25 UTC 完成整套 OnePlus 对照：
6 个完整 RGB/镜像检查全字节一致；本地视频和 RTSP 的 CPU/Vulkan 各 60 秒；
Vulkan 视频持续 300 秒。后续重复测试已取消，分析器按阶段分开统计。
首轮分析无采集器/Unity 错误或数据质量警告。

管线分析的完成结果吞吐约为：CPU 视频 8.60 FPS、CPU RTSP 6.93 FPS；
Vulkan 视频 24.82 FPS、Vulkan RTSP 22.22 FPS、Vulkan 视频长测 24.66 FPS。
这些数字仅描述整帧结果完成速度。逐稳定 ID 的统计包含离开后的零窗口，视频
有多人及身份切换，RTSP 画面部分肢体不可见；本次不能宣布逐人完整 COCO-17
稳定 20–25 FPS 通过，更不能将其作为 RK3588 NPU 性能结果。

原始逐人数据和阶段耗时在 `oneplus-comparison.json`、`oneplus-pipeline.json`。
原生共享内存候选、GPU 快照、模型尺寸回读仍执行各自资格检查；RGA 保持关闭。
RK3588 一包一键测试方式见 `docs/user-guide/RK3588_COMBINED_COMPARISON.md`。
