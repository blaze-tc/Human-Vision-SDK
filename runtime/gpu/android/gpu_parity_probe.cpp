#include "gpu/android/gpu_parity_probe.h"

#include <cmath>
#include <limits>
#include <algorithm>

namespace humanvision::gpu {
namespace {
bool StridesValid(const ParityGpuView& v) noexcept {
    if (v.elempack != 1 && v.elempack != 4) return false;
    // Interleaved image or planar/packed tensor. cstep and row stride are
    // measured in scalar elements, including padding; neither may overlap.
    if (v.channel_stride_elements == 1)
        return v.elempack == 1 && uint64_t(v.row_stride_elements) >= uint64_t(v.width) * v.channels;
    return uint64_t(v.row_stride_elements) >= uint64_t(v.width) * v.elempack &&
        uint64_t(v.channel_stride_elements) >= uint64_t(v.row_stride_elements) * v.height;
}
bool Valid(const ParityGpuView& actual, const ParityGpuView& golden,
           const ParityContract& contract) noexcept {
    if(actual.resource==golden.resource) {
        if(!actual.byte_capacity||!golden.byte_capacity) return false;
        const auto& first=actual.byte_offset<=golden.byte_offset?actual:golden;
        const auto& second=actual.byte_offset<=golden.byte_offset?golden:actual;
        if(second.byte_offset-first.byte_offset<first.byte_capacity) return false;
    }
    if (!actual.resource || !golden.resource ||
        actual.identity != ParityIdentity::Production ||
        golden.identity != ParityIdentity::IndependentGolden ||
        actual.device_uuid != golden.device_uuid ||
        actual.generation != contract.generation || golden.generation != contract.generation ||
        actual.source_id != contract.source_id || golden.source_id != contract.source_id ||
        golden.content_sha256 != contract.golden_sha256 ||
        actual.width == 0 || actual.height == 0 || actual.channels == 0 ||
        actual.width != golden.width || actual.height != golden.height ||
        actual.channels != golden.channels || actual.dtype != golden.dtype ||
        actual.elempack != golden.elempack ||
        !StridesValid(actual) || !StridesValid(golden) ||
        actual.dtype != contract.dtype ||
        !contract.generation || !contract.source_id ||
        std::all_of(actual.device_uuid.begin(), actual.device_uuid.end(), [](uint8_t b) { return b == 0; }) ||
        std::all_of(contract.golden_sha256.begin(), contract.golden_sha256.end(), [](uint8_t b) { return b == 0; }) ||
        (contract.width && actual.width != contract.width) ||
        (contract.height && actual.height != contract.height) ||
        (contract.channels && actual.channels != contract.channels) ||
        (contract.elempack && actual.elempack != contract.elempack) ||
        !std::isfinite(contract.element_tolerance) || contract.element_tolerance < 0 ||
        !std::isfinite(contract.max_error_limit) || contract.max_error_limit < 0 ||
        !std::isfinite(contract.mean_error_limit) || contract.mean_error_limit < 0)
        return false;
    const uint64_t count = uint64_t(actual.width) * actual.height * actual.channels;
    return count <= std::numeric_limits<uint32_t>::max();
}
} // namespace

bool GpuParityProbe::Record(ParityStage stage, const ParityGpuView& actual,
                            const ParityGpuView& golden, const ParityContract& contract,
                            ParityTicket& ticket) noexcept {
    ticket = {};
    if (!dispatch_.record || !dispatch_.collect || !Valid(actual, golden, contract))
        return false;
    std::lock_guard<std::mutex> guard(mutex_);
    for (uint32_t i = 0; i < slots_.size(); ++i) {
        auto& slot = slots_[i];
        if (slot.active) continue;
        slot = {};
        slot.active = true;
        slot.serial = next_serial_++;
        if (!slot.serial) slot.serial = next_serial_++;
        slot.stage = stage;
        slot.view = actual;
        slot.contract = contract;
        if (!dispatch_.record(dispatch_.context, actual, golden, contract, i, slot.reduction)) {
            slot.active = false;
            return false;
        }
        ticket = {slot.serial, i};
        return true;
    }
    return false;
}

bool GpuParityProbe::TryCollect(ParityTicket ticket, ParitySummary& summary) noexcept {
    if (!ticket.serial || ticket.slot >= slots_.size()) return false;
    std::lock_guard<std::mutex> guard(mutex_);
    auto& slot = slots_[ticket.slot];
    if (!slot.active || slot.serial != ticket.serial ||
        !dispatch_.collect(dispatch_.context, ticket.slot, slot.reduction))
        return false;
    const auto& r = slot.reduction;
    const auto& v = slot.view;
    const uint64_t expected = uint64_t(v.width) * v.height * v.channels;
    summary = {};
    summary.device_uuid=v.device_uuid;
    summary.golden_sha256=slot.contract.golden_sha256;
    summary.generation = v.generation;
    summary.source_id = v.source_id;
    summary.stage = slot.stage;
    summary.dtype = v.dtype;
    summary.elempack = v.elempack;
    summary.width = v.width;
    summary.height = v.height;
    summary.channels = v.channels;
    summary.row_stride_elements = v.row_stride_elements;
    summary.channel_stride_elements = v.channel_stride_elements;
    summary.element_count = r.element_count;
    summary.mismatch_count = r.mismatch_count;
    summary.first_mismatch = r.first_mismatch;
    if(r.first_mismatch<expected) {
        const uint32_t plane=v.width*v.height;
        summary.first_channel=r.first_mismatch/plane;
        summary.first_y=(r.first_mismatch%plane)/v.width;
        summary.first_x=r.first_mismatch%v.width;
    }
    summary.max_error = r.max_error;
    summary.mean_error = r.element_count ? r.error_sum / r.element_count : INFINITY;
    summary.samples = r.samples;
    summary.passed = r.element_count == expected &&
        r.mismatch_count == 0 && r.first_mismatch == kNoParityMismatch &&
        std::isfinite(summary.max_error) && std::isfinite(summary.mean_error) &&
        summary.max_error >= 0 && summary.mean_error >= 0 &&
        summary.max_error <= slot.contract.max_error_limit &&
        summary.mean_error <= slot.contract.mean_error_limit;
    slot.active = false;
    return true;
}
} // namespace humanvision::gpu
