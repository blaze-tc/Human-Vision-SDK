#pragma once
#include "android_input_gpu.h"
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
}
