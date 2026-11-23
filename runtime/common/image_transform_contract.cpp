#include "common/image_transform_contract.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace humanvision::runtime {
namespace {
void HashBytes(uint64_t& hash,const void* data,size_t size) noexcept {
 const auto* bytes=static_cast<const uint8_t*>(data);
 for(size_t i=0;i<size;++i){hash^=bytes[i];hash*=1099511628211ull;}
}
template<class T> void Hash(uint64_t& hash,const T& value) noexcept {HashBytes(hash,&value,sizeof(value));}
bool FiniteRect(const HV_Rect& r) noexcept {return std::isfinite(r.x)&&std::isfinite(r.y)&&std::isfinite(r.width)&&std::isfinite(r.height);}
bool IdentityValid(const HV_ImageIdentityV1& id) noexcept {return id.frame_id>=0&&id.timestamp_us>=0;}
}
uint64_t ImageContractFingerprint(std::string_view manifest,std::string_view profile) noexcept {
 uint64_t hash=14695981039346656037ull;HashBytes(hash,manifest.data(),manifest.size());
 const uint8_t separator=0;Hash(hash,separator);HashBytes(hash,profile.data(),profile.size());return hash?hash:1;
}
uint64_t ImagePlanFingerprint(const HV_ImageTransformPlanV1& p) noexcept {
 uint64_t hash=14695981039346656037ull;
 Hash(hash,p.api_version);Hash(hash,p.identity.source_id);Hash(hash,p.identity.generation);
 Hash(hash,p.identity.config_epoch);Hash(hash,p.identity.frame_id);Hash(hash,p.identity.timestamp_us);Hash(hash,p.identity.region_revision);
 Hash(hash,p.contract_fingerprint);Hash(hash,p.source_crop_px.x);Hash(hash,p.source_crop_px.y);Hash(hash,p.source_crop_px.width);Hash(hash,p.source_crop_px.height);
 Hash(hash,p.output_width);Hash(hash,p.output_height);Hash(hash,p.output_format);Hash(hash,p.rotation_degrees);Hash(hash,p.mirror_x);Hash(hash,p.interpolation);
 Hash(hash,p.normalization_owner);Hash(hash,p.total_normalization_passes);
 for(size_t i=0;i<3;++i){Hash(hash,p.mean[i]);Hash(hash,p.norm[i]);Hash(hash,p.pad_rgb[i]);}
 return hash?hash:1;
}
bool SameImageIdentity(const HV_ImageIdentityV1& a,const HV_ImageIdentityV1& b) noexcept {
 return a.source_id==b.source_id&&a.generation==b.generation&&a.config_epoch==b.config_epoch&&
  a.frame_id==b.frame_id&&a.timestamp_us==b.timestamp_us&&a.region_revision==b.region_revision;
}
uint32_t ImagePixelBytes(uint32_t format) noexcept {
 switch(format){case HV_IMAGE_RGBA8:case HV_IMAGE_BGRA8:return 4;case HV_IMAGE_RGB8:case HV_IMAGE_BGR8:return 3;case HV_IMAGE_RGB32F:return 12;default:return 0;}
}
HV_ImageDescriptorV1 DescribeCopiedImage(const HV_VideoFrame& f,const FrameEnvelope& envelope) noexcept {
 HV_ImageDescriptorV1 result{};result.struct_size=sizeof(result);result.api_version=1;
 result.identity={envelope.source_id,envelope.generation,envelope.config_epoch,f.frame_id,f.timestamp_us,envelope.region_revision};
 result.width=static_cast<uint32_t>(f.width);result.height=static_cast<uint32_t>(f.height);result.pixel_format=static_cast<uint32_t>(f.pixel_format);
 result.plane_count=1;result.planes[0]={f.data,static_cast<uint64_t>(std::max(0,f.data_bytes)),0,static_cast<uint32_t>(f.stride_bytes),ImagePixelBytes(result.pixel_format)};
 result.memory_kind=HV_IMAGE_MEMORY_CPU;result.completion=HV_IMAGE_COMPLETED;result.acquire_sync_fd=-1;
 result.row_origin=HV_IMAGE_TOP_LEFT;result.color_matrix=HV_IMAGE_COLOR_RGB;result.color_range=HV_IMAGE_RANGE_FULL;
 return result;
}
HV_ImageTransformPlanV1 IdentityImagePlan(const HV_ImageDescriptorV1& source,uint64_t contract) noexcept {
 HV_ImageTransformPlanV1 plan{};plan.struct_size=sizeof(plan);plan.api_version=1;plan.identity=source.identity;
 plan.contract_fingerprint=contract;plan.source_crop_px={0,0,float(source.width),float(source.height)};
 plan.output_width=source.width;plan.output_height=source.height;plan.output_format=source.pixel_format;
 plan.interpolation=HV_IMAGE_INTERPOLATE_NEAREST;plan.normalization_owner=HV_IMAGE_NORMALIZE_PIPELINE;plan.total_normalization_passes=1;
 for(auto& norm:plan.norm)norm=1;
 plan.plan_fingerprint=ImagePlanFingerprint(plan);return plan;
}
int32_t ValidateImageTransform(const HV_ImageDescriptorV1& s,const HV_ImageTransformPlanV1& p) noexcept {
 if(s.struct_size<sizeof(s)||s.api_version!=1||p.struct_size<sizeof(p)||p.api_version!=1||s.reserved||p.reserved||
    !IdentityValid(s.identity)||!SameImageIdentity(s.identity,p.identity)||!p.contract_fingerprint||
    p.plan_fingerprint!=ImagePlanFingerprint(p)||!s.width||!s.height||s.width>4096||s.height>4096||
    !p.output_width||!p.output_height||p.output_width>4096||p.output_height>4096||
    !FiniteRect(p.source_crop_px)||p.source_crop_px.width<=0||p.source_crop_px.height<=0||
    p.source_crop_px.width>float(s.width)*4||p.source_crop_px.height>float(s.height)*4||
    p.source_crop_px.x>=s.width||p.source_crop_px.y>=s.height||p.source_crop_px.x+p.source_crop_px.width<=0||p.source_crop_px.y+p.source_crop_px.height<=0||
    p.rotation_degrees>=360||p.rotation_degrees%90||p.mirror_x>1||
    (p.interpolation!=HV_IMAGE_INTERPOLATE_NEAREST&&p.interpolation!=HV_IMAGE_INTERPOLATE_BILINEAR)||
    p.normalization_owner>HV_IMAGE_NORMALIZE_SERVICE||p.total_normalization_passes>1||
    s.normalization_passes||((p.normalization_owner==HV_IMAGE_NORMALIZE_NONE)!=(p.total_normalization_passes==0)))return HV_ERR_INVALID_ARGUMENT;
 for(size_t i=0;i<3;++i)if(!std::isfinite(p.mean[i])||!std::isfinite(p.norm[i])||!std::isfinite(p.pad_rgb[i])||p.pad_rgb[i]<0||p.pad_rgb[i]>255)return HV_ERR_INVALID_ARGUMENT;
 const uint32_t bpp=ImagePixelBytes(s.pixel_format),out_bpp=ImagePixelBytes(p.output_format);
 if(s.memory_kind!=HV_IMAGE_MEMORY_CPU||s.format_modifier||s.plane_count!=1||
    s.completion!=HV_IMAGE_COMPLETED||s.acquire_sync_fd!=-1||s.color_matrix!=HV_IMAGE_COLOR_RGB||s.color_range!=HV_IMAGE_RANGE_FULL||
    (s.row_origin!=HV_IMAGE_TOP_LEFT&&s.row_origin!=HV_IMAGE_BOTTOM_LEFT)||
    !bpp||s.pixel_format==HV_IMAGE_RGB32F||!out_bpp)return HV_IMAGE_UNSUPPORTED;
 const auto& plane=s.planes[0];
 const uint64_t row_bytes=uint64_t(s.width)*bpp;
 if(!plane.data||plane.pixel_stride_bytes!=bpp||plane.row_stride_bytes<row_bytes||plane.offset_bytes>plane.byte_count||
    uint64_t(plane.row_stride_bytes)*(s.height-1)+row_bytes>plane.byte_count-plane.offset_bytes)return HV_ERR_INVALID_ARGUMENT;
 if((p.normalization_owner==HV_IMAGE_NORMALIZE_SERVICE)!=(p.output_format==HV_IMAGE_RGB32F))return HV_IMAGE_UNSUPPORTED;
 return HV_IMAGE_READY;
}
void ImageInverseTransform(const HV_ImageTransformPlanV1& p,float out[6]) noexcept {
 // Build affine from three pixel-boundary coordinates. Mirror is applied after rotation.
 const auto point=[&](float x,float y,float& sx,float& sy){
  float u=x/p.output_width,v=y/p.output_height;if(p.mirror_x)u=1-u;
  switch(p.rotation_degrees){case 90:{const float before=u;u=v;v=1-before;break;}case 180:u=1-u;v=1-v;break;case 270:{const float before=u;u=1-v;v=before;break;}default:break;}
  sx=p.source_crop_px.x+u*p.source_crop_px.width;sy=p.source_crop_px.y+v*p.source_crop_px.height;
 };
 float x0,y0,x1,y1,x2,y2;point(0,0,x0,y0);point(1,0,x1,y1);point(0,1,x2,y2);
 out[0]=x1-x0;out[1]=x2-x0;out[2]=x0;out[3]=y1-y0;out[4]=y2-y0;out[5]=y0;
}
HV_Result ValidatePreparedImage(const HV_ImageTransformPlanV1& plan,const HV_PreparedImageLeaseV1& lease) noexcept {
 if(plan.struct_size<sizeof(plan)||plan.api_version!=1||plan.plan_fingerprint!=ImagePlanFingerprint(plan)||
    lease.struct_size<sizeof(lease)||lease.api_version!=1||!lease.lease_token||lease.completion!=HV_IMAGE_COMPLETED||
    lease.contract_fingerprint!=plan.contract_fingerprint||lease.plan_fingerprint!=plan.plan_fingerprint||
    lease.image.struct_size<sizeof(lease.image)||lease.image.api_version!=1||!SameImageIdentity(plan.identity,lease.image.identity)||
    lease.image.completion!=HV_IMAGE_COMPLETED||lease.image.owner_token!=lease.lease_token||lease.image.width!=plan.output_width||lease.image.height!=plan.output_height||lease.image.pixel_format!=plan.output_format||
    lease.normalization_passes!=lease.image.normalization_passes||lease.normalization_passes!=(plan.normalization_owner==HV_IMAGE_NORMALIZE_SERVICE?1u:0u)||
    lease.normalization_passes+(plan.normalization_owner==HV_IMAGE_NORMALIZE_PIPELINE?1u:0u)!=plan.total_normalization_passes)return HV_ERR_INVALID_ARGUMENT;
 if(lease.image.memory_kind==HV_IMAGE_MEMORY_CPU){
  const auto& plane=lease.image.planes[0];const uint32_t bpp=ImagePixelBytes(lease.image.pixel_format);
  if(!bpp||lease.image.plane_count!=1||!plane.data||plane.pixel_stride_bytes!=bpp||
     plane.row_stride_bytes<uint64_t(lease.image.width)*bpp||plane.offset_bytes>plane.byte_count||
     uint64_t(plane.row_stride_bytes)*(lease.image.height-1)+uint64_t(lease.image.width)*bpp>plane.byte_count-plane.offset_bytes)return HV_ERR_INVALID_ARGUMENT;
 }
 float inverse[6];ImageInverseTransform(plan,inverse);
 for(size_t i=0;i<6;++i)if(!std::isfinite(lease.inverse_transform[i])||lease.inverse_transform[i]!=inverse[i])return HV_ERR_INVALID_ARGUMENT;
 return HV_OK;
}
int32_t PreparedLeasePool::Reserve(uint64_t& token) noexcept {
 std::lock_guard<std::mutex> lock(mutex_);token=0;
 for(uint32_t i=0;i<slots_.size();++i)if(slots_[i].state==State::Free){
  if(next_==std::numeric_limits<uint64_t>::max()/4)return HV_IMAGE_BUSY;
  token=(++next_)*4+i;slots_[i]={token,State::Pending};return HV_IMAGE_READY;
 }return HV_IMAGE_BUSY;
}
bool PreparedLeasePool::Complete(uint64_t token) noexcept {
 std::lock_guard<std::mutex> lock(mutex_);const auto index=Index(token);if(!token||index>=3||slots_[index].token!=token)return false;
 auto& state=slots_[index].state;
 if(state==State::Pending){state=State::Ready;return true;}if(state==State::CancelPending){state=State::CancelReady;return true;}return false;
}
int32_t PreparedLeasePool::Cancel(uint64_t token) noexcept {
 std::lock_guard<std::mutex> lock(mutex_);const auto index=Index(token);if(!token||index>=3||slots_[index].token!=token||slots_[index].state==State::Free)return HV_ERR_INVALID_ARGUMENT;
 auto& state=slots_[index].state;
 if(state==State::Pending)state=State::CancelPending;else if(state==State::Ready)state=State::CancelReady;
 return state==State::CancelPending?HV_IMAGE_PENDING:HV_IMAGE_CANCELLED;
}
int32_t PreparedLeasePool::Status(uint64_t token) const noexcept {
 std::lock_guard<std::mutex> lock(mutex_);const auto index=Index(token);if(!token||index>=3||slots_[index].token!=token)return HV_ERR_INVALID_ARGUMENT;
 switch(slots_[index].state){case State::Ready:return HV_IMAGE_READY;case State::CancelReady:return HV_IMAGE_CANCELLED;case State::Pending:case State::CancelPending:return HV_IMAGE_PENDING;default:return HV_ERR_INVALID_ARGUMENT;}
}
int32_t PreparedLeasePool::Release(uint64_t token) noexcept {
 std::lock_guard<std::mutex> lock(mutex_);const auto index=Index(token);if(!token||index>=3||slots_[index].token!=token)return HV_ERR_INVALID_ARGUMENT;
 auto& state=slots_[index].state;if(state==State::Pending||state==State::CancelPending)return HV_IMAGE_BUSY;
 if(state==State::Free)return HV_ERR_INVALID_ARGUMENT;state=State::Free;return HV_IMAGE_READY;
}
}
