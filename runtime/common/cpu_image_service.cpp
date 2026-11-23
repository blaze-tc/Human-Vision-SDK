#include "common/cpu_image_service.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace humanvision::runtime {
namespace {
void Error(HV_ErrorBufferV1* error,const char* message) noexcept {
 if(!error||error->struct_size<sizeof(*error)||error->api_version!=1||!error->data||!error->capacity)return;
 const size_t n=std::min(std::strlen(message),size_t(error->capacity-1));std::memcpy(error->data,message,n);error->data[n]=0;
}
bool ValidLease(const HV_PreparedImageLeaseV1* lease) noexcept {return lease&&lease->struct_size>=sizeof(*lease)&&lease->api_version==1;}
float Pixel(const HV_ImageDescriptorV1& source,int x,int y,int channel,const float pad[3]) noexcept {
 if(x<0||y<0||x>=int(source.width)||y>=int(source.height))return channel<3?pad[channel]:255.f;
 const auto& plane=source.planes[0];if(source.row_origin==HV_IMAGE_BOTTOM_LEFT)y=int(source.height)-1-y;
 const auto* p=static_cast<const uint8_t*>(plane.data)+plane.offset_bytes+size_t(y)*plane.row_stride_bytes+size_t(x)*plane.pixel_stride_bytes;
 if(channel==3)return plane.pixel_stride_bytes==4?float(p[3]):255.f;
 if(source.pixel_format==HV_IMAGE_BGRA8||source.pixel_format==HV_IMAGE_BGR8)channel=2-channel;
 return float(p[channel]);
}
void Transform(const HV_ImageDescriptorV1& source,const HV_ImageTransformPlanV1& plan,uint8_t* target) noexcept {
 const auto& plane=source.planes[0];const uint32_t bpp=ImagePixelBytes(plan.output_format);
 const uint32_t stride=plan.output_width*bpp;
 if(plan.output_format==source.pixel_format&&!plan.rotation_degrees&&!plan.mirror_x&&
    plan.output_width==source.width&&plan.output_height==source.height&&plan.source_crop_px.x==0&&plan.source_crop_px.y==0&&
    plan.source_crop_px.width==float(source.width)&&plan.source_crop_px.height==float(source.height)) {
  for(uint32_t y=0;y<source.height;++y){const uint32_t sy=source.row_origin==HV_IMAGE_TOP_LEFT?y:source.height-1-y;
   std::memcpy(target+size_t(y)*stride,static_cast<const uint8_t*>(plane.data)+plane.offset_bytes+size_t(sy)*plane.row_stride_bytes,stride);}
  return;
 }
 float inverse[6];ImageInverseTransform(plan,inverse);
 const bool fp32=plan.output_format==HV_IMAGE_RGB32F;
 for(uint32_t y=0;y<plan.output_height;++y)for(uint32_t x=0;x<plan.output_width;++x){
  const float fx=inverse[0]*(float(x)+.5f)+inverse[1]*(float(y)+.5f)+inverse[2]-.5f;
  const float fy=inverse[3]*(float(x)+.5f)+inverse[4]*(float(y)+.5f)+inverse[5]-.5f;
  const int x0=int(std::floor(fx)),y0=int(std::floor(fy));const float wx=fx-x0,wy=fy-y0;
  for(int channel=0;channel<(fp32?3:int(bpp));++channel){
   int source_channel=channel;if(channel<3&&(plan.output_format==HV_IMAGE_BGR8||plan.output_format==HV_IMAGE_BGRA8))source_channel=2-channel;
   float value;
   if(plan.interpolation==HV_IMAGE_INTERPOLATE_NEAREST)value=Pixel(source,int(std::floor(fx+.5f)),int(std::floor(fy+.5f)),source_channel,plan.pad_rgb);
   else {
    const float a=Pixel(source,x0,y0,source_channel,plan.pad_rgb)*(1-wx)+Pixel(source,x0+1,y0,source_channel,plan.pad_rgb)*wx;
    const float b=Pixel(source,x0,y0+1,source_channel,plan.pad_rgb)*(1-wx)+Pixel(source,x0+1,y0+1,source_channel,plan.pad_rgb)*wx;
    value=a*(1-wy)+b*wy;
   }
   if(fp32){value=(value-plan.mean[channel])*plan.norm[channel];std::memcpy(target+size_t(y)*stride+size_t(x)*12+channel*4,&value,sizeof(value));}
   else target[size_t(y)*stride+size_t(x)*bpp+channel]=static_cast<uint8_t>(std::clamp(std::floor(value+.5f),0.f,255.f));
  }
 }
}
}
HV_Result CpuImageService::Query(uint32_t version,HV_ImageServiceApiV1* out) noexcept {
 if(version!=1||!out||out->struct_size<sizeof(*out)||out->api_version!=1)return HV_ERR_INVALID_ARGUMENT;
 *out={sizeof(*out),1,this,Support,Submit,Wait,Cancel,Release};return HV_OK;
}
int32_t HV_CALL CpuImageService::Support(void* context,const HV_ImageDescriptorV1* source,const HV_ImageTransformPlanV1* plan,HV_ErrorBufferV1* error) {
 if(!context||!source||!plan){Error(error,"Image service source/plan/context is missing");return HV_ERR_INVALID_ARGUMENT;}
 const auto result=ValidateImageTransform(*source,*plan);
 if(result!=HV_IMAGE_READY){Error(error,result==HV_IMAGE_UNSUPPORTED?"CPU image service requires a completed, explicitly readable packed RGB image":"Invalid image transform identity, layout or preprocessing contract");return result;}
 const auto& self=*static_cast<CpuImageService*>(context);
 if(uint64_t(plan->output_width)*plan->output_height*ImagePixelBytes(plan->output_format)>self.maximum_bytes_){Error(error,"Image transform exceeds the configured bounded output storage");return HV_IMAGE_UNSUPPORTED;}
 Error(error,"");return HV_IMAGE_READY;
}
int32_t HV_CALL CpuImageService::Submit(void* context,const HV_ImageDescriptorV1* source,const HV_ImageTransformPlanV1* plan,HV_PreparedImageLeaseV1* out,HV_ErrorBufferV1* error) {
 if(!ValidLease(out))return HV_ERR_INVALID_ARGUMENT;
 const auto supported=Support(context,source,plan,error);if(supported!=HV_IMAGE_READY)return supported;
 auto& self=*static_cast<CpuImageService*>(context);std::lock_guard<std::mutex> lock(self.mutex_);
 uint64_t token=0;const auto admitted=self.pool_.Reserve(token);if(admitted!=HV_IMAGE_READY)return admitted;
 try {
  auto& slot=self.slots_[PreparedLeasePool::Index(token)];const uint32_t stride=plan->output_width*ImagePixelBytes(plan->output_format);
  slot.bytes.resize(size_t(stride)*plan->output_height);Transform(*source,*plan,slot.bytes.data());
  auto& lease=slot.lease;lease={};lease.struct_size=sizeof(lease);lease.api_version=1;lease.lease_token=token;
  lease.contract_fingerprint=plan->contract_fingerprint;lease.plan_fingerprint=plan->plan_fingerprint;
  lease.image=*source;lease.image.width=plan->output_width;lease.image.height=plan->output_height;lease.image.pixel_format=plan->output_format;
  lease.image.planes[0]={slot.bytes.data(),slot.bytes.size(),0,stride,ImagePixelBytes(plan->output_format)};
  lease.image.owner_token=token;lease.image.row_origin=HV_IMAGE_TOP_LEFT;
  lease.normalization_passes=plan->normalization_owner==HV_IMAGE_NORMALIZE_SERVICE?1u:0u;
  lease.image.normalization_passes=lease.normalization_passes;lease.completion=HV_IMAGE_COMPLETED;
  ImageInverseTransform(*plan,lease.inverse_transform);
  if(!self.pool_.Complete(token)){Error(error,"Image service lost its owned completion token");return HV_ERR_INTERNAL;}
  *out=lease;return HV_IMAGE_READY;
 }catch(...){self.pool_.Complete(token);self.pool_.Release(token);Error(error,"Image service output allocation failed");return HV_ERR_INTERNAL;}
}
int32_t HV_CALL CpuImageService::Wait(void* context,HV_PreparedImageLeaseV1* out,uint32_t timeout,HV_ErrorBufferV1*) {
 if(!context||!ValidLease(out)||timeout>60000)return HV_ERR_INVALID_ARGUMENT;
 auto& self=*static_cast<CpuImageService*>(context);std::lock_guard<std::mutex> lock(self.mutex_);
 const auto status=self.pool_.Status(out->lease_token);
 if(status==HV_IMAGE_READY||status==HV_IMAGE_CANCELLED){*out=self.slots_[PreparedLeasePool::Index(out->lease_token)].lease;return status;}
 return status==HV_IMAGE_PENDING&&timeout==0?HV_IMAGE_TIMEOUT:status;
}
int32_t HV_CALL CpuImageService::Cancel(void* context,HV_PreparedImageLeaseV1* out,HV_ErrorBufferV1*) {
 if(!context||!ValidLease(out))return HV_ERR_INVALID_ARGUMENT;
 auto& self=*static_cast<CpuImageService*>(context);std::lock_guard<std::mutex> lock(self.mutex_);
 const auto status=self.pool_.Cancel(out->lease_token);
 if(status==HV_IMAGE_CANCELLED){auto& lease=self.slots_[PreparedLeasePool::Index(out->lease_token)].lease;
  lease.completion=lease.image.completion=HV_IMAGE_COMPLETED_CANCELLED;*out=lease;}
 return status;
}
int32_t HV_CALL CpuImageService::Release(void* context,const HV_PreparedImageLeaseV1* out,HV_ErrorBufferV1*) {
 if(!context||!ValidLease(out))return HV_ERR_INVALID_ARGUMENT;
 auto& self=*static_cast<CpuImageService*>(context);std::lock_guard<std::mutex> lock(self.mutex_);return self.pool_.Release(out->lease_token);
}
}
