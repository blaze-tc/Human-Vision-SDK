#include "services/region_mask.h"
#include <cstring>
#include <cmath>
namespace humanvision::runtime {
bool MaskRegions(const HV_VideoFrame& source,const HV_Rect* regions,uint32_t count,FrameBuffer& out,std::string& error){
 if(count>8||(count&&!regions)){error="Invalid region count";return false;}
 if(ValidateVideoFrame(&source,error)!=HV_OK)return false;
 for(uint32_t i=0;i<count;++i){auto r=regions[i];if(!std::isfinite(r.x)||!std::isfinite(r.y)||!std::isfinite(r.width)||!std::isfinite(r.height)||r.width<0||r.height<0){error="Invalid region rectangle";return false;}}
 out.width=source.width;out.height=source.height;out.stride_bytes=source.stride_bytes;out.pixel_format=source.pixel_format;out.frame_id=source.frame_id;out.timestamp_us=source.timestamp_us;out.bytes.resize(source.data_bytes);std::memcpy(out.bytes.data(),source.data,source.data_bytes);
 if(!count)return true;const int bpp=BytesPerPixel(source.pixel_format);
 for(int y=0;y<source.height;++y)for(int x=0;x<source.width;++x){bool keep=false;float nx=(x+.5F)/source.width,ny=(y+.5F)/source.height;
  for(uint32_t i=0;i<count&&!keep;++i){auto r=regions[i];keep=nx>=r.x&&ny>=r.y&&nx<r.x+r.width&&ny<r.y+r.height;}
  if(!keep){auto* p=out.bytes.data()+size_t(y)*source.stride_bytes+size_t(x)*bpp;p[0]=p[1]=p[2]=0;}
 }
 return true;
}
}
