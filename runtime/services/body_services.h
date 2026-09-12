#pragma once
#include "humanvision_plugin.h"
#include <array>
namespace humanvision::runtime {
struct BodySnapshot {uint32_t count=0;std::array<HV_CanonicalBodyV1,8> bodies{};};
// Single coordinator thread owns this object; publish copied snapshots externally.
class BodyServices {
public:
 void Configure(int capacity,const HV_Rect* regions,uint32_t count,int64_t revision);
 void Observe(const HV_ObservationFrameV1&,int64_t revision);
 void MergeHands(const HV_HandObservationV1*,uint32_t count,int64_t revision);
 BodySnapshot Raw() const;
 BodySnapshot Sample(int64_t now_us) const;
 uint32_t HandRequests(HV_RegionOfInterestV1*,uint32_t capacity,int64_t now_us,int64_t minimum_interval_us=0);
private:
 struct Track {HV_CanonicalBodyV1 raw{},filtered{};std::array<float,32> vx{},vy{};HV_Rect box_velocity{};std::array<int64_t,2> hand_request_time{};int hits=0;bool active=false;};
 std::array<Track,8> tracks_{};std::array<HV_Rect,8> regions_{};
 int width_=1,height_=1;int capacity_=8;uint32_t region_count_=0,hand_cursor_=0,hand_served_mask_=0;int64_t revision_=0,next_id_=1,last_time_=0;
 int Region(const HV_Rect&,int width,int height) const;
};
}
