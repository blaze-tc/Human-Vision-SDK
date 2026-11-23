#pragma once
#include "common/frame_envelope.h"
#include "humanvision_input_native.h"
#include <limits>

namespace humanvision::runtime {
// Pixel-boundary mapping, exactly inverse to input_yuv_to_rgba.comp's top-left
// sampling: crop -> clockwise rotation -> horizontal mirror -> output scale.
// These fields describe transforms already applied by Input; the delivered
// image's crop/rotation remain unchanged for pipeline preprocessing.
inline bool BindNativeImageGeometry(const HV_InputNativeFrameV1& native,FrameEnvelope& envelope) noexcept {
 if(!native.source_width||!native.source_height||native.source_width>uint32_t(std::numeric_limits<int32_t>::max())||
    native.source_height>uint32_t(std::numeric_limits<int32_t>::max())||
    !native.frame.width||!native.frame.height||native.frame.width>4096||native.frame.height>4096||
    native.rotation_degrees>=360||native.rotation_degrees%90||native.mirror_x>1||
    native.crop_left<0||native.crop_top<0||native.crop_right<=native.crop_left||native.crop_bottom<=native.crop_top||
    uint32_t(native.crop_right)>native.source_width||uint32_t(native.crop_bottom)>native.source_height)return false;
 const FrameCrop crop{native.crop_left,native.crop_top,native.crop_right-native.crop_left,native.crop_bottom-native.crop_top};
 if(!crop.FitsWithin(static_cast<int32_t>(native.source_width),static_cast<int32_t>(native.source_height)))return false;
 const std::array<double,3> u{1.0/crop.width,0,-double(crop.x)/crop.width};
 const std::array<double,3> v{0,1.0/crop.height,-double(crop.y)/crop.height};
 auto a=u,b=v;
 const auto reverse=[](std::array<double,3> value){value[0]=-value[0];value[1]=-value[1];value[2]=1-value[2];return value;};
 switch(native.rotation_degrees){case 90:a=reverse(v);b=u;break;case 180:a=reverse(u);b=reverse(v);break;case 270:a=v;b=reverse(u);break;default:break;}
 if(native.mirror_x)a=reverse(a);
 for(size_t i=0;i<3;++i){envelope.source_to_analysis[i]=static_cast<float>(a[i]*native.frame.width);envelope.source_to_analysis[i+3]=static_cast<float>(b[i]*native.frame.height);}
 envelope.source_width=static_cast<int32_t>(native.source_width);envelope.source_height=static_cast<int32_t>(native.source_height);
 envelope.source_crop=crop;envelope.applied_rotation_degrees=static_cast<int32_t>(native.rotation_degrees);
 envelope.applied_mirror_x=native.mirror_x!=0;envelope.source_geometry_valid=true;
 envelope.source_matrix_raw=native.matrix;envelope.source_range_raw=native.range;
 envelope.source_transfer_raw=native.transfer;envelope.source_primaries_raw=native.primaries;
 return true;
}
}
