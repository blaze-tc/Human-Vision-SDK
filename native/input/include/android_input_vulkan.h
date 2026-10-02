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
#ifdef __cplusplus
}
#endif
#endif
