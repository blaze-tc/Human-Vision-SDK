# 0.3.0 preview delivery plan

User scope 2026-09-09 supersedes older milestone ordering and authorizes main/Release publication.
Existing rectangle dragging/resizing stays unchanged. No new runtime/unit/camera tests are run.

1. Fix Android live preview coupling and unnecessary readback/history work; bound ORT CPU threads and disable spinning. Preserve MP4 synchronized rendering. Expose source age and true inference throughput; do not claim measured phone performance.
2. Add same-snapshot six hand endpoints from existing wholebody small model. Preserve COCO-17 ABI; reject mismatched tensors. Palm is derived from hand-model root/MCPs, endpoints direct, invalid remains invalid.
3. Independent pooled joint/line renderer with thickness controls, video separate. Independent settings scene/controller with existing region editing logic.
4. Build Windows/Android native and managed assemblies. Produce importable unitypackage and Git UPM package with deterministic model preparation, metadata and license provenance.
5. Inspect package with Unity importer, review source, publish declared changes to main and versioned GitHub Release; verify remote ref and downloaded assets.

Progress: implementation, native/managed compilation and archive/hash checks complete. Fresh Unity import and phone performance remain unverified. Publication to main/Release is next.

## 0.3.0-preview.2 手机更新

- 两个场景共用安全区、横竖屏自适应 GUI。手机短边按480个界面单位布局，按钮高50单位；设置面板支持滚动。
- 相机启用自动旋转，修正90/270度纹理采样方向；预览与识别共用校正后的图像。启动时校准GPU回读行顺序。旋转后丢弃旧方向结果。
- 骨骼显示不再受固定350ms门限限制。HUD中的Bodies是原生结果人数，Visible是当前可显示人数，Age是源帧年龄。
- `HumanVisionRaisedHandDetector.cs` 是简单举手示例：regionIndex选择区域，比较有效手腕与肩膀的归一化Y坐标，输出左右手状态；默认忽略超过1500ms的动作结果。
- 现有相机场景的HumanVisionSceneControls会自动挂载举手组件；新建场景也已挂载。可在Inspector调整regionIndex、heightMargin、minimumConfidence和maximumPoseAgeMilliseconds。
- 更新Git依赖到新标签即可；不要同时导入unitypackage。若当前场景经过自行修改并删除了SceneControls，请手动挂载举手组件并指定manager。
- 未执行手机、摄像头、Unity运行测试；仅编译与包内容校验。请实机检查前后摄像头、横竖屏、身体与双手、举手状态，以及关闭Use regions后的全画面识别。
