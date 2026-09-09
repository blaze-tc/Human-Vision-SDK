#include "core/result_snapshot_store.h"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace humanvision {

void ResultSnapshotStore::Publish(const ResultSnapshot& snapshot) {
    std::lock_guard<std::mutex> lock(mutex_);
    const int back_index = 1 - front_index_;
    ResultSnapshot& back = snapshots_[back_index];
    back.meta = snapshot.meta;
    back.meta.struct_size = sizeof(HV_ResultMeta);
    back.meta.body_count = static_cast<std::int32_t>(snapshot.bodies.size());
    back.bodies.assign(snapshot.bodies.begin(), snapshot.bodies.end());
    back.hands.assign(snapshot.hands.begin(), snapshot.hands.end());
    back.region_indices.assign(snapshot.region_indices.begin(), snapshot.region_indices.end());
    back.region_revision = snapshot.region_revision;
    front_index_ = back_index;
    has_result_ = true;
}

HV_Result ResultSnapshotStore::GetMeta(HV_ResultMeta& destination) const {
    if (destination.struct_size < static_cast<std::int32_t>(sizeof(HV_ResultMeta))) {
        return HV_ERR_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!has_result_) {
        return HV_NO_NEW_RESULT;
    }
    destination = snapshots_[front_index_].meta;
    return HV_OK;
}

int ResultSnapshotStore::BodyCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return has_result_ ? static_cast<int>(snapshots_[front_index_].bodies.size()) : 0;
}

HV_Result ResultSnapshotStore::CopyBodies(
    HV_Body* destination,
    const int capacity,
    int* written) const {
    if (written == nullptr || capacity < 0 || (capacity > 0 && destination == nullptr)) {
        return HV_ERR_INVALID_ARGUMENT;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (!has_result_) {
        *written = 0;
        return HV_NO_NEW_RESULT;
    }
    const auto& bodies = snapshots_[front_index_].bodies;
    *written = static_cast<int>(bodies.size());
    if (capacity < static_cast<int>(bodies.size())) {
        return HV_ERR_INVALID_ARGUMENT;
    }
    std::copy(bodies.begin(), bodies.end(), destination);
    return HV_OK;
}

HV_Result ResultSnapshotStore::CopyHands(int64_t sequence, HV_Joint* destination, int capacity) const {
    if (capacity < 0 || (capacity && !destination)) return HV_ERR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!has_result_ || snapshots_[front_index_].meta.result_sequence != sequence) return HV_NO_NEW_RESULT;
    const auto& hands = snapshots_[front_index_].hands;
    if (capacity < static_cast<int>(hands.size())) return HV_ERR_INVALID_ARGUMENT;
    std::copy(hands.begin(), hands.end(), destination);
    return HV_OK;
}

HV_Result ResultSnapshotStore::CopyRegions(int64_t sequence, int32_t* destination,
    int capacity, int64_t* revision) const {
    if (!revision || capacity < 0 || (capacity && !destination)) return HV_ERR_INVALID_ARGUMENT;
    std::lock_guard<std::mutex> lock(mutex_);
    if (!has_result_ || snapshots_[front_index_].meta.result_sequence != sequence) return HV_NO_NEW_RESULT;
    const auto& front = snapshots_[front_index_];
    if (capacity < static_cast<int>(front.region_indices.size())) return HV_ERR_INVALID_ARGUMENT;
    std::copy(front.region_indices.begin(), front.region_indices.end(), destination);
    *revision = front.region_revision;
    return HV_OK;
}
}  // namespace humanvision
