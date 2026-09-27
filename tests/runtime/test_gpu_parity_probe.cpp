#include "gpu/android/gpu_parity_probe.h"

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <cstdint>

namespace {
using namespace humanvision::gpu;

TEST(GpuParityProbe, NewControlSessionDoesNotInheritCompletedFixture) {
    ParityControlEpoch epoch;
    const auto first=epoch.Begin();
    EXPECT_FALSE(epoch.IsComplete());
    EXPECT_TRUE(epoch.Complete(first));
    EXPECT_TRUE(epoch.IsComplete());
    const auto second=epoch.Begin();
    EXPECT_NE(first,second);
    EXPECT_FALSE(epoch.IsComplete());
    EXPECT_TRUE(epoch.Complete(second));
    EXPECT_TRUE(epoch.IsComplete());
}

TEST(GpuParityProbe, OldControlSessionCannotCompleteOrOverwriteNewSession) {
    ParityControlEpoch epoch;
    const auto old=epoch.Begin(), current=epoch.Begin();
    EXPECT_FALSE(epoch.Complete(old));
    EXPECT_FALSE(epoch.IsComplete());
    EXPECT_TRUE(epoch.Complete(current));
    EXPECT_FALSE(epoch.Complete(old));
    EXPECT_TRUE(epoch.IsComplete());
}

constexpr std::array<uint8_t, 16> kDevice{{1, 2, 3}};
constexpr std::array<uint8_t, 32> kGoldenHash{{4, 5, 6}};

struct FakeGpu {
    std::array<float, 4 * 3 * 3> actual{};
    std::array<float, 4 * 3 * 3> golden{};
    ParityReduction pending{};
    bool ready = false;
    uint32_t records = 0;

    static bool Record(void* context, const ParityGpuView& actual,
                       const ParityGpuView& golden, const ParityContract& contract,
                       uint32_t, ParityReduction& reduction) noexcept {
        auto& self = *static_cast<FakeGpu*>(context);
        ++self.records;
        const auto* a = reinterpret_cast<const float*>(actual.resource);
        const auto* g = reinterpret_cast<const float*>(golden.resource);
        reduction = {};
        reduction.element_count = actual.width * actual.height * actual.channels;
        reduction.first_mismatch = kNoParityMismatch;
        for (uint32_t i = 0; i < reduction.element_count; ++i) {
            const float error = std::fabs(a[i] - g[i]);
            reduction.max_error = std::max(reduction.max_error, error);
            reduction.error_sum += error;
            if (error > contract.element_tolerance) {
                ++reduction.mismatch_count;
                if (reduction.first_mismatch == kNoParityMismatch)
                    reduction.first_mismatch = i;
            }
        }
        for (uint32_t i = 0; i < kParitySampleCount; ++i) {
            const uint32_t index = (i * (reduction.element_count - 1)) / (kParitySampleCount - 1);
            reduction.samples[i] = a[index];
        }
        self.pending = reduction;
        return true;
    }
    static bool Collect(void* context, uint32_t, ParityReduction& reduction) noexcept {
        auto& self = *static_cast<FakeGpu*>(context);
        if (!self.ready) return false;
        reduction = self.pending;
        return true;
    }
};

ParityGpuView View(const float* values, uint64_t source_id, uint64_t generation,
                   bool golden = false) {
    ParityGpuView v{};
    v.resource = reinterpret_cast<uintptr_t>(values);
    v.device_uuid = kDevice;
    v.width = 4; v.height = 3; v.channels = 3;
    v.row_stride_elements = 12;
    v.channel_stride_elements = 1;
    v.dtype = ParityDtype::Fp32;
    v.elempack = 1;
    v.source_id = source_id;
    v.generation = generation;
    v.identity = golden ? ParityIdentity::IndependentGolden : ParityIdentity::Production;
    if (golden) v.content_sha256 = kGoldenHash;
    return v;
}
ParityContract Contract(uint64_t source_id = 150, uint64_t generation = 7) {
    ParityContract c{};
    c.source_id = source_id;
    c.generation = generation;
    c.golden_sha256 = kGoldenHash;
    c.dtype = ParityDtype::Fp32;
    c.element_tolerance = 0.02f;
    c.max_error_limit = 0.02f;
    c.mean_error_limit = 0.002f;
    return c;
}

TEST(R4GpuParity, FixedSizeSummaryAndNonblockingCollection) {
    static_assert(sizeof(ParitySummary) < 256, "parity readout must stay bounded");
    FakeGpu gpu;
    GpuParityProbe probe({&gpu, &FakeGpu::Record, &FakeGpu::Collect});
    auto actual = View(gpu.actual.data(), 150, 7);
    auto golden = View(gpu.golden.data(), 150, 7, true);
    ParityTicket ticket{};
    ASSERT_TRUE(probe.Record(ParityStage::Normalized, actual, golden, Contract(), ticket));
    ParitySummary result{};
    EXPECT_FALSE(probe.TryCollect(ticket, result));
    gpu.ready = true;
    ASSERT_TRUE(probe.TryCollect(ticket, result));
    EXPECT_TRUE(result.passed);
    EXPECT_EQ(result.element_count, 36u);
    EXPECT_EQ(result.sample_count, kParitySampleCount);
    EXPECT_EQ(result.first_mismatch, kNoParityMismatch);
    EXPECT_EQ(result.stage, ParityStage::Normalized);
    EXPECT_EQ(result.source_id, 150u);
    EXPECT_EQ(result.generation, 7u);
}

TEST(R4GpuParity, SummaryPreservesLayoutIdentityAndLogicalChwCoordinate) {
    FakeGpu gpu; gpu.actual[17]=1;
    GpuParityProbe probe({&gpu,&FakeGpu::Record,&FakeGpu::Collect});
    auto actual=View(gpu.actual.data(),150,7);
    auto golden=View(gpu.golden.data(),150,7,true);
    ParityTicket ticket{};
    ASSERT_TRUE(probe.Record(ParityStage::Normalized,actual,golden,Contract(),ticket));
    gpu.ready=true; ParitySummary summary{};
    ASSERT_TRUE(probe.TryCollect(ticket,summary));
    EXPECT_EQ(summary.device_uuid,kDevice);
    EXPECT_EQ(summary.golden_sha256,kGoldenHash);
    EXPECT_EQ(summary.channel_stride_elements,1u);
    EXPECT_EQ(summary.first_x,1u); EXPECT_EQ(summary.first_y,1u); EXPECT_EQ(summary.first_channel,1u);
}

TEST(R4GpuParity, DistinctSuballocationsShareBufferButOverlappingRangesAreRejected) {
    FakeGpu gpu;GpuParityProbe probe({&gpu,&FakeGpu::Record,&FakeGpu::Collect});
    auto actual=View(gpu.actual.data(),150,7);
    auto golden=View(gpu.actual.data(),150,7,true);
    actual.byte_capacity=golden.byte_capacity=144;golden.byte_offset=256;
    ParityTicket ticket{};
    EXPECT_TRUE(probe.Record(ParityStage::Normalized,actual,golden,Contract(),ticket));
    golden.byte_offset=128;
    EXPECT_FALSE(probe.Record(ParityStage::Normalized,actual,golden,Contract(),ticket));
}

TEST(R4GpuParity, RejectsSameProductStaleGenerationAndPoolPressure) {
    FakeGpu gpu;
    GpuParityProbe probe({&gpu, &FakeGpu::Record, &FakeGpu::Collect});
    const auto actual = View(gpu.actual.data(), 150, 7);
    auto golden = View(gpu.golden.data(), 150, 7, true);
    ParityTicket ticket{};
    EXPECT_FALSE(probe.Record(ParityStage::Source, actual, actual, Contract(), ticket));
    golden.content_sha256[0] = 42;
    EXPECT_FALSE(probe.Record(ParityStage::Source, actual, golden, Contract(), ticket));
    golden.content_sha256 = kGoldenHash;
    golden.generation = 6;
    EXPECT_FALSE(probe.Record(ParityStage::Source, actual, golden, Contract(), ticket));
    golden.generation = 7;
    std::array<ParityTicket, kParityTicketCount> tickets{};
    for (auto& item : tickets)
        ASSERT_TRUE(probe.Record(ParityStage::Source, actual, golden, Contract(), item));
    EXPECT_FALSE(probe.Record(ParityStage::Source, actual, golden, Contract(), ticket));
    gpu.ready = true;
    ParitySummary summary{};
    ASSERT_TRUE(probe.TryCollect(tickets[0], summary));
    EXPECT_FALSE(probe.TryCollect(tickets[0], summary));
    ASSERT_TRUE(probe.Record(ParityStage::Source, actual, golden, Contract(), ticket));
    EXPECT_NE(ticket.serial, tickets[0].serial);
}

TEST(R4GpuParity, CorruptionControlsFailAtTheirIntroducedBoundary) {
    constexpr float source[36] = {
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12,
        13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24,
        25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36};
    const std::array<ParityStage, 7> stage{{ParityStage::Source, ParityStage::Producer,
        ParityStage::ImportedRgb, ParityStage::ImportedRgb,
        ParityStage::Normalized, ParityStage::Normalized, ParityStage::Packed}};
    for (size_t fault = 0; fault < stage.size(); ++fault) {
        FakeGpu gpu;
        std::copy(std::begin(source), std::end(source), gpu.actual.begin());
        std::copy(std::begin(source), std::end(source), gpu.golden.begin());
        switch (fault) {
        case 0: // vertical flip
            for (int i = 0; i < 12; ++i) std::swap(gpu.actual[i], gpu.actual[24 + i]); break;
        case 1: // channel swap
            for (int i = 0; i < 36; i += 3) std::swap(gpu.actual[i], gpu.actual[i + 2]); break;
        case 2: // clipped copy
            for (int i = 27; i < 36; ++i) gpu.actual[i] = 0; break;
        case 3: // wrong stride
            for (int i = 12; i < 36; ++i) gpu.actual[i] = source[i - 3]; break;
        case 4: // stale slot
            gpu.actual[0] = -99; break;
        case 5: // scale
            for (float& value : gpu.actual) value *= 0.5f; break;
        case 6: // FP16/packing corruption
            gpu.actual[35] = 0; break;
        }
        GpuParityProbe probe({&gpu, &FakeGpu::Record, &FakeGpu::Collect});
        ParityTicket ticket{};
        ASSERT_TRUE(probe.Record(stage[fault], View(gpu.actual.data(), 150, 7),
                                 View(gpu.golden.data(), 150, 7, true), Contract(), ticket));
        gpu.ready = true;
        ParitySummary summary{};
        ASSERT_TRUE(probe.TryCollect(ticket, summary));
        EXPECT_FALSE(summary.passed) << fault;
        EXPECT_GT(summary.mismatch_count, 0u) << fault;
        EXPECT_EQ(summary.stage, stage[fault]);
    }
}

TEST(R4GpuParity, PlanarStridesAndExplicitDtypeAreEnforced) {
    FakeGpu gpu;
    GpuParityProbe probe({&gpu, &FakeGpu::Record, &FakeGpu::Collect});
    auto actual = View(gpu.actual.data(), 150, 7);
    auto golden = View(gpu.golden.data(), 150, 7, true);
    actual.row_stride_elements = golden.row_stride_elements = 4;
    actual.channel_stride_elements = golden.channel_stride_elements = 12;
    ParityTicket ticket{};
    EXPECT_TRUE(probe.Record(ParityStage::Normalized, actual, golden, Contract(), ticket));
    auto wrong = Contract(); wrong.dtype = ParityDtype::Fp16;
    EXPECT_FALSE(probe.Record(ParityStage::Normalized, actual, golden, wrong, ticket));
    actual.row_stride_elements = 3;
    EXPECT_FALSE(probe.Record(ParityStage::Normalized, actual, golden, Contract(), ticket));
}

TEST(R4GpuParity, RejectsUnidentifiedResourcesAndMalformedReduction) {
    FakeGpu gpu;
    GpuParityProbe probe({&gpu, &FakeGpu::Record, &FakeGpu::Collect});
    auto actual = View(gpu.actual.data(), 150, 7);
    auto golden = View(gpu.golden.data(), 150, 7, true);
    actual.device_uuid = golden.device_uuid = {};
    ParityTicket ticket{};
    EXPECT_FALSE(probe.Record(ParityStage::Source, actual, golden, Contract(), ticket));
    actual.device_uuid = golden.device_uuid = kDevice;
    auto contract = Contract();
    golden.content_sha256 = contract.golden_sha256 = {};
    EXPECT_FALSE(probe.Record(ParityStage::Source, actual, golden, contract, ticket));
    golden.content_sha256 = kGoldenHash;
    ASSERT_TRUE(probe.Record(ParityStage::Normalized, actual, golden, Contract(), ticket));
    gpu.pending.error_sum = -1; gpu.pending.max_error = -1; gpu.ready = true;
    ParitySummary result{};
    ASSERT_TRUE(probe.TryCollect(ticket, result));
    EXPECT_FALSE(result.passed);
}
} // namespace
