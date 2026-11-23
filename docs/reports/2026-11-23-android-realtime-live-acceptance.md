# Android 独立输入、实时 Texture 与业务取点验收

## 范围和设备

用户恢复安装授权后，在 `E:/UnityProject/Human-Vision-SDK-Test` 的 RuntimePrefabAcceptance 构建并更新安装当前候选。设备为 OnePlus 9 Pro LE2120、Android 14、ARM64，Unity 2021.3.45f1、Vulkan、IL2CPP、min API 26。未修改业务游戏工程，未合并、推送或发布。

测试源为原始 Exercise-11 的 1080p30 H.264 RTSP。本机独立夹具端口18574，通过仅本轮持有的 ADB reverse 访问；不是业务摄像机。前序阶段预览请求1280×720，最终原图阶段已改为实际可见源尺寸1920×1080，见末节。没有RK3588硬件，不计RGA／RKNN设备通过；当前原生图像服务仍使用Vulkan。

## 预览及 Unity Texture

旧APK `6e440ebe5e36dce9a164a89502896f812f1a2bc16821c2b79bed521956d57609` 暴露出两个问题：5Hz CPU高清复制被误用为实时预览；超预算后永久暂停。CPU画面停在frame727，但识别继续。预算恢复补丁只作为旧路径兜底，不能满足实时Texture合同。

新路径从同一decoder输出独立的live和analysis有界槽池。live不等待推理、canonical结果或analysis claim；Unity在渲染线程通过GPU副本和真实完成fence取得live纹理。分析和live共享源身份与原始帧号，尺寸各自配置。精确拍照仍单独请求配对。

第二个APK `82b532d49af36d50b5147a1e3b2d6598a11283e0572775b3c6f2d2ccc32eedad` 真机暴露Unity 2021的另一问题：支持graphics fence但不支持async compute时，查询AsyncQueueSynchronisation.passed抛异常，导致画面为空。该候选也不计通过。

修复为CPU查询型 `CPUSynchronisation` fence，仍使用真实passed检查和原有槽位／租约退役条件，不强制完成。随后APK `9f41fe2619cf7ae4e3be697d87d4223619f46a004e0ec4fc72d0dc16cced68a9` 真机CPU／GPU低中高均恢复真实视频和骨骼，后续停止、重启、卸载和重连没有出现该异常。中档两张相隔约10秒的截图可见时间水印、人物姿态和帧号前进，颜色、方向和1280×720画面肉眼检查正常。

## 源输入与模型吞吐

以下为同源、同代、同epoch，排除启动5秒后的约18秒稳态尾窗；不是固定四人全关节性能SLA。所有窗口原生input/runtime errors为0。

| 模式 | 源解码FPS | live发布FPS | 送入识别FPS | 识别完成FPS | Unity新Texture观察FPS | Unity更新FPS |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| CPU低 | 30.59 | 30.59 | 30.59 | 16.59 | 27.08 | 30.04 |
| GPU低 | 29.99 | 29.99 | 25.36 | 25.36 | 22.95 | 约30 |
| GPU中 | 30.04 | 30.04 | 17.39 | 17.39 | 21.99 | 约30 |
| GPU高 | 29.94 | 30.00 | 10.09 | 10.09 | 26.40 | 约30 |

“送入识别”是管线入口／GPU claim计数。CPU有latest-pending队列，因此送入次数可高于实际完成次数；GPU当前按claim→处理→退休→下一次claim串行推进，稳态入口与完成速率接近。它们都不是相机解码FPS。此前Unity InputPublicationFps还只是主线程观察值，保留兼容字段但不再称源输入。

GPU中、高档的Texture更新快于推理完成，证明预览没有按骨骼结果硬限速。尚不能把Unity观察FPS当作屏幕实际呈现FPS，也未定位其与30FPS解码之间所有损失；共享物理GPU资源仍可能影响渲染／副本／模型速度。

CPU、中、高尾窗均有4人，但低档窗口包含无人和部分人数结果；识别完成包括空结果，不能写成四人完整骨骼FPS。样例96点／92线不等于每人完整32点，更不证明手掌／指尖／拇指模型输出。

独立分析及逐窗口行号、计数和输入哈希：`out/review-realtime-device-20261123/device-steady-and-stall-analysis.json`。原始截图、JSONL、logcat位于 `out/android-realtime-cpufence-20261123/`。

## 主线程停顿与生命周期

CPU低档注入真实Thread.Sleep，实测2.004535秒。source1／generation1／epoch1保持一致：decoded和acquired各+60，live／analysis／native input各+59，native fresh+35；Unity结果回调和交付各+0，原生错误均0。Unity恢复后第一条诊断仍见旧frame610，后一秒已见frame700；因此只证明约一秒内追上最新，不能宣称首张恢复画面立即最新。live模式的FramePresented计数是精确配对路径，不作为实时预览渲染计数。

停止后画面／点线清空、状态Stopped；重启恢复源6。卸载后SDK未创建、画面／点线清空；连接后源7恢复。GPU三档来回切换后仍有新纹理和结果，但切档当前会重开会话，不宣称切档零中断。

全屏等比画面与骨骼实跑；按需拍照产生1280×720 PNG，日志记录 `input_frame=1422`、`result_frame=1422`、`source=6`、`generation=1`。原色照片已拉回并检查，不把实时叠加当作精确同帧照片。此尺寸为当前请求，不是1920×1080原始分辨率照片。

## 普通业务 Update 取点与单预览

用户后续明确要求：Demo使用一次Update直接读取当前有效坐标绘制点线，单个最新Texture预览叠加区域框和编号。普通业务不应复制帧率统计、帧身份或精确照片配对条件。SDK已有 `TryGetJointNormalizedPosition` 等接口，内部处理有效性和来源；样例将使用这些现有接口，不新增业务API或修改原生调度。

取点实现已改为每次Update遍历槽位和关节，直接调用 `Sdk.TryGetJointNormalizedPosition`；SDK返回false就清对应点。去除样例中的analysis Texture、RuntimeProgress、帧号／新序号及180ms显示保留条件。精确拍照仍使用原配对合同。`HumanVisionGameplayExample` 也已改为每Update读取 `CopySkeleton2DByIndex`，结果事件只是可选方式。

真实旧私有样例先有7项合同失败，更新后SDK查询合同15/15；单预览组合旧0/6、中间5/6，补仅区域全屏后6/6；安全补丁28/28。公共／私有样例Windows和Android四路静态编译均通过，最终样例阶段实际Unity106/106通过。中间APK `321783a96da91dc24702710f28bdbd52400fe26bdc4954341450181478063b62` 显示单画面、区域与4人96点／92线。Windows实际Play也得到4人94点／89线和变化的纹理，SDK错误为空。首次启动等待数据的空快照保留，未记为稳定窗口通过。

用户之后要求预览靠右，左侧集中控制和日志。最终布局和原图验收见末节；旧双画面与中间居中单预览截图不作为新排版证据。

## 前序阶段自动化和候选身份

- SDK原生全量572/572；Input原生64/64；Android ARM64/API26两库编译、强符号闭包及独立依赖审计通过。
- 实际Unity EditMode 106/106；实际PlayMode默认GPU采样fence 1/1。新增GPU用例最初误放EditMode导致无法证明最终fence完成，并污染后续清理，失败XML保留；改用真实PlayMode帧后通过，没有放宽真实完成条件。
- 源计数合同16项、候选依赖审计工具29项、安全Demo补丁16项通过。定向测试不倒推历史391项整批通过。
- 已验证APK内9个SO及62项Runtime索引资产哈希；完整SDK启用RKNN厂商实现，不用stub冒充Neural资格。SDK SHA `0ad4d6adaa018224682b17d546a6710250d420e8e9be1e33a2ce62fcf99a3cdc`，Input SHA `5e7a217c924d0a86c01a77d760fddbc9bea37e00fc9a91143f54e13cc55c6576`。
- 此前Windows验收Editor保持可见、未冷重载，当时加载既有DML DLL；最终原图阶段已安全冷重载配套新库，见下文。

未计通过：RK3588／RGA硬件、长期热机、真实联合游戏GPU压力、原生相机／文件驱动、切档零中断、完整手部输出和每人30完整骨骼FPS。

## 2026-11-24 原图与左右布局首轮验收

此前实现只分离预览与推理的更新频率，仍把预览缩到1280×720，未落实用户的普通颜色Texture原图要求。本次将普通RTSP路径统一为0/0表示实际解码可见原尺寸：Windows独立及SDK默认兼容路径、Android独立live及兼容路径均覆盖。模型缩放留在识别内部，明确Analysis接口保留分析语义。填写的URL不自动替换成低码流。

每轴4096与有界内存为当前实现门槛；原图导入、分配失败明确返回错误，不静默缩小。独立可选预览错误保留健康分析，旧兼容模式保留其资源失败语义。0/0、部分零值拒绝、正上限兼容、4K几何及换代都有自动化覆盖；没有4K设备验收。

Windows通过配对库备份、哈希校验、旧Editor正常退出后复制、可见重新启动的工具完成冷重载，没有强杀进程或影响业务Editor。实际Play两次检查得到1920×1080、4人96点／92线和变化的像素hash。最终交付前再次Play：`windows-final-original.json` 到 `windows-final-original-moving.json` 纹理frame1068→2084、hash变化；后一份4人96点／92线，SDK错误为空。无人片段不计骨骼失败。

首轮原图APK：`E:/UnityProject/Human-Vision-SDK-Test/Builds/IndependentRuntime-20261123/sidebar-original-verified-20261124/RuntimePrefabAcceptance-Android.apk`，238426011字节，SHA-256 `ec788a1e7df115c95217de336e408019ff407ba035ac055a12acfd50d7d72d49`。已用保留应用数据的更新安装，设备内base.apk哈希核对一致，PID770的截图和JSONL对应此包；其拍照后保留缺陷由下节新候选修复。

| 配套库 | SHA-256 |
| --- | --- |
| Android SDK | `a68b3209804b422beffc272c4a1d11b6bef31a23b7e32f11d014ab3613881b83` |
| Android Input | `d53c66fb39cdcbd2f50972f86c38f7095e6851096810e1d11f5706b12118fc86` |
| Windows SDK | `33b637cfa068d9054dae89f9ae784b66883a877558876c83d8ce32ba01f1163e` |
| Windows Input | `385efcfd97e9d3890985eb6b367ede200796cf44d9dadda43c8e0ab6a00a0b94` |

手机独立GPU低／中／高档实际live均为1920×1080；中档模型640×384、analysis640×360，与原图分开。`original-medium-steady.png` 显示源解码约30、Texture25.2、识别完成17.4 FPS，4人92点／86线。后续回切独立中档时识别约11–12 FPS，源解码仍约30，原生input/runtime errors均0；差异未做单变量归因，本次不将单张截图写成持续性能保证。拍照后预览持续降速另有下节的明确缺陷和修复证据，不能归因于温度。

默认兼容模式也实际连接：`original-compatible-steady.png` 显示1920×1080持续预览、Texture24.3、4人96点／92线。兼容模式缺少原生SourceProgress时显示未知，不填假30。首次连接初始化期间的uninitialized／0骨骼快照保留；其后真实结果正常，不能把预热快照当最终结果。

最终界面右侧一个等比实时预览，区域框、编号和每Update查询的点线同画面。左上控制区和左下固定36%日志区分别滚动；实际拖动控制滚动条后，人数、重启／停止／卸载、骨骼开关和照片按钮可见，日志仍保持显示。基础配置使用ModalWindow，支持独立／兼容往返；骨骼开关、全屏等比及点线正常。

原图版实际精确拍照为1920×1080：日志`input_frame=4666 result_frame=4666 result_sequence=2000 source=4 generation=1`；保存文件已拉回为`exact-original-photo.png`。这与前序1280照片分开记录。

最终验证：SDK原生 `ctest --test-dir build/windows-test -C Release --output-on-failure` 576/576；Input原生66/66；真实Unity回归批次146/146（0失败、0跳过），前序实际PlayMode真实GPU fence1/1仍保留。新原图策略先RED后GREEN；GUI组合11项、SDK查询15项、安全补丁29项与public/private Windows/Android编译通过。最终APK九个SO、62项索引资产、API26强符号及真实RKNN依赖资格已核对。首次最终布局构建因验收工程根目录审计回执陈旧失败，更新为真实新回执后构建通过，没有伪造审计或跳过检查。

完整证据目录：`out/original-resolution-20261124/`；SDK全量日志`out/windows-original-source-ctest-final.log`，Input全量日志`out/input-original-resolution-reviewed-build-and-tests.log`。本地RTSP夹具有时限，过期后需重新启动或在基础配置填写真实摄像机地址；USB夹具依赖ADB reverse。Windows/手机本轮结论不代表RK3588、游戏联合GPU压力、长期热稳定或完整手部通过。

## 原图拍照结束后的保留槽修复

上节原图APK `ec788a1e…` 随后在精确拍照后发现持续预览降速，不能作为最终完整功能候选。按同source4／generation1／epoch1固定窗口，拍照前20秒Texture平均22.52 FPS，拍照后5–35秒5.38、35–65秒6.67；源解码对应29.77／30.01／30.18，原生错误增量0。这里既不是识别帧率，也不是旧CPU live预算限频。窗口选择、原始行号和源身份见 `out/original-resolution-photo-release-20261124/baseline-old-analysis.json`。

代码确认：RuntimeView结束MatchedColor切回Disabled时清除了自己的配对引用，却没有撤销Input consumer最后配对帧的protectedSlot。三个Unity目标槽中持续保留一个，直到换源。修复增加内部按source／generation／frame／resource token／Texture完整身份撤保；采样fence与SourceCopyLease仍须真实完成后才能归还槽，不强制释放或伪造fence完成。ClearFrame、Disabled及OnDisable均覆盖；旧帧清理不能撤销新配对保护。

真实Unity旧生产库上新增两例均RED：期望protected=-1、实际0。更新后完整批次150/150，0失败、0跳过；consumer定向正式用例13/13、Windows/Android八路静态编译及独立代码复审通过。未修改native二进制或布局。新候选APK SHA-256 `d9f9fa908940db0644075b05a9fdf1f41fc7153eb0db3ad34f4b39a02155f212`，路径 `E:/UnityProject/Human-Vision-SDK-Test/Builds/IndependentRuntime-20261123/original-photo-release-20261124/RuntimePrefabAcceptance-Android.apk`；构建0错误0警告、9个SO及62项Runtime索引资产核对，保留应用数据更新安装后设备base.apk哈希一致，PID6654。

普通模式下，旧文本HV_RT_SAMPLE的`bodies`、`skeleton_fps`、`paired_*`、`size`仍为精确配对字段，Disabled时可为0，不能用于判断实时骨骼是否绘制。五项HUD中的骨骼交付和JSONL `valid_skeleton_fps` 来自SDK有效结果，`joints/bones` 来自实际Update绘制，两者不依赖配对。

最终新包同源3／generation1／epoch1实测，没有通过重开源恢复吞吐。精确照片1920×1080，`input_frame=result_frame=3896`、`result_sequence=1632`，文件已拉回检查。固定窗口对照如下，所有窗口完整，原生input/runtime errors累计及增量均0，窗口内无E/F或Unity exception；不把相邻不同人数画面当固定四人性能SLA。

| 新包同一源窗口 | Texture观察FPS | 源解码FPS | 识别完成FPS | Unity更新FPS |
| --- | ---: | ---: | ---: | ---: |
| 拍照前20秒 | 24.55 | 29.97 | 15.99 | 30.07 |
| 拍照后5–35秒 | 22.73 | 29.97 | 11.11 | 30.06 |
| 拍照后35–65秒 | 22.37 | 30.01 | 10.78 | 30.07 |

本次未再出现拍照后持续5–7FPS，仍不保证恒定30FPS，也未单变量解释识别吞吐变化。约拍照后170秒的 `fixed-final-layout.png` 仍为同源3、1920×1080、4人96点／92线，截图HUD Texture26.2、源解码31.1、识别11.6；一秒统计可能波动，平均值以上表为准。

Windows新managed修复实际Play保持1920×1080，`windows-final-first.json` 为4人92点／86线；到 `windows-final-moving.json` 纹理frame235→3094、像素hash变化，SDK错误为空，后者无人片段清空骨骼。最终手机与Editor都保留可继续操作状态。

新包证据：`out/original-resolution-photo-release-20261124/`，包含 `apk-verified.json`、`install.txt`、`launch.json`、真实RED XML、`unity-green-150.xml`、`analysis.json`、同源截图／JSONL及原色照片。分析命令：`py -3 out/original-resolution-photo-release-20261124/analyze_photo_cadence.py`；统计脚本记录原始行号、哈希、字段、样本数和身份排除规则。

收口命令：`py -3 tools/maintenance/check_architecture_boundaries.py`（公开接口及架构通过）、`py -3 tools/maintenance/generate_component_catalog.py --check`（通过）、`py -3 -m unittest discover -s tools/package -p test_package_release_snapshot.py`（15/15）、`git diff --check`（通过）。实际150项由停止的验收Editor菜单 `Human Vision/Acceptance/Run Realtime Regression Batch` 运行，再以递归XML逐testcase确认，不能只看外层容器。Input66项CTest目录为 `out/input-native/windows`。收口时8090为Play且不在编译，手机PID6654继续运行；仅停止本轮logcat采集，没有停止预览。
