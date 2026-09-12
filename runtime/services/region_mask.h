#pragma once
#include "core/frame_buffer.h"
namespace humanvision::runtime {
bool MaskRegions(const HV_VideoFrame&,const HV_Rect*,uint32_t count,FrameBuffer&,std::string& error);
}
