#include "input_h264_color.h"
#include <array>
namespace hvinput { namespace {
struct Bits {
 const uint8_t* p;size_t n,pos=0;bool ok=true;
 uint32_t Read(unsigned count){if(count>32||pos>n*8||count>n*8-pos){ok=false;return 0;}uint32_t v=0;while(count--){v=(v<<1)|((p[pos/8]>>(7-pos%8))&1);++pos;}return v;}
 uint32_t UE(){unsigned zeros=0;while(ok&&!Read(1)){if(++zeros>30){ok=false;return 0;}}return ok?((uint32_t(1)<<zeros)-1+Read(zeros)):0;}
 int32_t SE(){auto v=UE();return (v&1)?int32_t((v+1)/2):-int32_t(v/2);}
};
bool Hrd(Bits& b){auto count=b.UE();if(count>31)return false;b.Read(4);b.Read(4);for(unsigned i=0;i<=count;++i){b.UE();b.UE();b.Read(1);}b.Read(5);b.Read(5);b.Read(5);b.Read(5);return b.ok;}
bool Sps(const uint8_t* p,size_t n,H264Color& out){
 if(n<2||n>4096||(p[0]&0x9f)!=7)return false;
 std::array<uint8_t,4096> rbsp{};size_t used=0;unsigned zeros=0;
 for(size_t i=1;i<n;++i){auto c=p[i];if(zeros>=2&&c==3){if(i+1>=n||p[i+1]>3)return false;zeros=0;continue;}rbsp[used++]=c;zeros=c==0?zeros+1:0;}
 Bits b{rbsp.data(),used};auto profile=b.Read(8);b.Read(8);b.Read(8);if(b.UE()>31)return false;uint32_t chroma=1;
 switch(profile){case 100:case 110:case 122:case 244:case 44:case 83:case 86:case 118:case 128:case 138:case 139:case 134:case 135:
  chroma=b.UE();if(chroma>3)return false;if(chroma==3)b.Read(1);if(b.UE()>6||b.UE()>6)return false;b.Read(1);
  if(b.Read(1))for(unsigned i=0;i<(chroma==3?12u:8u);++i)if(b.Read(1)){int last=8,next=8;for(unsigned j=0;j<(i<6?16u:64u);++j){if(next){auto delta=b.SE();if(delta < -128 || delta >127)return false;next=(last+delta+256)%256;}if(next)last=next;}}
 }
 if(b.UE()>12)return false;auto order=b.UE();if(order==0){if(b.UE()>12)return false;}else if(order==1){b.Read(1);b.SE();b.SE();auto count=b.UE();if(count>255)return false;for(unsigned i=0;i<count;++i)b.SE();}else if(order>2)return false;
 if(b.UE()>16)return false;b.Read(1);if(b.UE()>65535||b.UE()>65535)return false;auto frame=b.Read(1);if(!frame)b.Read(1);b.Read(1);if(b.Read(1))for(unsigned i=0;i<4;++i)if(b.UE()>65535)return false;
 H264Color c{};if(b.Read(1)){
  if(b.Read(1)){auto aspect=b.Read(8);if(aspect==255){b.Read(16);b.Read(16);}}
  if(b.Read(1))b.Read(1);
  if(b.Read(1)){b.Read(3);c.range=b.Read(1)?1:2;c.present=true;if(b.Read(1)){c.primaries=b.Read(8);c.transfer=b.Read(8);c.matrix_code=b.Read(8);if(c.matrix_code!=1&&c.matrix_code!=2&&c.matrix_code!=5&&c.matrix_code!=6)return false;c.matrix=c.matrix_code==1?2:(c.matrix_code==5||c.matrix_code==6?1:0);}}
  if(b.Read(1)){if(b.UE()>5||b.UE()>5)return false;}
  if(b.Read(1)){b.Read(32);b.Read(32);b.Read(1);}
  auto nal=b.Read(1);if(nal&&!Hrd(b))return false;auto vcl=b.Read(1);if(vcl&&!Hrd(b))return false;if(nal||vcl)b.Read(1);b.Read(1);
  if(b.Read(1)){b.Read(1);for(unsigned i=0;i<6;++i)if(b.UE()>65535)return false;}
 }
 if(!b.ok||b.Read(1)!=1)return false;while(b.pos%8)if(b.Read(1)!=0)return false;if(!b.ok||b.pos!=used*8)return false;out=c;return true;
}
}
static bool Parse(const uint8_t* p,size_t n,H264Color& out) noexcept {
 out={};if(!p||n<4||n>65536)return false;
 if(p[0]==1){if(n<7)return false;size_t pos=6;unsigned count=p[5]&31;bool found=false;for(unsigned i=0;i<count;++i){if(pos+2>n)return false;size_t len=(size_t(p[pos])<<8)|p[pos+1];pos+=2;if(!len||len>n-pos)return false;H264Color c;if(!Sps(p+pos,len,c))return false;if(found&&(out.matrix!=c.matrix||out.range!=c.range||out.primaries!=c.primaries||out.transfer!=c.transfer))return false;out=c;found=true;pos+=len;}return found;}
 auto start=[&](size_t i)->size_t {if(i+3<=n&&p[i]==0&&p[i+1]==0&&p[i+2]==1)return 3;if(i+4<=n&&p[i]==0&&p[i+1]==0&&p[i+2]==0&&p[i+3]==1)return 4;return 0;};
 bool found=false;for(size_t i=0;i<n;){auto prefix=start(i);if(!prefix){++i;continue;}size_t begin=i+prefix,end=begin;while(end<n&&!start(end))++end;if(begin<end&&(p[begin]&31)==7){H264Color c;if(!Sps(p+begin,end-begin,c))return false;if(found&&(out.matrix!=c.matrix||out.range!=c.range||out.primaries!=c.primaries||out.transfer!=c.transfer))return false;out=c;found=true;}i=end;}return found;
}
bool ParseH264Color(const uint8_t* p,size_t n,H264Color& out) noexcept {H264Color candidate{};bool ok=Parse(p,n,candidate);out=ok?candidate:H264Color{};return ok;}
}
