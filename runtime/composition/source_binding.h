#pragma once
#include "humanvision/humanvision_runtime_control.h"
#include "humanvision_input_native.h"
#include "common/frame_envelope.h"
#include "host/gpu_runtime_host.h"
#include <atomic>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace humanvision::runtime {
class RuntimeSession;
struct OwnedSourceConfig {
    uint32_t kind=0,flags=0,width=0,height=0,timeout_ms=5000,reconnect_delay_ms=1000;
    uint32_t transport_tcp=1,rotation_degrees=0,mirror_x=0;
    uint64_t source_id=0;
    std::string library,url,hardware_avc_codec;
};
bool CopySourceConfig(const HV_RuntimeSourceV1&,OwnedSourceConfig&,std::string&);
struct InputClockMapping {
    uint64_t clock_id=0;
    int64_t offset_us=0,uncertainty_us=0;
    bool valid=false;
    bool Map(const HV_InputFrameInfo&,int64_t input_us,int64_t& runtime_us) const noexcept;
};
bool BuildInputClockMapping(const HV_InputClockInfo&,int64_t before_us,int64_t after_us,InputClockMapping&) noexcept;
FrameEnvelope EnvelopeForNativeFrame(const HV_InputFrameInfo&,const InputClockMapping&,
    uint64_t source_id,uint64_t epoch) noexcept;

// Dynamic boundary keeps the source plugin independent of Runtime and models.
struct NativeInputApi {
    decltype(&HV_Input_Open) open=nullptr;
    decltype(&HV_Input_Close) close=nullptr;
    decltype(&HV_Input_Release) release=nullptr;
    decltype(&HV_Input_GetState) state=nullptr;
    decltype(&HV_Input_GetLastError) error=nullptr;
    decltype(&HV_Input_PollFrame) poll=nullptr;
    decltype(&HV_Input_CopyRgba) copy=nullptr;
    decltype(&HV_Input_QueryClock) clock=nullptr;
    decltype(&HV_Input_OpenNativeV1) open_native=nullptr;
    decltype(&HV_Input_GetNativeDeviceV1) device=nullptr;
    decltype(&HV_Input_GetNativePoolV1) pool=nullptr;
    decltype(&HV_Input_AcquireNativeFrameV1) acquire=nullptr;
    decltype(&HV_Input_WaitNativeFrameV1) wait=nullptr;
    decltype(&HV_Input_CopyNativeRgbaV1) copy_native=nullptr;
    decltype(&HV_Input_ReleaseNativeFrameV1) release_native=nullptr;
    decltype(&HV_Input_RequestNativeCaptureV1) request_capture=nullptr;
    decltype(&HV_Input_CopyNativeCaptureV1) copy_capture=nullptr;
};
struct NativeSourceProgress {
    uint64_t frames=0,generation=0,frame_id=0,errors=0;
    InputClockMapping clock;
};
class NativeSourceBinding {
public:
    NativeSourceBinding()=default;
    ~NativeSourceBinding();
    NativeSourceBinding(const NativeSourceBinding&)=delete;
    bool Open(const OwnedSourceConfig&,bool gpu_route,uint64_t epoch,std::string&);
    bool StartIngestion(RuntimeSession&,uint64_t epoch,std::string&);
    void StopIngestion();
    void Close(); // control owner; only after RuntimeSession/imports retire
    GpuConsumerSource* GpuSource() const noexcept {return gpu_source_.get();}
    NativeSourceProgress Progress() const noexcept;
    std::string LastError() const;
    const OwnedSourceConfig& Config() const noexcept {return config_;}
    const NativeInputApi& Api() const noexcept {return api_;}
    HV_InputHandle Handle() const noexcept {return handle_;}
    const InputClockMapping& Clock() const noexcept {return mapping_;}
    bool UsesGpuImageService() const noexcept {return android_native_;}
    uint32_t State() const noexcept;
private:
    bool Load(const std::string&,std::string&);
    void Run(RuntimeSession*,uint64_t epoch);
    void SetError(const std::string&);
    OwnedSourceConfig config_;
    NativeInputApi api_;
    void* library_=nullptr;
    HV_InputHandle handle_=nullptr;
    InputClockMapping mapping_;
    std::unique_ptr<GpuConsumerSource> gpu_source_;
    std::atomic<RuntimeSession*> runtime_{nullptr}; // session retires before source
    bool android_native_=false;
    std::atomic<bool> running_{false};
    std::thread worker_;
    std::vector<uint8_t> pixels_;
    std::atomic<uint64_t> frames_{0},generation_{0},frame_id_{0},errors_{0};
    mutable std::mutex error_mutex_,wait_mutex_;
    std::condition_variable wake_;
    std::string error_;
};
}
