#pragma once
#include "humanvision_input.h"
// Additive Android-only target/event contract. Task3 struct layout and exports
// are unchanged. No Vulkan/decoder/backend types cross this input C boundary.
#ifdef __ANDROID__
#ifdef __cplusplus
extern "C" {
#endif
typedef void (*HV_InputRenderEvent)(int event_id);
/* Records a borrowed Unity Texture native pointer and its actual output
   contract. Unity objects stay on the main thread. BUSY retains the prior
   target while its GPU work is still in flight. */
HV_INPUT_API int HV_INPUT_CALL HV_Input_BindUnityTarget(HV_InputHandle,void* unity_texture,uint32_t width,uint32_t height,uint64_t generation);
HV_INPUT_API HV_InputRenderEvent HV_INPUT_CALL HV_Input_GetRenderEventFunc(void);
typedef struct HV_InputGpuFrame { HV_InputFrameInfo frame; uint32_t slot, reserved; } HV_InputGpuFrame;
HV_INPUT_API int HV_INPUT_CALL HV_Input_BindGpuTargets(HV_InputHandle,void* const textures[3],uint32_t width,uint32_t height,uint64_t generation,int rotation,int mirror);
HV_INPUT_API int HV_INPUT_CALL HV_Input_GetGpuGeometry(HV_InputHandle,uint32_t*,uint32_t*,uint64_t*);
HV_INPUT_API int HV_INPUT_CALL HV_Input_PollGpuFrame(HV_InputHandle,uint64_t after,HV_InputGpuFrame*);
HV_INPUT_API int HV_INPUT_CALL HV_Input_ReleaseGpuSlot(HV_InputHandle,uint32_t slot,uint64_t sequence);
HV_INPUT_API int HV_INPUT_CALL HV_Input_GpuRetired(HV_InputHandle);
HV_INPUT_API void HV_INPUT_CALL HV_Input_LogGpuCounters(void);
HV_INPUT_API void HV_INPUT_CALL HV_Input_SelectHardwareCodec(const char*);
HV_INPUT_API void HV_INPUT_CALL HV_Input_RetireGpuTargets(HV_InputHandle);
HV_INPUT_API int HV_INPUT_CALL HV_Input_GpuTargetsRetired(HV_InputHandle);
HV_INPUT_API int HV_INPUT_CALL HV_Input_GpuCopyActive(HV_InputHandle);
#ifdef __cplusplus
}
#endif
#endif
