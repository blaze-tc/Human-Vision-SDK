#pragma once

#include "humanvision/humanvision_types.h"

#include <array>
#include <mutex>
#include <vector>

namespace humanvision {

struct ResultSnapshot {
    HV_ResultMeta meta{};
    std::vector<HV_Body> bodies;
    std::vector<int32_t> region_indices;
    int64_t region_revision = 0;
};

class ResultSnapshotStore {
public:
    void Publish(const ResultSnapshot& snapshot);
    HV_Result GetMeta(HV_ResultMeta& destination) const;
    int BodyCount() const;
    HV_Result CopyBodies(HV_Body* destination, int capacity, int* written) const;
    HV_Result CopyRegions(int64_t sequence, int32_t* destination, int capacity, int64_t* revision) const;

private:
    mutable std::mutex mutex_;
    std::array<ResultSnapshot, 2> snapshots_;
    int front_index_ = 0;
    bool has_result_ = false;
};

}  // namespace humanvision
