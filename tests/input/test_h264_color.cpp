#include "input_h264_color.h"
#include <cstdio>
#include <string>
#include <vector>
std::vector<uint8_t> Syntax(bool vui,unsigned matrix){
 std::string bits;auto put=[&](unsigned v,unsigned n){for(unsigned i=n;i;--i)bits+=((v>>(i-1))&1)?'1':'0';};
 auto ue=[&](unsigned v){unsigned n=0;for(unsigned x=v+1;x>1;x>>=1)++n;bits.append(n,'0');put(v+1,n+1);};
 put(66,8);put(0,8);put(30,8);ue(0);ue(0);ue(2);ue(1);put(0,1);ue(39);ue(22);put(1,1);put(1,1);put(0,1);put(vui,1);
 if(vui){put(0,1);put(0,1);put(1,1);put(5,3);put(1,1);put(1,1);put(2,8);put(2,8);put(matrix,8);put(0,1);put(0,1);put(0,1);put(0,1);put(0,1);put(0,1);}
 put(1,1);while(bits.size()%8)put(0,1);std::vector<uint8_t> out{0,0,1,0x67};
 for(size_t i=0;i<bits.size();i+=8){unsigned v=0;for(unsigned j=0;j<8;++j)v=(v<<1)|(bits[i+j]=='1');out.push_back(uint8_t(v));}return out;
}
int main(){
 // Actual fixture aeab0013688b3992224645778a6688e2c3a0a0b101fbc70a0d86c561a2b1e4bc SPS, run082139.
 const uint8_t sps[]={0,0,0,1,0x67,0x42,0xc0,0x1e,0xda,0x02,0x80,0xbf,0xe5,0xc0,0x5b,0x81,0x01,0x00,0xa0,0x00,0x00,0x03,0x00,0x20,0x00,0x00,0x06,0x41,0xe2,0xc5,0xd4};
 hvinput::H264Color c;if(!hvinput::ParseH264Color(sps,sizeof(sps),c)||c.matrix!=2||c.range!=1||c.primaries!=2||c.transfer!=2){std::printf("actual SPS VUI failure m=%u r=%u p=%u t=%u\n",c.matrix,c.range,c.primaries,c.transfer);return 1;}
 for(size_t n=0;n<sizeof(sps);++n)if(hvinput::ParseH264Color(sps,n,c)||c.matrix!=0){std::puts("truncation admitted");return 1;}
 const uint8_t overflow[]={0,0,1,0x67,0x42,0,0,0,0,0,0,0,0};if(hvinput::ParseH264Color(overflow,sizeof(overflow),c))return 1;
 const uint8_t avcc[]={1,0x42,0xc0,0x1e,0xff,0xe1,0,0xff,0x67};if(hvinput::ParseH264Color(avcc,sizeof(avcc),c))return 1;
 // Actual 601/limited fixture run081342, AVCC framing as extracted by pinned ffprobe.
 const uint8_t limited[]={1,0x42,0xc0,0x1e,0xff,0xe1,0,0x1b,0x67,0x42,0xc0,0x1e,0xda,0x02,0x80,0xbf,0xe5,0xc0,0x5a,0x81,0x01,0x03,0x20,0,0,3,0,0x20,0,0,6,0x41,0xe2,0xc5,0xd4,1,0,4,0x68,0xce,0x0f,0xc8};
 if(!hvinput::ParseH264Color(limited,sizeof(limited),c)||c.matrix!=1||c.range!=2||c.matrix_code!=6||c.primaries!=2||c.transfer!=2)return 1;
 auto absent=Syntax(false,0);if(!hvinput::ParseH264Color(absent.data(),absent.size(),c)||c.present||c.matrix||c.range)return 1;
 auto unsupported=Syntax(true,9);if(hvinput::ParseH264Color(unsupported.data(),unsupported.size(),c)||c.matrix||c.range)return 1;
 auto unspecified=Syntax(true,2);if(!hvinput::ParseH264Color(unspecified.data(),unspecified.size(),c)||!c.present||c.matrix||c.range!=1||c.matrix_code!=2)return 1;
 std::puts("H264 actual VUI bounded parse PASS");return 0;
}
