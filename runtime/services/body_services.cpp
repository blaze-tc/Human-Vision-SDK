#include "services/body_services.h"
#include <algorithm>
#include <cmath>
#include <limits>
namespace humanvision::runtime {
namespace {
constexpr int64_t kPredictionHorizonUs=25000;
constexpr int64_t kHandExpiryUs=200000;
constexpr double kObservationPeriodAlpha=.2;
float Iou(HV_Rect a,HV_Rect b){float w=std::max(0.F,std::min(a.x+a.width,b.x+b.width)-std::max(a.x,b.x));float h=std::max(0.F,std::min(a.y+a.height,b.y+b.height)-std::max(a.y,b.y));return w*h/std::max(1.F,a.width*a.height+b.width*b.height-w*h);}
void Midpoint(HV_CanonicalBodyV1& body,int target,int left,int right){
 auto& j=body.joints[target];const auto& a=body.joints[left];const auto& b=body.joints[right];if(j.valid||!a.valid||!b.valid)return;
 j=a;j.derived=1;j.x_px=(a.x_px+b.x_px)*.5F;j.y_px=(a.y_px+b.y_px)*.5F;j.x_norm=(a.x_norm+b.x_norm)*.5F;j.y_norm=(a.y_norm+b.y_norm)*.5F;j.confidence=std::min(a.confidence,b.confidence);j.observation_timestamp_us=std::min(a.observation_timestamp_us,b.observation_timestamp_us);
}
void Derive(HV_CanonicalBodyV1& b){
 Midpoint(b,HV_CANONICAL_PELVIS,HV_CANONICAL_HIP_LEFT,HV_CANONICAL_HIP_RIGHT);
 Midpoint(b,HV_CANONICAL_NECK,HV_CANONICAL_SHOULDER_LEFT,HV_CANONICAL_SHOULDER_RIGHT);
 Midpoint(b,HV_CANONICAL_SPINE_NAVEL,HV_CANONICAL_PELVIS,HV_CANONICAL_NECK);
 Midpoint(b,HV_CANONICAL_SPINE_CHEST,HV_CANONICAL_SPINE_NAVEL,HV_CANONICAL_NECK);
 Midpoint(b,HV_CANONICAL_CLAVICLE_LEFT,HV_CANONICAL_NECK,HV_CANONICAL_SHOULDER_LEFT);
 Midpoint(b,HV_CANONICAL_CLAVICLE_RIGHT,HV_CANONICAL_NECK,HV_CANONICAL_SHOULDER_RIGHT);
 Midpoint(b,HV_CANONICAL_HEAD,HV_CANONICAL_EAR_LEFT,HV_CANONICAL_EAR_RIGHT);
}
// Square Hungarian assignment, fixed storage for eight bodies; no frame allocation.
std::array<int,8> Assign(const float costs[8][8],int n){
 float u[9]{},v[9]{};int p[9]{},way[9]{};
 for(int i=1;i<=n;++i){p[0]=i;int j0=0;float minimum[9];std::fill_n(minimum,9,1e9F);bool used[9]{};
  do{used[j0]=true;int i0=p[j0],j1=0;float delta=1e9F;
   for(int j=1;j<=n;++j)if(!used[j]){float current=costs[i0-1][j-1]-u[i0]-v[j];if(current<minimum[j]){minimum[j]=current;way[j]=j0;}if(minimum[j]<delta){delta=minimum[j];j1=j;}}
   for(int j=0;j<=n;++j)if(used[j]){u[p[j]]+=delta;v[j]-=delta;}else minimum[j]-=delta;j0=j1;
  }while(p[j0]);
  do{int j1=way[j0];p[j0]=p[j1];j0=j1;}while(j0);
 }
 std::array<int,8> result;result.fill(-1);for(int j=1;j<=n;++j)if(p[j])result[p[j]-1]=j-1;return result;
}
}
void BodyServices::Configure(int capacity,const HV_Rect* regions,uint32_t count,int64_t revision){
 capacity_=std::clamp(capacity,1,8);region_count_=regions?std::min(count,uint32_t(capacity_)):0;regions_={};for(uint32_t i=0;i<region_count_;++i)regions_[i]=regions[i];tracks_={};revision_=revision;last_time_=0;observation_period_ewma_us_=0;hand_cursor_=0;hand_served_mask_=0;
}
int64_t BodyServices::RenderHoldUs() const{
 return observation_period_ewma_us_>0?int64_t(std::clamp(observation_period_ewma_us_*2.5,300000.,800000.)):500000;
}
int64_t BodyServices::TrackLostUs() const{return std::max(int64_t(800000),RenderHoldUs());}
int BodyServices::Region(const HV_Rect& box,int width,int height) const{
 if(!region_count_)return -1;float x=(box.x+box.width*.5F)/width,y=(box.y+box.height*.5F)/height;
 for(uint32_t i=0;i<region_count_;++i){auto r=regions_[i];if(x>=r.x&&y>=r.y&&x<=r.x+r.width&&y<=r.y+r.height)return int(i);}return -2;
}
void BodyServices::Observe(const HV_ObservationFrameV1& frame,int64_t revision){
 if(revision!=revision_||frame.source_timestamp_us<=last_time_||frame.width<=0||frame.height<=0||frame.body_count>8)return;
 if(last_time_){const double period=double(frame.source_timestamp_us-last_time_);observation_period_ewma_us_=observation_period_ewma_us_>0?observation_period_ewma_us_+kObservationPeriodAlpha*(period-observation_period_ewma_us_):period;}
 last_time_=frame.source_timestamp_us;width_=frame.width;height_=frame.height;int obs[8]{},regions[8]{},num=0,slots[8]{},active=0;
 for(int i=0;i<capacity_;++i){auto& t=tracks_[i];if(t.active&&last_time_-t.raw.observation_timestamp_us>TrackLostUs())t.active=false;if(t.active){slots[active++]=i;t.raw.lifecycle=t.filtered.lifecycle=2;}}
 for(uint32_t i=0;i<frame.body_count&&num<capacity_;++i){const auto& b=frame.bodies[i].bbox_px;if(!std::isfinite(b.x)||!std::isfinite(b.y)||!std::isfinite(b.width)||!std::isfinite(b.height)||b.width<=0||b.height<=0)continue;int region=Region(b,frame.width,frame.height);if(region==-2)continue;bool taken=false;for(int j=0;j<num;++j)taken|=region>=0&&regions[j]==region;if(taken)continue;obs[num]=int(i);regions[num++]=region;}
 float costs[8][8];for(auto& row:costs)std::fill_n(row,8,2.F);int n=std::max(active,num);
 for(int i=0;i<active;++i)for(int j=0;j<num;++j){const auto& track=tracks_[slots[i]];const auto& t=track.raw;const auto& b=frame.bodies[obs[j]];
  float age=std::clamp(float(last_time_-t.observation_timestamp_us)/1e6F,0.F,.2F);auto box=t.bbox_px;
  box.x+=track.box_velocity.x*age;box.y+=track.box_velocity.y*age;box.width=std::max(1.F,box.width+track.box_velocity.width*age);box.height=std::max(1.F,box.height+track.box_velocity.height*age);
  float dx=(box.x-b.bbox_px.x)/frame.width,dy=(box.y-b.bbox_px.y)/frame.height;float distance=std::sqrt(dx*dx+dy*dy);float keydistance=0;int keys=0;
  for(int k=0;k<32;++k)if(t.joints[k].valid&&b.joints[k].valid){float x=t.joints[k].x_norm+track.vx[k]*age/frame.width-b.joints[k].x_norm,y=t.joints[k].y_norm+track.vy[k]*age/frame.height-b.joints[k].y_norm;keydistance+=std::sqrt(x*x+y*y);++keys;}
  costs[i][j]=(distance>.4F||(regions[j]>=0&&t.region_index!=regions[j]))?10.F:.6F*(1-Iou(box,b.bbox_px))+distance+(keys?keydistance/keys:0);
 }
 auto assignment=Assign(costs,n);int target[8];std::fill_n(target,8,-1);
 for(int i=0;i<active;++i){int j=assignment[i];if(j>=0&&j<num&&costs[i][j]<1.2F)target[j]=slots[i];}
 for(int j=0;j<num;++j){int slot=target[j];if(slot<0){for(int k=0;k<capacity_;++k)if(!tracks_[k].active){slot=k;break;}if(slot<0)continue;tracks_[slot]={};hand_served_mask_&=~(3u<<(slot*2));tracks_[slot].active=true;tracks_[slot].raw.track_id=next_id_++;}
  auto& t=tracks_[slot];const auto previous=t.raw;HV_CanonicalBodyV1 body{};body.struct_size=sizeof(body);body.api_version=HV_API_VERSION_040;body.track_id=previous.track_id;body.region_index=regions[j]>=0?regions[j]:slot;body.region_revision=revision_;body.source_frame_id=frame.source_frame_id;body.observation_timestamp_us=last_time_;body.lifecycle=++t.hits>=2?1:0;body.bbox_px=frame.bodies[obs[j]].bbox_px;body.confidence=frame.bodies[obs[j]].confidence;std::copy_n(frame.bodies[obs[j]].joints,32,body.joints);Derive(body);
  const float dt=float(last_time_-previous.observation_timestamp_us)/1e6F;
  if(previous.observation_timestamp_us&&dt>0&&dt<.5F){const auto a=previous.bbox_px,b=body.bbox_px;t.box_velocity={(b.x-a.x)/dt,(b.y-a.y)/dt,(b.width-a.width)/dt,(b.height-a.height)/dt};}
  auto filtered=body;
  for(int k=0;k<32;++k){auto& current=body.joints[k];auto& f=filtered.joints[k];if(!current.valid){t.vx[k]=t.vy[k]=0;continue;}if(previous.joints[k].valid&&dt>0&&dt<.5F){float vx=(current.x_px-previous.joints[k].x_px)/dt,vy=(current.y_px-previous.joints[k].y_px)/dt;t.vx[k]=.5F*t.vx[k]+.5F*vx;t.vy[k]=.5F*t.vy[k]+.5F*vy;float cutoff=2.F+.04F*std::sqrt(t.vx[k]*t.vx[k]+t.vy[k]*t.vy[k]);float alpha=1.F/(1.F+1.F/(6.2831853F*cutoff*dt));f.x_px=t.filtered.joints[k].x_px+alpha*(current.x_px-t.filtered.joints[k].x_px);f.y_px=t.filtered.joints[k].y_px+alpha*(current.y_px-t.filtered.joints[k].y_px);f.x_norm=f.x_px/frame.width;f.y_norm=f.y_px/frame.height;}}
  // Preserve independently observed hands; their own age controls validity.
  for(int k:{8,9,10,15,16,17})if(!body.joints[k].valid&&previous.joints[k].valid&&last_time_-previous.joints[k].observation_timestamp_us<=200000){body.joints[k]=previous.joints[k];filtered.joints[k]=previous.joints[k];}
  t.raw=body;t.filtered=filtered;
 }
}
BodySnapshot BodyServices::Raw() const{BodySnapshot out;for(const auto& t:tracks_)if(t.active&&t.raw.observation_timestamp_us==last_time_)out.bodies[out.count++]=t.raw;return out;}
BodySnapshot BodyServices::Sample(int64_t now) const{
 BodySnapshot out;const int64_t hold=RenderHoldUs();
 for(const auto& t:tracks_)if(t.active&&now>=t.raw.observation_timestamp_us&&now-t.raw.observation_timestamp_us<=hold){
  auto body=t.filtered;
  for(int k=0;k<32;++k){
   auto& j=body.joints[k];if(!j.valid)continue;
   const bool hand=(k>=8&&k<=10)||(k>=15&&k<=17);
   const int64_t age=now-j.observation_timestamp_us;
   j.prediction_ms=0;
   if(age>(hand?kHandExpiryUs:hold)){j.valid=0;continue;}
   // Once the short prediction window ends, hold the last filtered observation.
   // Do not keep integrating velocity throughout a slow inference interval.
   if(age>=0&&age<=kPredictionHorizonUs){
    const float horizon=float(age)/1e6F,dx=t.vx[k]*horizon,dy=t.vy[k]*horizon;
    j.x_norm+=dx/width_;j.y_norm+=dy/height_;j.x_px+=dx;j.y_px+=dy;j.prediction_ms=horizon*1000;
   }
  }
  out.bodies[out.count++]=body;
 }
 return out;
}
BodySampleDiagnostics BodyServices::Diagnostics(int64_t now) const{
 BodySampleDiagnostics out;const int64_t hold=RenderHoldUs(),lost=TrackLostUs();
 out.observation_period_ewma_ms=observation_period_ewma_us_/1000.;out.render_hold_ms=double(hold)/1000.;out.track_lost_ms=double(lost)/1000.;
 int64_t youngest=std::numeric_limits<int64_t>::max();
 for(const auto& t:tracks_)if(t.active&&now>=t.raw.observation_timestamp_us){
  const int64_t age=now-t.raw.observation_timestamp_us;youngest=std::min(youngest,age);
  if(age<=lost)++out.tracked_body_count;if(age<=hold)++out.sampled_body_count;
 }
 if(youngest!=std::numeric_limits<int64_t>::max())out.sample_age_ms=double(youngest)/1000.;
 if(out.sampled_body_count)out.state=youngest<=kPredictionHorizonUs?BodySampleState::Predicted:BodySampleState::Held;
 return out;
}
void BodyServices::MergeHands(const HV_HandObservationV1* hands,uint32_t count,int64_t revision){
 if(!hands||revision!=revision_)return;
 for(uint32_t i=0;i<std::min(count,16u);++i)for(auto& t:tracks_){
  if(!t.active||t.raw.track_id!=hands[i].request_id||hands[i].side>=2)continue;
  const int base=hands[i].side?15:8;
  const HV_CanonicalJointV1 points[]{hands[i].palm,hands[i].fingertip,hands[i].thumb};
  for(int k=0;k<3;++k){const auto& point=points[k];
   if(std::abs(t.raw.observation_timestamp_us-point.observation_timestamp_us)>200000||
      point.observation_timestamp_us<t.raw.joints[base+k].observation_timestamp_us)continue;
   t.raw.joints[base+k]=t.filtered.joints[base+k]=point;t.vx[base+k]=t.vy[base+k]=0;
  }
 }
}
uint32_t BodyServices::HandRequests(HV_RegionOfInterestV1* out,uint32_t capacity,int64_t now,int64_t minimum_interval){
 if(!out)return 0;
 std::array<HV_RegionOfInterestV1,16> candidates{};
 std::array<float,16> speed{};uint32_t eligible=0;
 for(uint32_t item=0;item<16;++item){
  const auto& t=tracks_[item/2];int side=item%2;
  if(t.hand_request_time[side]&&now-t.hand_request_time[side]<minimum_interval)continue;
  const auto& wrist=t.raw.joints[side?14:7];const auto& elbow=t.raw.joints[side?13:6];
  if(!t.active||!wrist.valid||!elbow.valid||now<t.raw.observation_timestamp_us||now-t.raw.observation_timestamp_us>200000)continue;
  float dx=wrist.x_px-elbow.x_px,dy=wrist.y_px-elbow.y_px,length=std::sqrt(dx*dx+dy*dy);
  if(!std::isfinite(length)||length<8||wrist.x_px<0||wrist.y_px<0||wrist.x_px>=width_||wrist.y_px>=height_)continue;
  float size=std::min(float(std::max(width_,height_)),std::max(24.F,length*1.6F));
  candidates[item]={sizeof(HV_RegionOfInterestV1),HV_PLUGIN_API_V1,t.raw.track_id,{wrist.x_px+dx*.2F-size*.5F,wrist.y_px+dy*.2F-size*.5F,size,size},uint32_t(side),0};
  int joint=side?14:7;speed[item]=std::hypot(t.vx[joint],t.vy[joint]);eligible|=1u<<item;
 }
 uint32_t count=0,selected=0;
 while(count<capacity&&(eligible&~selected)){
  // Motion orders each fair round; no hand can monopolize the shared budget.
  if(!(eligible&~hand_served_mask_))hand_served_mask_=0;
  uint32_t available=eligible&~hand_served_mask_&~selected;
  if(!available)break;
  int best=-1;float highest=-1;
  for(uint32_t step=0;step<16;++step){uint32_t item=(hand_cursor_+step)%16;
   if((available&(1u<<item))&&speed[item]>highest){best=int(item);highest=speed[item];}
  }
  if(best<0)break;
  tracks_[best/2].hand_request_time[best%2]=now;
  out[count++]=candidates[best];selected|=1u<<best;hand_served_mask_|=1u<<best;hand_cursor_=(best+1)%16;
 }
 return count;
}
}
