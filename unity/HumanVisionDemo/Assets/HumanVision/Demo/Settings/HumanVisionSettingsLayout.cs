using System;
using System.Collections.Generic;
using System.Linq;
using HumanVision.Demo;
using HumanVision.Input;
using UnityEngine;
using UnityEngine.EventSystems;
using UnityEngine.UI;

namespace HumanVision.Demo
{
    // 布局单独维护，避免业务绑定和场景生成代码交织。生成后为可手动修改的普通 UGUI。
    public sealed partial class HumanVisionSettingsView
    {
        public static HumanVisionSettingsView Create(Transform parent, Font font = null)
        {
            var canvas = new GameObject("HumanVision Settings Canvas",typeof(RectTransform),typeof(Canvas),typeof(CanvasScaler),typeof(GraphicRaycaster),typeof(HumanVisionSettingsView));
            canvas.transform.SetParent(parent,false); canvas.GetComponent<Canvas>().renderMode=RenderMode.ScreenSpaceOverlay;
            var scale=canvas.GetComponent<CanvasScaler>(); scale.uiScaleMode=CanvasScaler.ScaleMode.ScaleWithScreenSize; scale.referenceResolution=new Vector2(1600,900); scale.matchWidthOrHeight=.5f;
            var view=canvas.GetComponent<HumanVisionSettingsView>(); var fieldList=new List<InputField>(); var buttonList=new List<Button>();
            // 左侧 72% 给真实画面，状态只占底部一条；右侧 28% 控件铺满而非固定 100px。
            var previewPanel=InputPreviewCanvas.Panel(canvas.transform,"Preview panel",new Vector2(0,.16f),new Vector2(.72f,.92f));
            var image=new GameObject("Camera Video RTSP RawImage",typeof(RectTransform),typeof(RawImage),typeof(AspectRatioFitter));
            image.transform.SetParent(previewPanel,false); InputPreviewCanvas.Stretch((RectTransform)image.transform);
            view.preview=image.GetComponent<RawImage>(); view.preview.raycastTarget=false; view.preview.color=Color.black;
            var aspect=image.GetComponent<AspectRatioFitter>(); aspect.aspectMode=AspectRatioFitter.AspectMode.FitInParent; aspect.aspectRatio=16f/9;
            // SDK 的 MaskableGraphic 不要求 CanvasRenderer，UGUI 自定义图形必须显式添加。
            var skeleton=new GameObject("SDK Skeleton Overlay",typeof(RectTransform),typeof(CanvasRenderer),typeof(HumanVisionOverlay)); skeleton.transform.SetParent(image.transform,false); InputPreviewCanvas.Stretch((RectTransform)skeleton.transform);
            view.overlay=skeleton.GetComponent<HumanVisionOverlay>(); view.regionHandles=new HumanVisionSettingsRegionHandle[8];
            for(int i=0;i<8;i++) {
                var go=new GameObject("Region "+i,typeof(RectTransform),typeof(Image),typeof(HumanVisionSettingsRegionHandle)); go.transform.SetParent(image.transform,false);
                go.GetComponent<Image>().color=Color.clear;
                Color[] colors={Color.cyan,Color.yellow,new Color(1,.35f,.6f),Color.green,new Color(.7f,.5f,1),new Color(1,.6f,.2f),new Color(.3f,.7f,1),Color.white};
                var border=new GameObject("Visible wireframe",typeof(RectTransform),typeof(CanvasRenderer),typeof(HumanVisionSettingsRegionBorder)); border.transform.SetParent(go.transform,false); InputPreviewCanvas.Stretch((RectTransform)border.transform);
                border.GetComponent<HumanVisionSettingsRegionBorder>().color=colors[i]; border.GetComponent<HumanVisionSettingsRegionBorder>().raycastTarget=false;
                var label=InputPreviewCanvas.Label(go.transform,"区域 "+i+" → 角色 "+i); label.rectTransform.anchorMin=label.rectTransform.anchorMax=new Vector2(0,1); label.rectTransform.pivot=new Vector2(0,1); label.rectTransform.anchoredPosition=new Vector2(8,-8); label.rectTransform.sizeDelta=new Vector2(220,38); label.alignment=TextAnchor.MiddleLeft; label.color=colors[i];
                var shade=label.gameObject.AddComponent<Shadow>(); shade.effectColor=Color.black; shade.effectDistance=new Vector2(2,-2);
                var corner=new GameObject("Resize corner",typeof(RectTransform),typeof(Image)); corner.transform.SetParent(go.transform,false);
                var cornerRect=(RectTransform)corner.transform; cornerRect.anchorMin=cornerRect.anchorMax=new Vector2(1,0); cornerRect.pivot=new Vector2(1,0); cornerRect.sizeDelta=new Vector2(24,24);
                corner.GetComponent<Image>().color=colors[i]; corner.GetComponent<Image>().raycastTarget=false;
                view.regionHandles[i]=go.GetComponent<HumanVisionSettingsRegionHandle>(); view.regionHandles[i].Index=i; go.SetActive(false);
            }
            Action<Transform,string,string> button=(where,key,title)=> { var b=InputPreviewCanvas.Button(where,title,()=>{}); b.name=key; b.GetComponent<LayoutElement>().preferredHeight=58; buttonList.Add(b); };
            Action<Transform,string,string> field=(where,key,title)=> { var f=InputPreviewCanvas.Field(where,title,""); f.name=key; f.GetComponent<LayoutElement>().preferredHeight=58; fieldList.Add(f); };
            var top=InputPreviewCanvas.Panel(canvas.transform,"Mode bar",new Vector2(0,.92f),new Vector2(.72f,1)); var nav=InputPreviewCanvas.Row(top);
            button(nav,"Camera","Camera 相机"); button(nav,"Video","Video 视频"); button(nav,"RTSP","RTSP 推流"); button(nav,"FullscreenPreview","放大画面 / 拉框");
            var info=InputPreviewCanvas.Panel(canvas.transform,"Runtime status",new Vector2(0,.08f),new Vector2(.72f,.16f));
            view.status=InputPreviewCanvas.Label(info,"等待启动"); InputPreviewCanvas.Stretch(view.status.rectTransform); view.status.fontSize=18;
            var bottom=InputPreviewCanvas.Panel(canvas.transform,"Actions",Vector2.zero,new Vector2(.72f,.08f)); var bottomRow=InputPreviewCanvas.Row(bottom);
            button(bottomRow,"Apply","应用 / 重连"); button(bottomRow,"ApplySave","应用并保存"); button(bottomRow,"Stop","停止"); button(bottomRow,"Return","返回游戏");
            var right=InputPreviewCanvas.Panel(canvas.transform,"Settings panel",new Vector2(.72f,0),Vector2.one);
            var content=InputPreviewCanvas.Scroll(right); content.GetComponent<VerticalLayoutGroup>().childControlWidth=true;
            content.GetComponent<VerticalLayoutGroup>().spacing=8;
            InputPreviewCanvas.Label(content,"识别设置",34);
            var shared=RowBlock(content,96); var people=ColumnBlock(shared,"People settings"); var quality=ColumnBlock(shared,"Model settings");
            view.peopleChoice=Choice(people,"PeopleChoice","人数",Enumerable.Range(1,8).Select(i=>i+" 人").ToArray());
            view.qualityChoice=Choice(quality,"QualityChoice","模型等级",new[]{"准备中"});
            view.qualityHint=InputPreviewCanvas.Label(content,"实际模型合同加载中",48); view.qualityHint.fontSize=18;
            button(content,"UseRegions","按区域绑定角色");
            var regionTools=RowBlock(content,58); button(regionTools,"EditRegions","拉框 / 编辑"); button(regionTools,"ResetRegions","均分区域");
            view.sourceHint=InputPreviewCanvas.Label(content,"选择输入模式",42); view.sourceHint.fontSize=18;
            view.cameraPanel=ColumnBlock(content,"Camera source options").gameObject;
            view.cameraChoice=Choice(view.cameraPanel.transform,"CameraChoice","摄像头",new[]{"刷新设备列表"}); button(view.cameraPanel.transform,"SelectCamera","刷新摄像头");
            view.videoPanel=ColumnBlock(content,"Video source options").gameObject;
            view.videoChoice=Choice(view.videoPanel.transform,"VideoChoice","StreamingAssets 视频",new[]{"刷新视频列表"}); button(view.videoPanel.transform,"RefreshVideos","刷新视频列表");
            field(view.videoPanel.transform,"VideoPath","自定义视频路径（可留空使用列表）");
            view.rtspPanel=ColumnBlock(content,"RTSP source options").gameObject;
            field(view.rtspPanel.transform,"RtspUrl","RTSP 地址（已自动填入）"); field(view.rtspPanel.transform,"RtspHost","推流电脑 IP");
            var presets=RowBlock(view.rtspPanel.transform,58); button(presets,"BuildRtsp","电脑摄像头"); button(presets,"BuildRtspVideo","电脑视频");
            button(content,"Mirror","镜像");
            view.captureChoice=Choice(content,"CaptureChoice","采集分辨率",new[]{"640 × 480","1280 × 720","1920 × 1080","3840 × 2160"});
            view.captureChoice.SetValueWithoutNotify(1);
            var captureHint=InputPreviewCanvas.Label(content,"相机请求采集尺寸；视频 / RTSP 以源实际尺寸为准。",52); captureHint.name="Capture resolution hint"; captureHint.fontSize=18;
            var drawing=RowBlock(content,94); field(ColumnBlock(drawing,"Bone width"),"LineWidth","骨骼线宽 px"); field(ColumnBlock(drawing,"Joint diameter"),"PointSize","关节点 px");
            button(content,"Advanced","高级设置 / 日志");
            var advanced=ColumnBlock(content,"Advanced settings"); view.advancedPanel=advanced.gameObject;
            button(advanced,"AutoStart","Init 自动启动"); button(advanced,"WindowsCpu","Windows CPU"); button(advanced,"ResetQuality","重置模型等级为中");
            field(advanced,"FPS","采集 FPS");
            var regionDetails=ColumnBlock(content,"Region numeric settings"); view.regionDetailsPanel=regionDetails.gameObject;
            var selection=RowBlock(regionDetails,58); button(selection,"PreviousRegion","上一区域"); button(selection,"NextRegion","下一区域");
            var xy=RowBlock(regionDetails,94); field(ColumnBlock(xy,"Region X"),"RegionX","区域 X"); field(ColumnBlock(xy,"Region Y"),"RegionY","区域 Y（向下）");
            var wh=RowBlock(regionDetails,94); field(ColumnBlock(wh,"Region Width"),"RegionW","区域宽度"); field(ColumnBlock(wh,"Region Height"),"RegionH","区域高度"); button(regionDetails,"UpdateRegion","更新草稿区域");
            regionDetails.SetSiblingIndex(regionTools.GetSiblingIndex()+1); regionDetails.gameObject.SetActive(false);
            InputPreviewCanvas.Label(advanced,"日志设置",34); button(advanced,"DetailedLogs","详细骨骼日志");
            field(advanced,"LogInterval","统计间隔 s"); field(advanced,"PoseLogInterval","每人骨骼记录间隔 s"); field(advanced,"LogFileMB","单文件 MB（新会话）"); field(advanced,"LogSessions","保留会话数（新会话）");
            button(advanced,"OpenLogs","打开日志目录 / Android 导出"); button(advanced,"ExportLogs","导出 ZIP"); button(advanced,"CopyLogPath","复制日志路径"); button(advanced,"Save","仅保存草稿"); button(advanced,"Reload","重读保存配置"); button(advanced,"RestoreBackup","恢复备份为草稿");
            // 两个旧字段仅作为业务读写缓冲，用户使用下拉框，无需手动输入设备名 / 人数。
            var hidden=ColumnBlock(canvas.transform,"Choice values"); field(hidden,"MaxBodies",""); field(hidden,"CameraDevice",""); hidden.gameObject.SetActive(false);
            var qualityLegacy=ColumnBlock(hidden,"Legacy quality action"); button(qualityLegacy,"Quality","");
            view.fields=fieldList.ToArray(); view.buttons=buttonList.ToArray(); view.advancedPanel.SetActive(false); view.videoPanel.SetActive(false); view.rtspPanel.SetActive(false);
            foreach(var label in canvas.GetComponentsInChildren<Text>(true)) {
                label.font=font!=null ? font : HumanVisionUnityCompatibility.DefaultFont; label.raycastTarget=false;
                if(label.fontSize==22) label.fontSize=22;
                label.horizontalOverflow=HorizontalWrapMode.Wrap;
            }
            foreach(var input in view.fields) { input.textComponent.fontSize=24; input.textComponent.horizontalOverflow=HorizontalWrapMode.Overflow; }
            foreach(var b in view.buttons) { var label=b.GetComponentInChildren<Text>(); label.resizeTextForBestFit=true; label.resizeTextMinSize=18; label.resizeTextMaxSize=22; }
            if(!UnityEngine.Object.FindObjectsOfType<EventSystem>().Any(e => e.gameObject.scene == parent.gameObject.scene)) new GameObject("EventSystem",typeof(EventSystem),typeof(StandaloneInputModule)).transform.SetParent(parent,false);
            return view;
        }
        private static RectTransform ColumnBlock(Transform parent,string name)
        {
            var go=new GameObject(name,typeof(RectTransform),typeof(VerticalLayoutGroup)); go.transform.SetParent(parent,false);
            var group=go.GetComponent<VerticalLayoutGroup>(); group.childControlWidth=group.childControlHeight=true; group.childForceExpandHeight=false; group.childForceExpandWidth=true; group.spacing=6;
            return (RectTransform)go.transform;
        }
        private static RectTransform RowBlock(Transform parent,float height)
        {
            var row=InputPreviewCanvas.Row(parent); row.gameObject.AddComponent<LayoutElement>().preferredHeight=height;
            var layout=row.GetComponent<HorizontalLayoutGroup>(); layout.padding=new RectOffset(); layout.childForceExpandHeight=false; return row;
        }
        private static Dropdown Choice(Transform parent,string name,string title,string[] options)
        {
            InputPreviewCanvas.Label(parent,title,30);
            var go=DefaultControls.CreateDropdown(new DefaultControls.Resources()); go.name=name; go.transform.SetParent(parent,false);
            go.AddComponent<LayoutElement>().preferredHeight=58;
            var dropdown=go.GetComponent<Dropdown>(); dropdown.ClearOptions(); dropdown.AddOptions(new List<string>(options));
            dropdown.captionText.fontSize=22; dropdown.captionText.alignment=TextAnchor.MiddleLeft;
            dropdown.template.sizeDelta=new Vector2(0,320);
            var item=dropdown.itemText.transform.parent as RectTransform; item.sizeDelta=new Vector2(item.sizeDelta.x,58);
            dropdown.itemText.fontSize=22; dropdown.itemText.alignment=TextAnchor.MiddleLeft;
            // DefaultControls 的条目背景为白色；白字需要统一深色的 normal/selected/highlight 状态。
            var itemToggle=dropdown.itemText.GetComponentInParent<Toggle>(true);
            // 未提供默认 checkmark 图片时 Unity 会画白方块，改用小圆点作为选中标记。
            var checkmark=itemToggle.graphic;
            if(checkmark!=null) {
                checkmark.enabled=false;
                var markObject=new GameObject("Selected dot",typeof(RectTransform),typeof(CanvasRenderer),typeof(HumanVisionSettingsSelectionDot));
                markObject.transform.SetParent(checkmark.transform,false);
                var markRect=(RectTransform)markObject.transform; markRect.anchorMin=markRect.anchorMax=new Vector2(.5f,.5f); markRect.sizeDelta=new Vector2(12,12);
                var dot=markObject.GetComponent<HumanVisionSettingsSelectionDot>(); dot.color=Color.white; dot.raycastTarget=false;
                itemToggle.graphic=dot;
            }
            var palette=itemToggle.colors; palette.normalColor=new Color(.10f,.15f,.21f); palette.highlightedColor=new Color(.19f,.37f,.50f);
            palette.pressedColor=new Color(.13f,.30f,.43f); palette.selectedColor=new Color(.17f,.33f,.46f); palette.disabledColor=new Color(.10f,.15f,.21f); itemToggle.colors=palette;
            if(itemToggle.targetGraphic!=null) itemToggle.targetGraphic.color=Color.white;
            dropdown.template.GetComponent<ScrollRect>().scrollSensitivity=35;
            go.GetComponent<Image>().color=new Color(.12f,.17f,.22f); dropdown.captionText.color=Color.white;
            dropdown.template.GetComponent<Image>().color=new Color(.10f,.15f,.21f); dropdown.itemText.color=Color.white;
            var arrow=go.transform.Find("Arrow"); if(arrow!=null) {
                arrow.GetComponent<Image>().enabled=false;
                var mark=InputPreviewCanvas.Label(arrow,"▼"); InputPreviewCanvas.Stretch(mark.rectTransform); mark.fontSize=18; mark.alignment=TextAnchor.MiddleCenter;
                ((RectTransform)arrow).sizeDelta=new Vector2(24,24);
            }
            return dropdown;
        }
    }
}
