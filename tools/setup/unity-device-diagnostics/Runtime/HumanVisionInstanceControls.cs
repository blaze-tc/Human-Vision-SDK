using System;
using System.Collections;
using HumanVision.Demo;
using UnityEngine;
using UnityEngine.UI;
namespace HumanVision.TestProject.Diagnostics {
 /// <summary>设备对照的显式实例开关。点击后重建运行时；界面总数仍以原生实际创建数为准。</summary>
 public sealed class HumanVisionInstanceControls : MonoBehaviour {
  [SerializeField] private HumanVisionSettingsController controller;
  [SerializeField] private HumanVisionCombinedBenchmark benchmark;
  [SerializeField] private Button button;
  private int requested=1;
  private bool applying;
  public void Configure(HumanVisionSettingsController c,HumanVisionCombinedBenchmark b,Button value){controller=c;benchmark=b;button=value;}
  private void OnEnable(){if(button!=null)button.onClick.AddListener(Change);}
  private void OnDisable(){if(button!=null)button.onClick.RemoveListener(Change);}
  private void Update(){if(button!=null&&controller!=null&&benchmark!=null){button.interactable=!applying&&!controller.Busy&&!benchmark.Running;
   int shown=controller.Sdk.ActiveAccelerationMode==HumanVisionAccelerationMode.Graphics?Math.Min(requested,2):requested;
   button.GetComponentInChildren<Text>().text="实例请求："+shown+"（切换）";}}
  private void Change(){if(applying||controller.Busy||benchmark.Running)return;
   // Vulkan retains one of three bridge slots for copy; only two consumers.
   int maximum=controller.View.ReadDraft().AccelerationMode==HumanVisionAccelerationMode.Graphics?2:3;
   requested=requested>=maximum?1:requested+1;
   Environment.SetEnvironmentVariable("HV_VULKAN_INSTANCE_COUNT",Math.Min(requested,2).ToString());
   Environment.SetEnvironmentVariable("HV_CPU_INSTANCE_COUNT",requested.ToString());
   Environment.SetEnvironmentVariable("HV_RKNN_INSTANCE_COUNT",requested.ToString());
   if(controller.Sdk.IsRunning)StartCoroutine(Apply());
  }
  private IEnumerator Apply(){applying=true;try{yield return controller.Apply(false);}finally{applying=false;}}
 }
}
