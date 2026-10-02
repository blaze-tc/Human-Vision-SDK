#pragma once
#include "android_input_gpu.h"
#include "humanvision_input.h"
#include "input_color_contract.h"
#include "IUnityGraphics.h"
#include "IUnityGraphicsVulkan.h"
#include <memory>
namespace hvinput {
IUnityGraphicsVulkan* InputUnityVulkan();
bool InputForeignEnabled();
void ConfigureInputRenderEvent();
bool ColorProbeEnabled();
bool QueueColorImage(AndroidDecodedImage&);
void NotifyRemovedBuffer(AHardwareBuffer*);
void DrainColorImagesBeforeReaderClose();
void InputRenderEvent(int);
void ShutdownInputColor();
bool BeginInputGpu(HV_InputHandle);
void CloseInputGpu(HV_InputHandle);
void DetachInputGpu(HV_InputHandle);
struct Session;
bool InputGpuProduction(Session*);
void PrepareInputGpuGeneration(Session*);
bool InputGpuRetired(HV_InputHandle);
int PollInputGpuMetadata(HV_InputHandle,uint64_t,HV_InputFrameInfo*);
}
