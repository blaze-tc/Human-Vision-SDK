#include "input_color_contract.h"
#include <cmath>
#include <cstdio>
#include <stdexcept>
using namespace hvinput;
#define CHECK(x) do {if(!(x))throw std::runtime_error(#x);}while(0)
int main() {try {
 for(auto matrix:{ColorMatrix::Bt601,ColorMatrix::Bt709})for(auto range:{ColorRange::Full,ColorRange::Limited}) {
  ColorContract c{matrix,range};
  for(auto rgb:{Rgb{0,0,0},Rgb{1,1,1},Rgb{1,0,0},Rgb{0,1,0},Rgb{0,0,1},Rgb{0.5f,0.5f,0.5f}}) {
   auto yuv=EncodeReference(rgb,c);auto back=DecodeReference(yuv,c);
   CHECK(std::fabs(back.r-rgb.r)<0.0001f);CHECK(std::fabs(back.g-rgb.g)<0.0001f);CHECK(std::fabs(back.b-rgb.b)<0.0001f);
  }
 }
 CHECK(OutputColorSpace(ColorContract{})==0); // unknown transfer remains unknown
 auto red601=EncodeReference(Rgb{1,0,0},ColorContract{ColorMatrix::Bt601,ColorRange::Full});
 CHECK(std::fabs(red601.y-.299f)<.00001f);CHECK(std::fabs(red601.u-(128.f/255-.1687359f))<.00001f);CHECK(std::fabs(red601.v-(128.f/255+.5f))<.00001f);
 auto red709=EncodeReference(Rgb{1,0,0},ColorContract{ColorMatrix::Bt709,ColorRange::Limited});
 CHECK(std::fabs(red709.y-(16.f+219*.2126f)/255)<.00001f);CHECK(std::fabs(red709.v-240.f/255)<.00001f);
 const float cornerX[]={0,0,1,1},cornerY[]={0,1,1,0};int corner=0;
 for(int rotation:{0,90,180,270}) {auto p=MapOutputToSource(Transform{640,384,0,0,640,360,rotation,false},0,1);CHECK(std::fabs(p.x-cornerX[corner])<.00001f);CHECK(std::fabs(p.y-cornerY[corner]*360.f/384)<.00001f);++corner;}
 for(int rotation:{0,90,180,270})for(bool mirror:{false,true}) {
  Transform t{640,360,16,8,624,352,rotation,mirror}; auto a=MapOutputToSource(t,0.25f,0.75f); auto b=MapSourceToOutput(t,a.x,a.y);
  CHECK(std::fabs(b.x-0.25f)<0.0001f);CHECK(std::fabs(b.y-0.75f)<0.0001f);
 }
 auto p=MapOutputToSource(Transform{640,360,0,0,640,360,0,false},0,0);CHECK(p.x==0);CHECK(p.y==1);
 std::puts("YuvColorContract PASS");return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;} }
