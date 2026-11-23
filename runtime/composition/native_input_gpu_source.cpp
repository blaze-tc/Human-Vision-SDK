#include "composition/native_input_gpu_source.h"
#include "composition/native_frame_geometry.h"
#include <algorithm>
#include <chrono>
#include <limits>
#if defined(__ANDROID__)
#include <poll.h>
#endif
namespace humanvision::runtime {
NativeInputGpuSource::NativeInputGpuSource(HV_InputHandle input,NativeInputGpuApi api,int64_t offset,
 uint64_t source_id,uint64_t epoch,void* context,Arrival arrival,uint64_t clock_id)
 :input_(input),api_(api),offset_(offset),source_id_(source_id),epoch_(epoch),clock_id_(clock_id),context_(context),arrival_(arrival){}
bool NativeInputGpuSource::Initialize(std::string& error){
 if(!input_||!api_.device||!api_.pool||!api_.acquire||!api_.release||!source_id_||!epoch_){error="Incomplete native GPU source contract";return false;}
 HV_InputNativeDeviceV1 device{};device.size=sizeof(device);device.version=1;
 pool_.size=sizeof(pool_);pool_.version=1;
 if(api_.device(input_,&device)!=HV_INPUT_OK||api_.pool(input_,&pool_)!=HV_INPUT_OK||
    pool_.slot_count!=3||!pool_.width||!pool_.height||pool_.width>4096||pool_.height>4096||pool_.format!=1||
    !device.ahb_enabled||!device.sync_fd_enabled){error="Native GPU device/pool is unavailable or incompatible";return false;}
 for(auto buffer:pool_.hardware_buffers)if(!buffer){error="Native GPU pool has a missing AHB";return false;}
 identity_.queried=true;identity_.vendor_id=device.vendor_id;identity_.device_id=device.device_id;
 std::copy_n(device.device_uuid,16,identity_.device_uuid.begin());std::copy_n(device.driver_uuid,16,identity_.driver_uuid.begin());
 if(gpu::MatchDevice(identity_,{{0,identity_}}).status!=gpu::DeviceMatchStatus::Matched){error="Native GPU identity has invalid UUIDs";return false;}
 error.clear();return true;
}
gpu::SlotResult NativeInputGpuSource::Claim(gpu::ConsumerFrame& out) noexcept {
 if(quarantined_||failed_)return gpu::SlotResult::Closed;
 HV_InputNativeFrameV1 frame{};frame.size=sizeof(frame);frame.version=1;
 const auto status=api_.wait?api_.wait(input_,sequence_,50,&frame):api_.acquire(input_,sequence_,&frame);
 if(status==HV_INPUT_NO_FRAME||status==HV_INPUT_BUSY)return gpu::SlotResult::NoReady;
 if(status!=HV_INPUT_OK){++errors_;failed_=true;return gpu::SlotResult::Closed;}
 out={};out.claimed=true;out.producer=this;
 out.token={frame.slot,frame.frame.generation,frame.frame.sequence};
 out.metadata.generation=frame.frame.generation;out.metadata.frame_id=frame.frame.sequence;
 out.metadata.source_id=source_id_;out.metadata.config_epoch=epoch_;
 out.ahb_buffer=reinterpret_cast<uintptr_t>(frame.hardware_buffer);out.producer_fd=gpu::SyncFd(frame.producer_sync_fd);
 const auto decoded=frame.frame.decoded_timestamp_us;
 if(decoded<0||decoded>std::numeric_limits<int64_t>::max()-std::max<int64_t>(offset_,0)||
    decoded+offset_<0||frame.frame.sequence>uint64_t(std::numeric_limits<int64_t>::max())||
    (clock_id_&&(frame.frame.clock_id!=clock_id_||frame.frame.clock_domain!=HV_INPUT_CLOCK_NATIVE_MONOTONIC))) {
  // No GPU consumer has run. Producer publication itself proves completion,
  // but reject invalid clocks instead of manufacturing a timestamp.
  ++errors_;failed_=true;
  if(RetireConsumer(out,gpu::CompletionProof::GpuQuiescent)!=gpu::SlotResult::Ok)QuarantineConsumer(out);
  return gpu::SlotResult::Invalid;
 }
 out.metadata.timestamp_us=out.metadata.capture_steady_us=decoded+offset_;
 auto& envelope=out.source_envelope;
 envelope.source_id=source_id_;envelope.generation=frame.frame.generation;envelope.frame_id=static_cast<int64_t>(frame.frame.sequence);envelope.config_epoch=epoch_;
 envelope.source_timestamp={decoded+offset_,1,FrameClockDomain::kRuntimeMonotonic,FrameTimeSource::kDecoded,true};
 const auto claimed_us=std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
 envelope.submitted={claimed_us,1,FrameClockDomain::kRuntimeMonotonic,FrameTimeSource::kSubmitted,true};
 envelope.decoded={decoded,frame.frame.clock_id,FrameClockDomain::kInputMonotonic,FrameTimeSource::kDecoded,true};
 envelope.received={frame.frame.received_timestamp_us,frame.frame.clock_id,FrameClockDomain::kInputMonotonic,FrameTimeSource::kReceived,frame.frame.received_timestamp_us>=0};
 envelope.presentation={frame.frame.presentation_timestamp_us,0,FrameClockDomain::kMediaPresentation,FrameTimeSource::kPresentation,frame.frame.pts_valid!=0};
 envelope.width=static_cast<int32_t>(pool_.width);envelope.height=static_cast<int32_t>(pool_.height);envelope.crop={0,0,envelope.width,envelope.height};
 envelope.format=HV_PIXEL_RGBA32;envelope.plane_count=1;envelope.planes[0]={0,static_cast<int32_t>(pool_.width*4),4};
 envelope.color_matrix=FrameColorMatrix::kRgb;envelope.color_range=FrameColorRange::kFull;envelope.completion=FrameCompletion::kReady;
 if(!BindNativeImageGeometry(frame,envelope)){
  ++errors_;failed_=true;
  if(RetireConsumer(out,gpu::CompletionProof::GpuQuiescent)!=gpu::SlotResult::Ok)QuarantineConsumer(out);
  return gpu::SlotResult::Invalid;
 }
 sequence_=frame.frame.sequence;generation_=frame.frame.generation;
 if(frame.slot>=3||!frame.frame.generation||!sequence_||
    frame.hardware_buffer!=pool_.hardware_buffers[frame.slot]||frame.frame.width!=pool_.width||frame.frame.height!=pool_.height){
  ++errors_;failed_=true;
  if(RetireConsumer(out,gpu::CompletionProof::GpuQuiescent)!=gpu::SlotResult::Ok)QuarantineConsumer(out);
  return gpu::SlotResult::Invalid;
 }
 if(arrival_)arrival_(context_,frame);
 return gpu::SlotResult::Ok;
}
gpu::SlotResult NativeInputGpuSource::RetainGeneration(gpu::ConsumerGeneration& out,void (*retain)(uintptr_t) noexcept) noexcept {
 if(quarantined_||!retain||!generation_)return gpu::SlotResult::Closed;
 out={};out.generation=generation_;out.contract.width=pool_.width;out.contract.height=pool_.height;
 out.contract.actual_format=pool_.format;out.contract.actual_usage=pool_.usage[0];out.contract.camera_session=source_id_;
 for(uint32_t i=0;i<3;++i){out.retained_ahb[i]=reinterpret_cast<uintptr_t>(pool_.hardware_buffers[i]);retain(out.retained_ahb[i]);}
 return gpu::SlotResult::Ok;
}
gpu::SlotResult NativeInputGpuSource::RetireConsumer(gpu::ConsumerFrame& frame,gpu::CompletionProof proof) noexcept {
 if(!frame.claimed||frame.producer!=this||proof!=gpu::CompletionProof::GpuQuiescent)return gpu::SlotResult::Invalid;
 if(api_.release(input_,frame.token.index,frame.token.generation,frame.token.frame_id)!=HV_INPUT_OK)return gpu::SlotResult::Busy;
 frame.producer_fd.Reset();frame.claimed=false;frame.role_owner=nullptr;frame.complete_role=nullptr;return gpu::SlotResult::Ok;
}
gpu::SlotResult NativeInputGpuSource::QuarantineConsumer(gpu::ConsumerFrame& frame) noexcept {
 // Keep the Input lease: its final Release must remain BUSY until restart.
 if(!quarantined_.exchange(true))++errors_;
 frame.claimed=false;frame.producer_fd.Reset();frame.role_owner=nullptr;frame.complete_role=nullptr;
 return gpu::SlotResult::Closed;
}
bool NativeInputGpuSource::DrainUnsubmitted(gpu::ConsumerFrame& frame) noexcept {
 const auto wait=[](void*,gpu::SyncFd& fd) noexcept {
  if(fd.State()==gpu::SyncPayloadState::AlreadySignaled)return true;
#if defined(__ANDROID__)
  pollfd item{fd.Get(),POLLIN,0};return poll(&item,1,5000)==1&&(item.revents&POLLIN)!=0;
#else
  return false;
#endif
 };
 return gpu::RetireUnsubmittedConsumer(*this,frame,wait,nullptr)==gpu::SlotResult::Ok;
}
}
