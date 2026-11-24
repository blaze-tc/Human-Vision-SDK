# 0.4.0 正式发布前的配套验证

用户授权将本轮独立Runtime工作合并main、上传GitHub并发布正式版，SDK0.4.0与Input0.1.0使用同一标签v0.4.0。本文件记录发布前已完成的源码/包检查，不单独作为远端已发布证明；最终源提交和附件字节以Release的source-snapshot.json、SHA256SUMS.txt及实际远端元数据为准。

## 检查结果

| 检查 | 实际结果 |
| --- | --- |
| Windows公开ABI | 与69be01b已发布humanvision.dll实际PE逐项比较，旧39名称/ordinal全部保留；新增控制查询只追加ordinal40。旧35开发表造成的失败1/2保留，修复后实际DLL两例2/2。真实FFmpeg RTSP实现重新链接，未加stub或修改原实现。 |
| SDK原生全回归 | 恢复RTSP真实实现的Windows测试配置578/578，71.11秒；包括两条新增loaded-DLL ABI及错误合同回归。 |
| Input原生 | 66/66，正式版前重新运行。 |
| Unity业务取点/原图候选 | 本轮实际EditMode150/150，无失败或跳过；原图/按需同帧的真实设备候选过程另见设备报告。 |
| 新包UPM真实导入 | Unity2021.3.45f1新短路径项目u2/p：Input-only通过且未加载SDK；SDK+Samples编译、32项主Runtime与12项Windows Runtime哈希、17个native逐字节、真实CPU模型初始化全部通过。 |
| 新包离线真实导入 | 新项目o2/p：先Input unitypackage导入/重开验证，再SDK unitypackage导入/重开验证，所有上述检查通过。 |
| 排除旧DLL缓存 | 两安装方式均通过进程实际模块路径及SHA检查：SDK d1ceaaf1…、Input385efcfd…、ORT9a50320d…，不能仅凭磁盘替换或托管版本字符串算通过。 |
| Unity专项合同 | 两安装方式Compatibility3/3、SdkQuery18/18、NativePreviewOwnership6/6。未改断言的AndroidCpuContract在真实legacy资料与新CPUaudit下12/12，0跳过。 |
| Public Android资格 | 同源码独立RKNN-OFF构建fb18d3e0…；API26 ELF强导入/依赖闭包通过，CPU审计实核ORT1.23入口，Neural审计真实标记未编译/需本地SDK。 |
| 包结构 | 747个文件、402个GUID、8个Android SO/9个Windows DLL，Input无模型/推理依赖、两包GUID分区和完整字节索引闭合；模型分发/硬件FPS资格仍false。 |
| 维护/打包 | 架构与公开接口、组件目录、Input包隔离、Git autocrlf换行检查及快照单元15/15通过。公共安装/入门文档固定v0.4.0并修正从源码目录复制后错位的链接。 |

最终四主库：Windows SDK `d1ceaaf1d66e23329a057e8f6ac47c141159d45b5032ffcf9bd40b3b534cac0e`；Windows Input `385efcfd97e9d3890985eb6b367ede200796cf44d9dadda43c8e0ab6a00a0b94`；公共Android SDK `fb18d3e0efd915bf14ca93beebc478d292b5a81687ac29dec64aa5f93670ad77`；Android Input `d53c66fb39cdcbd2f50972f86c38f7095e6851096810e1d11f5706b12118fc86`。

## 保留的失败与定位

历史Unity391项结果315通过/14失败/62跳过保持原记录，不用专项通过倒推旧整批全绿。其中InputAdapter已在本轮定向通过；另两类旧fixture通过真实重现定位：旧CPU测试依赖已入库的legacy配置/ONNX而非当前YOLO默认profile，补充资料仅在out按原Git及原模型SHA重建，不改生产/模型/断言；剩余一项真实失败揭示公共CPU audit绑定旧SDK，重新审计新ELF后原测试12/12通过。兼容测试则在正确源码根环境下3/3通过。

临时tgz调用漏package/前缀、272字符路径触发旧Mono文件访问限制、离线import与probe同次启动未完成导入的失败都保留。正式snapshot打包工具原本已正确写package/；使用规范归档、新短路径项目和独立导入/验证进程后通过，没有放宽真实payload或模型检查。

新UPM路径747个归档文件/元数据逐字节核验通过。离线路径严格核验保留一项Unity导入行为差异：743条记录中739条exact，`.androidlib`内部4个子文件的`.meta`没有被Unity生成/保留；对应原始AndroidManifest/project.properties等文件及外层PluginImporter元数据都存在且正确，没有真实payload缺失或hash不符。不把该差异改写成“全部元数据一模一样”，原strict JSON保留。

证据目录为out/releases/040-work/与各包/审计输出。关键文件：sdk-native-578-abi.log、input-native-66.log、windows-export-compatibility-green.json、u2/p/sdk-import.json、o2/p/sdk-import.json、legacy-audit-green/HumanVisionAndroidCpuContractTests.xml及gate-audit.md。最终commit后另运行Git UPM固定revision核验；公共八附件必须从该commit重建，在GitHub草稿下载逐个核对后发布非prerelease/latest版本。相关远端收据保存在本轮out中。

## 原有修改和本地资料

main的四份旧方案修改已逐文件备份并以scoped stash `465e9404e2f27e68c4b07ca1faeb7669dfd10160`保留；不删除原工作树、缓存或用户归档。方案在最终源码中保留并补充后续实施/验收内容。

完整本地Android替代SDK包含74项模型/profile资源（原62及12项匹配桌面资源，重叠项原hash一致），RKNN-ON a68b3209…与vendor只本地交付；与公开Input0.1.0和最终Windows SDK配套。不上传私有模型/vendor、补充包、设备APK或业务视频。本地安装替换同id的公共SDK，避免第二份Assets native。跨栏王实际工程本轮只读，后续改造使用[完整升级提示词](../integrations/HURDLE_KING_SDK_040_UPGRADE_PROMPT.md)，不能据此宣称游戏或RK3588已经验收。
