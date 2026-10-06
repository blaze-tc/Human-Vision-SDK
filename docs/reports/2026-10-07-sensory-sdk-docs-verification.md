# Sensory正式项目SDK文档核查（2026-10-07）

交付入口：[中文用户文档](../user-guide/README.md)。四份文档为首次安装引导、技术栈、平台测试效果、API调用与说明。

## 核查边界

正式项目Unity2021.3.45f1，SDK0.4.0-preview.4/Input0.1.0-preview.2，两包lock固定a201e0f44aa68a3f831f248b67400bd5fd7358c9。本次只读分析项目源代码、已安装包、留存运行JSON/截图/XML，编写仓库文档；未改游戏/SDK功能，未新建平台验收结论。

原开发checkout保持原分支和用户文件；文档从origin/main的f20206f在独立Codex工作区完成。发布只变更Markdown与脱敏JSON，不重新生成UPM、模型、插件或Release资产。

## 本次实际验证

| 验证 | 实际结果 |
| --- | --- |
| 项目lock与安装缓存比对 | Input127 + SDK325 = 452个包文件逐字节SHA-256相同，包含原生/模型文件；没有仅按版本文字推断 |
| 新文档与导航本地链接/标题锚点 | 全部存在；文档API目录锚点也检查 |
| API来源名称核对 | 167个选定公开成员名称在API文档中存在；语义另按实现人工核对。名称匹配不代替行为测试 |
| 文档C#示例编译 | Windows相机例、项目动作消费例2/2，以Unity2021.3及正式项目当前SDK/游戏程序集编译成功；未执行相机例 |
| `py -3.13 tools/maintenance/generate_component_catalog.py --check` | Component catalog: PASS |
| `py -3.13 tools/maintenance/check_architecture_boundaries.py` | Public surface contract: PASS；Architecture/documentation boundaries: PASS |
| `git diff --check` | 无空白错误 |

临时核查脚本/编译输出保留在本次独立工作区`out/doc-review`，不作为SDK软件实现发布。实际本地调用为：

```powershell
py -3.13 out/doc-review/verify_docs.py
pwsh -NoProfile -File out/doc-review/compile_examples.ps1
py -3.13 tools/maintenance/generate_component_catalog.py --check
py -3.13 tools/maintenance/check_architecture_boundaries.py
git diff --check
```

前两条依赖正式项目本地源码/缓存/Unity程序集，临时脚本不在GitHub源码中；不是读者clone仓库后即可运行的公共测试命令。架构检查则可在仓库运行。原始证据文件哈希与脱敏事实已纳入[项目基线摘要](../user-guide/evidence/sensory-project-baseline.json)。

## 实现事实复核

- 引导写明Input先于SDK、三种安装方式互斥、两个Git子目录均显式安装；没有要求清Library或编辑PackageCache。
- 新项目SDK菜单与正式项目Sensory Game菜单区分，直接运行Setting与Init到HurdleKing流程均说明。
- “应用请求接受”与源实际Streaming成功区分；保存草稿不改当前会话，ApplySave只在成功后保存。
- 当前PC416合同与Android512×288/640×384/960×576质量区分；采集请求与模型尺寸区分。
- RTSP预设的真实视频路径为`/video-1.mp4`，按钮只填URL，不启动发布服务。
- 32语义点、17+6兼容槽、有效性、派生点、独立时间、坐标、原始/采样结果、数组复用、GPU源退休及手序号事件含义均说明。
- 项目跑跳API、全部MotionSettings参数、角色区域下标、换人/跳跃序号消费与默认键盘混合模式均说明。
- 平台文档保留Windows一帧1人/可见骨骼和之后零身体记录；布局XML实际6/6，未把发现25项写成执行25/25。
- 历史AndroidFPS来自固定窗口完整观察数量，不能迁移成正式游戏APK验收；30FPS目标未达、真实手关闭、模型评估资格保留。

没有上传用户摄像头图像、视频、游戏原始源码、绝对日志路径或RTSP凭据。没有重跑原生/Unity/真机全回归：纯文档变更验证聚焦名称、示例编译、链接、证据身份和既有架构文档边界。
