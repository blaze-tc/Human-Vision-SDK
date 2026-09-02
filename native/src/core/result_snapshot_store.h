#pragma once

#include "humanvision/humanvision_types.h"

#include <array>
#include <mutex>
#include <vector>

namespace humanvision {

struct ResultSnapshot {
    HV_ResultMeta meta{};
    std::vector<HV_Body> bodies;
};

class ResultSnapshotStore {
public:
    void Publish(const ResultSnapshot& snapshot);
    HV_Result GetMeta(HV_ResultMeta& destination) const;
    int BodyCount() const;
    HV_Result CopyBodies(HV_Body* destination, int capacity, int* written) const;

private:
    mutable std::mutex mutex_;
    std::array<ResultSnapshot, 2> snapshots_;
    int front_index_ = 0;
    bool has_result_ = false;
};

}  // namespace humanvision
