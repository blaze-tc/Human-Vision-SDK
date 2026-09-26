#pragma once
#include "humanvision_plugin.h"
#include <array>

namespace humanvision::runtime {
struct RegionSet {
    std::array<HV_Rect,HV_MAX_PEOPLE> rects{};
    uint32_t count=0;
};
struct TrackAnchor {
    int64_t track_id=0;
    int32_t crop_track_id=0;
    int32_t region_index=-1;
    HV_Rect bbox_px{};
    int64_t observation_timestamp_us=0;
};
struct TrackAnchors {
    std::array<TrackAnchor,HV_MAX_PEOPLE> items{};
    uint32_t count=0;
};
struct AssignedObservation {
    HV_ObservationFrameV1 frame{};
    uint32_t body_count=0;
    std::array<int32_t,HV_MAX_PEOPLE> region_indices{};
    std::array<int32_t,HV_MAX_PEOPLE> crop_track_ids{};
    int64_t region_revision=0;
};
AssignedObservation AssignRegions(const HV_ObservationFrameV1&,const RegionSet&,
                                  int64_t revision,const TrackAnchors&,
                                  const float* detector_scores=nullptr,
                                  const int32_t* crop_track_ids=nullptr);
bool CanPublish(const AssignedObservation&,int64_t current_revision) noexcept;
}
