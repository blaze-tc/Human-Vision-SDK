# v0.4.0-preview.7 发布验证与边界

日期：2026-10-12。用户明确要求将 SDK 合并 main 并发布 Release。
SDK 0.4.0-preview.7，Input 0.1.0-preview.5；按预发布评估版交付。

## 本次内容

- ScriptableObject 默认配置，代码提供 Camera/RTSP/Video 输入地址。
- 统一生命周期、借用图像帧、2D/世界平面骨骼复制、人数、区域和稳定身份 API。
- 官方样例只保留 Settings，更新中文 API/初始化文档、Inspector 和菜单。
- 包含此前已实机回归的 Android 输入供帧、独立显示快照、RK3588 接口与诊断改动。
- 编辑器探针/快照清理按 EditMode/Player 分别释放；测试等待真实 GPU fence。
- 删除已移除 InputPreview 样例专用测试；未删除现行输入接口测试。

## 本轮重新验证

- 不可变公共快照 604 个文件（含 meta），17 个原生库（Android 8 SO / Windows 9 DLL）。
  文件哈希、GUID/程序集闭包、平台导入设置、实际 ELF/PE 依赖闭包、20 个 RuntimeData 文件通过。
- 快照序列化测试 15/15；V1 接口/架构、组件目录、Windows Git autocrlf 包字节检查通过。
- 新建 Unity 2021.3.45f1 项目，通过实际 AssetDatabase.ImportPackage 先导入 Input 并独立编译，
  再导入 SDK；生成 Settings scene/prefab/preset、检查字体和引用、初始化真实 Windows CPU 模型通过。
- 2022.3.61t4（Tuanjie 1.6.3）本地 tgz 安装、完整示例脚本编译、同一原生初始化检查通过。
  初次使用过长工作树路径触发 Unity 包缓存中的 DirectoryNotFoundException；改用短项目路径通过。
  Windows 项目建议使用较短路径。这不等于 stock Unity 2021.3.18 的本机验证。
- 当前测试项目全量 EditMode 初跑 406 通过 / 37 失败 / 40 跳过。
  补入真实公开 RuntimeData 为 ApprovedQualities、原生测试根目录，修正过时快照断言和编辑器清理后，
  重跑为 439 通过 / 4 失败 / 40 跳过：缺插件测试要求无插件专用项目；默认模式测试要求默认 Vulkan；
  兼容源码扫描测试在外部项目定位为 E:/upm；像素探针出现一次 fence 超时。
  随后独立 ModelReadbackTests 5/5 通过（14 项真实像素比较），快照绑定异步测试 1/1 通过。
  不将这些分项通过合并宣称全量绿灯。SDK 配置/查询/生命周期/Settings 四组 70 项在本轮全量结果中通过。
- 原生全量 CTest 重新运行：447 项中 414 通过、2 跳过、31 失败；失败仍指向迁移缺失的
  历史 out/c3-local-runtime / android-yolo golden/model 夹具。没有补造结果或降低断言。

## 设备及限制

OnePlus 前一轮同系列包的视频、多人人体、停止/重启与 RTSP 连接回归见
[SDK 配置报告](2026-10-12-sdk-preset-api.md)。RK3588 本轮未连接，稳定每人 20–25 或 30 个
完整新骨骼/秒未验收；世界坐标为平面映射；不承诺真实手端点。
热机视频长测曾出现一次自动恢复的 GPU copy reservation -1，根因未确认。
源暗光噪点、热降频、输入/结果/显示 FPS 和真实逐人骨骼 FPS 分开记录。

公共附件不含私有 librknnrt.so、私有 RKNN 模型、用户视频和诊断 APK。NPU 选择接口保留，
需另行部署适配资源；不能将 OnePlus 的 GPU 验证当成 RK3588 NPU 验收。
模型许可证、local_evaluation_only、distribution_qualified=false 和 hardware_fps_acceptance=false
原样保留。发布为 prerelease 不改变这些事实。

## 发布文件与复现

标准八个附件：两份 unitypackage、两份 tgz、README.md、asset-sha256.json、
source-snapshot.json、SHA256SUMS.txt。Input-first，安装方式不要混用。
采用 tools/package/release-preview7-authority.json，公共 Git 文件闭包生成干净源目录，
package_release_snapshot.py --source-root <clean-source> --authority <authority> --output <new-directory>。
检查真实 Git 安装、远端 main/tag 一致性及下载附件 SHA256 后发布。
完整原始日志保留在 out/release-preview7-*、out/sdk-packaging-20261012，未公开用户影像。
