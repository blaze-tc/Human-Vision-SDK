# v0.4.0-preview.8 发布核验

日期：2026-10-14。用户要求检查、合并 main、发布 GitHub Release，并完善文档。
SDK **0.4.0-preview.8** / Input **0.1.0-preview.5**，按预发布评估版交付。
实现基线为 `9faef49c521b6197421a052057b2ac6be4578607`；最终源提交由 tag 和
附件 `source-snapshot.json` 记录。旧 preview.7 和已有本地测试 APK/资源不覆盖。

## 修改与接入

- Android CPU 使用单个 YOLOv8n-pose FP32 ONNX、512×288，支持 1–8 人。
  界面“固定模型”表示当前 CPU 只有一个输入档位；Vulkan 低/中/高与 RK3588 NPU 低档保留。
- CPU 先保留最多八个真实候选进入公共追踪，再按配置人数和区域选择，减少多人时身份反复更换。
- 初始化成功回调在配置提交后触发，可读取本次 `ActiveConfiguration`。Unity/V1 API 兼容。
- 保留原 Input、Vulkan/NPU 模型与驱动、Windows 库；本轮发布检查只补充文档和测试夹具隔离。
- README、Git/tgz/unitypackage 安装、ScriptableObject、API、CPU 档位、Settings 和测试说明统一到 preview.8。

最小使用、图像/2D/世界平面骨骼、人数、区域、稳定身份、启动/重启/停止、配置与代码示例见
[配置与 API](../user-guide/SDK_CONFIGURATION.md)。安装顺序为 Input → SDK，安装形式三选一。
官方样例保留 HumanVisionSettingsDemo；测试场景在检查后恢复为已保存的 Settings。

## 软件回归的实际结果

| 检查 | 结果与范围 |
| --- | --- |
| 完整原生 CTest | 465 项：432 通过、31 失败、2 跳过；不是全量通过 |
| 历史 GPU 数据缺失 | 31 项失败仍指向缺失的原始 golden/model/crop 数据；未生成替代值或降低断言 |
| 新 CPU 候选/配置合同 | 修正旧 CPU 按人数切换断言和并行临时文件覆盖后，3/3 通过（含真实 ONNX） |
| Human-Vision-SDK-Test 全量 EditMode | 462 通过、3 失败、40 跳过；不是全量通过 |
| Settings 场景测试 | 全量的两项错误来自对已保存场景改名；改用自有 preview scene 后单独 2/2 通过 |
| 缺原生插件测试 | 全量中专用“无插件项目”测试与当前完整测试项目前提冲突；实际无插件专用项目 1/1 通过 |
| 打包工具 | snapshot 15/15、CPU staging 16/16、CPU addon 9/9、原 addon 5/5，共 45/45 |
| 稳定公共 API / 架构 / 组件目录 | 检查通过；未改变业务 API 签名 |
| Git 包字节 | autocrlf=true 的包文件签出检查通过 |

完整原生命令：

```powershell
ctest --test-dir out/yolo-cpu-native -C Release --output-on-failure
ctest --test-dir out/yolo-cpu-native -C Release -R 'YoloCpuCandidateSelection|AndroidBenchmarkProfiles' -j 4 --output-on-failure
py -3.13 tools/package/test_package_release_snapshot.py
py -3.13 tools/package/test_main_cpu_yolo_stage.py
py -3.13 tools/package/test_main_cpu_android_addon.py
py -3.13 tools/package/test_local_android_addon.py
py -3.13 tools/maintenance/check_architecture_boundaries.py
py -3.13 tools/maintenance/generate_component_catalog.py --check
py -3.13 tools/package/check_public_surface.py
py -3.13 tools/test/verify_upm_git_newlines.py
```

首次并行原生全量出现共享临时文件的哈希/锁冲突，后续完整回归按串行记录。
新的 CPU 两个用例使用各自的目录，三项合同再以并行模式通过。两个 Settings 场景用例
在已保存场景和 Test Runner 未保存场景下都只操作自身的 preview scene。
分项复测通过不拼接成一次全量绿灯；40 项跳过与缺失数据不计入通过。
本地日志：`out/preview8-native-full-serial-release-console.log`、`out/preview8-new-contract-tests.log`、
`out/preview8-unity-editmode-evidence.json`、`out/preview8-unity-transient-preview-scene-result.json`、
`out/preview8-unity-no-native-results.xml` 与 `out/preview8-*-tests.log`。

## OnePlus 证据与硬件边界

同一最终本地 APK 依次运行旧 CPU、新 CPU、Vulkan 低档与新 CPU 镜像；每阶段预热 30 秒、
统计 30 秒，仅使用截图。输入为 1024×576、25 FPS 的 video-1.mp4，最多四人、无区域过滤。
按 ResultUpdated 的新 sequence/source frame 去重，逐人使用真实有效观察时间和完整17点统计。

| 模式 | 新结果 FPS | 主要四人真实骨骼 FPS | 主要四人完整17点 FPS |
| --- | --- | --- | --- |
| 旧 CPU | 7.16 | 4.80–7.16 | 0–0.30 |
| 新 YOLO CPU | 14.23 | 13.80–14.23 | 11.97–12.60 |
| Vulkan 低档 | 24.60 | 24.53–24.60 | 21.16–21.90 |
| 新 CPU 镜像 | 13.39 | 11.33–13.39 | 10.00–12.63 |

图像、骨骼与镜像截图已检查，未记录无效坐标。CPU 仍有少量身份替换。
CPU 未达到 20–25 FPS；Vulkan 数字只是短时窗口，不是长期稳定验收。
输入发布中位约 24.69 FPS；各阶段热状态/温度不同，不能将这一顺序比较作为严格同温速度/散热证明。
完整阶段耗时、统计和限制见[本地交付报告](2026-10-14-main-cpu-yolo-api.md)；
该报告保留发布前的本地交付记录，本次公共发布范围以本页为准。

已测 APK SHA256：`9e42ec27863bfcf220109b77763a763d04199ea7750ca069fa09d5e07a81e595`。
Android SDK ELF：`32ce9715ad8a6daae5b47dd5d628e6340f354715eca24c11b84f651407694804`。
RK3588 本轮未连接，NPU 驱动兼容、精度与性能不能以 OnePlus 结果替代。
世界坐标为世界平面映射；当前姿态模型没有启用真实手掌/指尖/拇指推理。

## 公共附件与完整本地部署

八个标准公共附件：两份 unitypackage、两份 tgz、README.md、asset-sha256.json、
source-snapshot.json、SHA256SUMS.txt。归档使用 `tools/package/release-preview8-authority.json`，
只从核验后的 SDK/Input 文件闭包生成，并保留完整 file/meta/GUID/native/resource 校验。
上传后下载到独立目录逐项核对 SHA256，匹配最终 main/tag 后发布为 prerelease。

**公共附件不含完整本地资源补充包、CPU YOLO ONNX、私有 RKNN 模型、librknnrt.so、用户视频和测试 APK。**
Android CPU/NPU 完整测试仍需本地 `HumanVision-LocalAndroidCpuResources-0.4.0-preview.8.unitypackage`；
部署步骤与导出工具见[本地 Android 资源](../user-guide/LOCAL_ANDROID_CPU_YOLO.md)。
“本地/私有”说明的是本次分发范围，不代表已确认厂家禁止分发的法律结论。
保留上游许可证、来源、local_evaluation_only、distribution_qualified=false、hardware_fps_acceptance=false。
本次发布没有完成闭源商用模型许可核验，也没有赋予新的模型许可。
