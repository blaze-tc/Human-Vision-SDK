# SDK 独立运行候选验收记录

日期：2026-11-23。基线 `69be01b92332dbcc9e1cd7ee1cd81d415418cd1b`。这是隔离工作树中的本地候选，尚未合并或发布。用户指定 `E:/UnityProject/Human-Vision-SDK-Test` 的 `RuntimePrefabAcceptance`，随后明确“暂不进行真机安装”；本轮未继续安装或操作手机。

## 当前交互 Demo 验收状态

用户取消了新增分析ROI裁剪，要求先验证上次独立运行优化，并补齐已有SDK基础交互功能，避免反复发布。当前已在指定测试工程的Unity 2021.3.45f1中完成可见视频／骨骼、Windows CPU／DirectML六组合及下述基础交互实跑。原生回归556/556、交互Editor测试57/57通过；这是桌面短窗口功能验收，不代表RK3588、Android真机、GPU压力或长时性能目标通过。

真实preview-on复现记录在 `out/unity-visible-preview-20261123/`：SDK已有2–3个可查询人体及有效骨骼观察，预览约4–5FPS，而Demo显示关节点／骨骼线为0。此前关闭预览的线程测试也没有验证可见画面。

- `de79862` 移除Windows分析RGBA预览的200ms固定限速，保留三槽、满槽跳过及复制预算；Android原色高清捕获的5FPS预算不变。两项新增测试先失败，再与相关回归36/36通过。
- 更新后全量native回归 `ctest --test-dir build/windows-test -C Release --output-on-failure`：556/556，62.30秒，日志 `out/unity-visible-preview-20261123/final-native-tests.log`。DML提供器6/6，Android对应SDK链接通过。
- 已安装同一套CPU／DirectML候选：SDK DLL SHA `7c0053421810e329378506de7889d3f20fc6a30eedcfa176bd7d6c17e20d8277`，配套ORT1.23 SHA `9a50320dc14df2321b8a51c5d5fb72d00b888506c70704dc5cce5584cbf5fe42` 与 `hv_dml.dll`。完整回执 `out/unity-visible-preview-20261123/dml-stage.json`，原库已备份。
- Demo骨骼绑定增加真实native source/generation/epoch确认，不再要求独立输入不存在的Unity分析纹理；保留fresh和0.18秒过期规则。冷加载后的实际运行已确认新字段及有效点线，见下文。
- 交互面板已加入三输入、后端与等级、执行策略、人数／区域、镜像限制、预览、停止／重连、设置存读和限帧。配置回放由同一 `view.Sdk` owner执行；Windows CPU／GPU采用不打开输入的真实初始化预检，不能仅因模型存在就宣称后端可用。
- Main、EditMode tests、Editor及Android条件编译先完成隔离静态检查；补丁安全及配置handler状态回归见 `out/interactive-csharp-check/`、`out/interactive-state-red/`、`out/interactive-state-green/`。随后实际Unity Test Runner交互测试57/57、0失败、0跳过，job `2259b9b0`，41秒；回执 `out/unity-visible-preview-20261123/interactive-editmode-result.json`。这些运行测试与此前静态检查分别记账。

验收Editor正常退出以更新原生库后，Windows重新启动返回 `WinError 740`（需要管理员权限）。用户已在Unity Hub手动打开该独立工程；没有绕过系统权限或操作授权对话框。

恢复后已通过8090实际运行：交互模式开启，CPU/GPU预检可用，4个可查询人体，Demo绘制96个点／92条线；`hasNativeEpochField=true`、绑定已确认，raw和sampled各4人。512×288纹理的亮度范围非均匀，连续帧hash改变，证明有真实视频内容。CPU低档显示预览约23FPS；GPU高档也记录到4人、有效骨骼与约30FPS预览。证据 `out/unity-visible-preview-20261123/` 和测试工程 `Builds/IndependentRuntime-20261123/VisibleRuntime/`。

另实际执行Unity暂停消费者2.2秒：预览帧保持不变，native输入增加67、新结果增加66，source/generation/epoch保持一致，随后恢复播放。该项是“无Unity消费仍持续处理”的验证，不等同于共享GPU负载、真实32点完整人体或设备热机验收。记录 `pause-consumers-result.json`。

当前配套库还完成了真正的主线程 `Thread.Sleep` 对照：15FPS下停顿501.481ms，native输入+6／新结果+3；30FPS下停顿2002.111ms，native输入+60／新结果+60。两段Unity回调／交付／渲染均+0，来源身份稳定，原生input/runtime错误均为0。该测试证明停顿期间仍能推进，不设定短窗口FPS通过门槛。完整日志与回执在 `out/unity-visible-preview-20261123/final-main-thread-stalls-retry/`。第一次控制脚本遇到进出Play的REST短暂重载而中止，未形成停顿结果；修正只读状态重试并补齐一次性manual夹具后完成这两项，没有为此修改SDK代码或重启Editor进程。

早期交互切换日志观察到CPU低／中／高及DirectML中／高的新结果、实际backend和人体，窗口由交互操作产生，不作性能A/B。随后已补齐以下受控六组合和基础操作；所有窗口都属于短时功能检查。

## Windows同包受控六组合

`out/unity-visible-preview-20261123/controlled-matrix/receipt.json` 的六组全部通过，每组7次快照。各组均核对实际backend、profile及native执行路径，source／generation／epoch在组内稳定，输入与新结果前进，纹理非黑且像素hash改变，并出现实际骨骼。`postcheck.json` 再核对各组快照错误为空及对应source 10–15的原生progress错误计数为0；原始probe为 `native-progress-source10-15.log`。

| 后端／等级 | 实际profile | 传输／预览尺寸 | 窗口秒数 | 输入FPS | 新结果FPS |
| --- | --- | --- | --- | --- | --- |
| CPU低 | `windows-yolo-cpu-low` | 512×288 | 12.13 | 28.68 | 25.30 |
| CPU中 | `windows-yolo-cpu-medium` | 640×360 | 12.11 | 28.91 | 22.72 |
| CPU高 | `windows-yolo-cpu-high` | 960×540 | 12.12 | 29.37 | 10.48 |
| DirectML低 | `windows-yolo-directml-low` | 512×288 | 12.12 | 28.89 | 26.99 |
| DirectML中 | `windows-yolo-directml-medium` | 640×360 | 12.14 | 29.65 | 28.75 |
| DirectML高 | `windows-yolo-directml-high` | 960×540 | 12.12 | 30.04 | 29.96 |

帧率由快照间计数差／实际时间计算，新结果包括无人观察；不是逐人完整骨骼FPS。各组合顺序运行，未做热机、同片段重复或游戏GPU负载控制，不能据此宣称性能提升比例或稳定30FPS。CPU高档在该窗口的新结果约10.48FPS，功能通过不等于目标吞吐通过。

传输尺寸与模型tensor尺寸不是同一合同：原始1080p 16:9素材在decoder内等比fit至档位边界，因此低／中／高输出512×288、640×360、960×540；模型输入分别是512×288、640×384、960×576，由pipeline等比缩放并居中letterbox补边。依据 `native/input/src/windows_rtsp_decoder.cpp` 的尺寸fit及 `runtime/plugins/pipeline/yolo/yolo_tensor_pipeline.cpp` 的预处理；中高档的高度差是预期行为。

## 基础交互与显示过滤实跑

`out/unity-visible-preview-20261123/controlled-basics/receipt.json` 记录17项检查：16项通过，早期本地视频观察窗口的可见骨骼条件未通过。该失败原样保留；后续有界多样本复验见 `controlled-basics-followup/receipt.json`，4项均通过，console错误0。验收命令通过仅Editor启用的入口分发到原GUI handler，只有accepted不算通过；回执同时检查执行后的SDK状态、纹理、结果及几何。

| 项目 | 实际结果与边界 |
| --- | --- |
| 骨骼／全屏 | 骨骼关闭清除点线、打开恢复，输入继续前进；全屏开启与恢复通过。 |
| 置信度／年龄 | 提高置信度后0点，恢复后96点；缩短人体有效年龄后清除，恢复后96点。关节显示改用真实SDK查询过滤，未更改模型或SDK算法。 |
| 独立预览关闭 | 无预览纹理期间input +60、新结果 +60，重开恢复像素与骨骼。关闭显示不停止识别。 |
| 人数／区域 | MaxBodies 1、8、4配置、结果容量边界、native／fresh通过；区域关闭、自定义区域应用及索引范围通过。非法区域被拒绝，原source及运行会话保留。容量8不代表素材中实际存在8人。 |
| Unity限帧 | 15／30／60目标均应用，各约2秒窗口input分别+61／+59／+60、新结果+60／+52／+61；证明这些窗口内原生持续工作，不是压力性能结论。 |
| 设置存读 | 已保存15FPS，改到60后加载重新应用15FPS，并保留输入源、native策略及容量。三输入草稿与RTSP独立偏好的handler回归包含在57项Editor测试内；实际Video→RTSP往返恢复独立策略及有效点线。 |
| 停止／重启／卸载 | 停止后无输入、点线清除、帧计数不增；重启恢复native新结果与点线。卸载清空owner且无输入，再连接重新创建并恢复。此处未做真实RTSP断网／恢复。 |
| 本地视频 | 兼容输入的7次后续快照均有4个raw人体、89–92条线，视频帧与新结果前进且错误为空。VideoPlayer仍依赖Unity，不宣称自主采集。 |
| 最终可操作状态 | 已恢复RTSP独立输入、30FPS、预览及骨骼开启、非全屏、非busy、manual交互，错误为空。相机实流本轮未运行。 |

早期 `controlled-basics/local-video.json` 和 `local-video-later.json` 的像素hash及结果序号都在前进，但raw与sampled人体均为0，因此该窗口的“visible”条件失败。JSON仅证明有效的空人体观察，不能单凭它断言画面无人，也不是有原始骨骼却漏画。随后 `controlled-basics-followup/video-window-0.json` 至 `video-window-6.json` 的4人／89–92线及无错误窗口通过；不删除或倒改早期失败回执。

LiveSkeleton显示过滤另有真实代码合同测试14/14：先9失败／5通过，再14通过；日志 `out/live-skeleton-filter-unit-red-final/receipt.json`、`out/live-skeleton-filter-unit-green-final/receipt.json`。该控制台测试的SDK I/O与Time是控制桩，不算设备验证。实际Unity上述置信度和年龄操作补足显示端实跑证据。查询使用sampled人体的真实SDK sequence／frame／timestamp，保留native来源及代次校验、fresh失效清除和最多0.18秒显示保留；实时叠加仍不是精确同帧照片。

Windows当前YOLO profiles的 `hands.enabled=false`。4人96点／92线是该档位现有有效关节的显示证据，不是每人完整32点，也未验证真实Hand／Handtip／Thumb或手部30FPS。独立手部年龄合同测试不替代启用手模型后的实流验收。

## 交互验收入口与平台限制

测试工程：`E:/UnityProject/Human-Vision-SDK-Test`，场景 `Assets/HumanVision/Samples/RuntimePrefab/RuntimePrefabAcceptance.unity`。Library夹具第三行 `manual` 表示保留后端／模型选项并自动连接本地验证流；原有两行格式仍用于固定参数的自动测量。

```text
rtsp://127.0.0.1:18573/Exercise-11.mp4
1800
manual
```

该地址是独立测试fixture，不是业务摄像机。现有流脚本 `out/run-shared-independent-rtsp4.py` 使用原始Exercise-11，以实时时序发布；有界运行4小时，通过同目录 `stop.request` 正常退出。以后重开验收应先确认fixture仍在运行，或在Demo选择本地视频／真实摄像机地址。

最终交付检查 `out/unity-visible-preview-20261123/final-delivery/receipt.json` 全部通过：Editor仍在Play且未暂停，Game窗口已打开；CPU低档、RTSP独立输入、4人容量、预览／骨骼开启、配置弹窗及全屏关闭、非busy、30FPS目标。最后3秒窗口input+89／fresh+82、纹理hash改变、94点／89线，实际预览计数约29.11FPS，SDK及Console错误0。这是取样时状态，点线数量会随检测与关节置信度变化。没有新建或替换业务工程，也没有Windows桌面捕获。

Library测试夹具在启动时一次性消费并删除；之后再次进入Play会保留交互界面，由“连接／应用输入”打开所选来源。独立测试流于本次约19:16开始、约23:16到期；若服务已停，可在“基础配置／输入源”选择视频并填写 `E:/UnityProject/YS-Sensory game project/测试资源/Exercise-11.mp4`，点击应用。该VideoPlayer路径是兼容模式；独立模式验收需要另行启动真实RTSP源。

| 项目 | 当前状态／余下边界 |
| --- | --- |
| 输入与画面 | RTSP独立输入、本地视频兼容输入已通过上述窗口；WebCamTexture入口复用现有驱动，相机实流待验。 |
| CPU／GPU及等级 | 同一配套库六组合通过，实际provider资格由无输入初始化／释放预检确认，不以模型文件存在替代。Windows NPU明确不可用。 |
| 配置与生命周期 | 上述实跑及57项Editor回归已完成；真实断网重连和长时重复重配未验。SDK facade尚无公开Pause／Resume；Demo的Stop／Restart／Unload按实际语义提供。 |
| 镜像／方向 | Windows native RTSP不支持的镜像／旋转明确禁用，不伪装为生效；兼容源沿用已有能力，本轮未做摄像机方向实流验收。 |
| 预览与照片 | 关闭／恢复独立预览已实跑。Windows native仅分析RGBA，原分辨率精确照片不可用并显示原因；未把实时骨骼叠加当同帧抓拍。 |
| Android资格 | CPU／Neural沿用真实预检；Graphics资源完整时标记“模型已安装，待实际输入连接验证”，避免用无输入／兼容上下文误禁native路径，也不提前标记硬件可用。 |
| 线程优化 | 15／30／60限帧、2.2秒暂停消费者及当前候选500／2000ms主线程真实停顿均有计数证据；见上文。GUI预览不能用FramePresented计数代替。 |

编辑器内部菜单 `Human Vision/Acceptance/Inspect Visible Runtime` 导出SDK数据、纹理采样hash／亮度范围、raw／sampled及Demo几何数量；它不捕获Windows屏幕，也不驱动推理。仅用于上述实际运行核对。

## 已实现范围

复用现有 RuntimeSession coordinator 与 CPU／GPU Host，由完成记录主动发布 canonical 结果。即使Unity不再提交、查询或显示，最后一帧也能完成发布。帧像素、来源、代次、配置、时钟及坐标变换随job保存；latest-pending和有界结果journal避免积压。

新增独立版本化 RuntimeControl 表，串行控制线程处理初始化、暂停、换源、重配和退役。Unity默认仍使用 CompatibleUnityInput；IndependentPreferred／IndependentRequired 对RTSP接入原生供帧，结果和Unity对象更新保持主线程。VideoPlayer／WebCamTexture保留兼容模式，当前不会把Unity纹理交接宣称为自主采集。

Windows RTSP可直接向CPU／DirectML后端供RGBA，不依赖Unity图形上下文。Android原生Input拥有自己的Vulkan device／queue和三槽RGBA AHB输出；CPU／RKNN可使用真实完成后的staging回读，NCNN使用来源中立的device identity和lease。这里的NPU图像准备仍使用GPU，不是RGA直连，也没有物理算力隔离。

预览使用独立CPU-owned mailbox，慢消费者不持decoder租约；精确抓拍有单请求、期限、取消和代次失效合同。Windows仅有分析RGBA时明确不支持原分辨率精确照片。Android高清捕获和分析仍可能共享一次GPU提交／fence，尚未证明高清操作对分析完全无影响；硬件预算gate保留待验收。

新图像服务／prepared-input扩展独立于旧Plugin ABI1／2／3。默认旧路径保持原模型预处理，独立策略显式启用扩展。CPU oracle和实模型tensor／人体输出对照通过后才接入Host。

## 已保留的可追溯证据

| 检查 | 观察与边界 |
| --- | --- |
| FrameEnvelope与Host元数据 | 跨64帧、显式crop及像素／元数据原子交接回归通过；旧ABI布局和语义保留。 |
| 图像服务与prepared管线 | 集中90/90；实际CPU YOLO的padded RGBA完整tensor及7人输出与旧路径一致。 |
| 控制生命周期与journal | 集中25/25；两个独立reader及跨暂停／重配的cursor不重置。 |
| Unity新接口 | Unity 2021.3.45f1，6/6，job `92def2bf`；兼容输入、错误与主线程交接合同。 |
| Unity真实像素 | 1/1，job `48adb296`；Graphics.Blit／ReadPixels验证持有F1时继续产生F2–F9不会覆盖已显示槽。未使用OS屏幕捕获。 |
| Windows原生CPU闭环 | 20.27秒、输入514、新结果222、错误0，无Unity DLL、无Copy／journal轮询。`out/independent-runtime-native-20261123/cpu-1/`。 |
| Windows原生DirectML闭环 | 20.14秒、输入530、新结果325、错误0，实际DirectML后端。`out/independent-runtime-native-20261123/dml-1/`。CPU／DML使用各自配套ORT版本，不能据此做同条件性能A/B。 |
| Unity中间候选停顿 | DLL `f47bbad…`，15FPS、无预览；19.685秒输入29.312FPS、新结果26.670FPS。主线程停顿2001.715ms期间输入和新结果各+60，Unity回调／交付／渲染均+0，原生及console错误0。完整日志在 `out/unity-independent-rpa/native-nopreview-15fps-stall2000-full/`；保留为中间候选证据，不替代最终候选停顿实测。 |
| 架构、公开接口、组件目录 | `check_architecture_boundaries.py` 和重新生成后的 `generate_component_catalog.py --check` 通过；当前六组合证据独立记录在上文。 |

早期通用原生回归命令为 `pwsh -NoProfile -File tools/test/run_native_tests.ps1`，554/554通过，69.16秒，日志 `out/independent-runtime-native-20261123/final-native-tests.log`。随后新增Windows预览限速回归，当前最终原生结果为上文556/556。Android ARM64/API26对应SDK链接成功。预览锁外分配的4项新增测试先全部失败，再与相关回归39/39通过；GPU停止后有效lease不得进入模型的新增测试先失败，再与相关回归58/58通过，均经独立复审。

包快照测试 `py -3.13 -B tools/package/test_package_release_snapshot.py` 15/15；Input独立依赖／GUID／当前载荷哈希检查、公开接口self-test和UPM换行检查通过。验收runner的新13项负向／清理回归及既有30项分析测试通过。扩展工具测试中7项AccountingTests因缺历史device-logcat夹具失败，不能记为通过。

历史Unity 391项整批job `b8800324` 已完成（361秒）：315通过、14失败、62跳过。离线原始结果在 `out/unity-independent-rpa/editmode-final-result-offline.json`，该失败记录保留。7项InputAdapter随后定向实际复跑25/25、0失败、0跳过（job `62276d2f`，117秒），回执 `out/unity-independent-rpa/input-final-result.json`；不能由定向通过倒推原391项整批通过。另7项的源码检查涉及旧Android CPU模型／profile分支、缺失历史stage或UPM仓库布局；UnitySkills仅保存计数及失败名称，没有原断言消息或堆栈，故这些只是定位线索，不能宣称原始失败根因已被完整重现和修复。

实现合同与原计划的命名有所收敛：复用Input现有Windows Poll／CopyRGBA接口，新增Android版本化NativeV1 lease／wait API；未另造重复的InputV2表。新控制表在生命周期接口外加入Submit、regions、diagnostics和预览／抓拍尾成员，保留前缀兼容。Windows公开decoded原分辨率未知时返回未知，不伪填分析尺寸。

真实RTSP素材为原始1080p30 `Exercise-11.mp4`，SHA-256 `daae54318ba3c2a793fcfefaf3b6df658d33fb6e4ef21acb79c0cf90176f41f6`。新结果包含无人观察；以上计数不证明逐人完整骨骼或手部30FPS。

## 尚未验收或未实现

- 目标RK3588的RKNN／RGA、实际BSP格式／stride／fence与热机吞吐；没有目标板，不能用OnePlus或Windows替代。
- RGA A1自主图像服务、A2共享NPU内存优化，以及原生本地文件／Android相机／Windows相机driver。
- Android自主Vulkan路径及高清预览的本轮真机运行；安装按用户指令暂缓。
- Unity 2022.3实际Editor执行、本轮GPU渲染压力与CPU压力矩阵、相机实流、断网恢复、真实1／2／4／6／8人及完整手部语义验收。当前Windows profiles关闭手模型。
- 5分钟预热后30分钟长时稳定性与最慢10秒、5% fresh FPS／20ms age p95性能gate；未完成项不能算通过。

SDK线程自主性与同一GPU、内存带宽、功耗及散热竞争分别评估。此次改造不承诺GPU硬分区或目标骨骼帧率达标。
