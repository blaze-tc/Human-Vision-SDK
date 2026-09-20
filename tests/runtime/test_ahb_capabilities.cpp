#include "gpu/android/ahb_capabilities.h"
#include <gtest/gtest.h>

using namespace humanvision::gpu;
namespace {
AhbCandidate Complete(HV_AndroidGpuCopyPath path) {
    AhbCandidate c;
    c.path = path;
    c.requested = {640, 480, 1, 1, path == HV_ANDROID_GPU_COPY_BLIT ? 256u : 768u, 0};
    c.actual = c.requested;
    c.actual.stride = 672;
    c.allocated = c.described = c.source_supported = true;
    c.source_transfer_src = c.source_blit_src = c.source_sampled = true;
    c.requires_scale_or_conversion = c.blit_conversion = true;
    c.producer.properties = c.producer.external_query = c.producer.importable = true;
    c.producer.compatible_handle = c.producer.usage_compatible = c.producer.extent_supported = true;
    c.producer.sampled = c.producer.transfer_dst = c.producer.blit_dst = c.producer.color_attachment = true;
    c.producer.image_created = c.producer.memory_imported = c.producer.memory_bound = true;
    c.producer.view_created = c.producer.framebuffer_created = true;
    c.producer.vk_format = 37;
    c.producer.format_features = 0xdead;
    c.producer.image_usage = path == HV_ANDROID_GPU_COPY_BLIT ? 6 : 20;
    c.consumer = c.producer;
    c.consumer.image_usage = 4;
    return c;
}
}

TEST(AhbCapabilities, SelectsBlitThenColorOnlyWithCompleteMeasuredContracts) {
    auto blit = Complete(HV_ANDROID_GPU_COPY_BLIT);
    auto color = Complete(HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT);
    EXPECT_EQ(SelectAhbCopyPath({blit, color}).path, HV_ANDROID_GPU_COPY_BLIT);
    blit.source_transfer_src = false;
    EXPECT_EQ(SelectAhbCopyPath({blit, color}).path, HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT);
    color.source_sampled = false;
    EXPECT_EQ(SelectAhbCopyPath({blit, color}).path, HV_ANDROID_GPU_COPY_UNAVAILABLE);
}

TEST(AhbCapabilities, EveryBlitRequirementIsNecessary) {
    const std::vector<bool AhbCandidate::*> gates = {
        &AhbCandidate::allocated, &AhbCandidate::described, &AhbCandidate::source_supported,
        &AhbCandidate::source_transfer_src, &AhbCandidate::source_blit_src,
        &AhbCandidate::blit_conversion};
    for (const auto gate : gates) {
        auto c = Complete(HV_ANDROID_GPU_COPY_BLIT);
        c.*gate = false;
        EXPECT_EQ(SelectAhbCopyPath({c}).path, HV_ANDROID_GPU_COPY_UNAVAILABLE);
    }
    auto c = Complete(HV_ANDROID_GPU_COPY_BLIT);
    c.blit_conversion = c.requires_scale_or_conversion = false;
    EXPECT_EQ(SelectAhbCopyPath({c}).path, HV_ANDROID_GPU_COPY_BLIT);
    c.producer.transfer_dst = false;
    EXPECT_EQ(SelectAhbCopyPath({c}).path, HV_ANDROID_GPU_COPY_UNAVAILABLE);
    c = Complete(HV_ANDROID_GPU_COPY_BLIT);
    c.producer.blit_dst = false;
    EXPECT_EQ(SelectAhbCopyPath({c}).path, HV_ANDROID_GPU_COPY_UNAVAILABLE);
}

TEST(AhbCapabilities, EveryExternalImageRequirementAppliesToBothDevices) {
    const std::vector<bool AhbImageFacts::*> gates = {
        &AhbImageFacts::properties, &AhbImageFacts::external_query, &AhbImageFacts::importable,
        &AhbImageFacts::compatible_handle, &AhbImageFacts::usage_compatible,
        &AhbImageFacts::extent_supported, &AhbImageFacts::sampled,
        &AhbImageFacts::image_created, &AhbImageFacts::memory_imported, &AhbImageFacts::memory_bound};
    for (const auto path : {HV_ANDROID_GPU_COPY_BLIT, HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT}) {
        for (const auto gate : gates) {
            for (bool consumer : {false, true}) {
                auto c = Complete(path);
                (consumer ? c.consumer : c.producer).*gate = false;
                EXPECT_EQ(SelectAhbCopyPath({c}).path, HV_ANDROID_GPU_COPY_UNAVAILABLE);
            }
        }
    }
}

TEST(AhbCapabilities, ColorNeedsSamplingAttachmentViewAndFramebuffer) {
    for (const auto gate : {&AhbImageFacts::color_attachment, &AhbImageFacts::view_created,
                            &AhbImageFacts::framebuffer_created}) {
        auto c = Complete(HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT);
        c.producer.*gate = false;
        EXPECT_EQ(SelectAhbCopyPath({c}).path, HV_ANDROID_GPU_COPY_UNAVAILABLE);
    }
    auto c = Complete(HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT);
    c.source_sampled = false;
    EXPECT_EQ(SelectAhbCopyPath({c}).path, HV_ANDROID_GPU_COPY_UNAVAILABLE);
}

TEST(AhbCapabilities, RejectsActualDescriptionMismatchAndRetainsMeasuredStride) {
    for (int field = 0; field < 6; ++field) {
        auto c = Complete(HV_ANDROID_GPU_COPY_BLIT);
        if (field == 0) c.actual.width++;
        if (field == 1) c.actual.height++;
        if (field == 2) c.actual.layers++;
        if (field == 3) c.actual.format++;
        if (field == 4) c.actual.usage ^= 512;
        if (field == 5) c.actual.stride = 639;
        EXPECT_EQ(SelectAhbCopyPath({c}).path, HV_ANDROID_GPU_COPY_UNAVAILABLE);
    }
    EXPECT_EQ(SelectAhbCopyPath({Complete(HV_ANDROID_GPU_COPY_BLIT)}).contract.stride, 672u);
}

TEST(AhbCapabilities, RejectsWrongCandidateUsageAndWritableConsumerImage) {
    for (const auto path : {HV_ANDROID_GPU_COPY_BLIT, HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT}) {
        auto c = Complete(path);
        c.consumer.image_usage = 6;
        EXPECT_EQ(SelectAhbCopyPath({c}).path, HV_ANDROID_GPU_COPY_UNAVAILABLE);
        c = Complete(path);
        c.requested.usage = c.actual.usage = 768;
        if (path == HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT) c.requested.usage = c.actual.usage = 256;
        EXPECT_EQ(SelectAhbCopyPath({c}).path, HV_ANDROID_GPU_COPY_UNAVAILABLE);
        c = Complete(path);
        c.producer.image_usage = 4;
        EXPECT_EQ(SelectAhbCopyPath({c}).path, HV_ANDROID_GPU_COPY_UNAVAILABLE);
    }
}

TEST(AhbCapabilities, ReportsAllFailuresAndMeasuredFacts) {
    auto c = Complete(HV_ANDROID_GPU_COPY_BLIT);
    c.source_transfer_src = c.source_blit_src = c.producer.blit_dst = false;
    c.consumer.memory_bound = false;
    c.detail = "deviceUUID=123 driverUUID=456 vkBindImageMemory=-2";
    const auto result = SelectAhbCopyPath({c});
    for (const char* text : {"source.transfer_src", "source.blit_src", "producer.blit_dst",
         "consumer.memory_bound", "stride=672", "format=1", "usage=256", "features=57005",
         "deviceUUID=123", "vkBindImageMemory=-2"})
        EXPECT_NE(result.diagnostic.find(text), std::string::npos) << text;
}

TEST(AhbCapabilities, ProbeSequenceUsesSeparateContractsAndRetainsBothRejections) {
    std::vector<uint64_t> requests;
    auto probe = [&](const AhbDescription& desc, HV_AndroidGpuCopyPath path) {
        requests.push_back(desc.usage);
        auto c = Complete(path);
        c.requested = desc;
        c.source_transfer_src = false;
        c.detail = path == HV_ANDROID_GPU_COPY_BLIT ? "blit measured" : "color measured";
        return c;
    };
    const auto result = ProbeAhbContracts(640, 480, probe);
    EXPECT_EQ(requests, (std::vector<uint64_t>{256, 768}));
    EXPECT_EQ(result.path, HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT);
    EXPECT_NE(result.diagnostic.find("blit measured"), std::string::npos);
    EXPECT_NE(result.diagnostic.find("color measured"), std::string::npos);
    requests.clear();
    EXPECT_EQ(ProbeAhbContracts(640, 480, [&](const AhbDescription&, HV_AndroidGpuCopyPath path) {
        requests.push_back(256);
        return Complete(path);
    }).path, HV_ANDROID_GPU_COPY_BLIT);
    EXPECT_EQ(requests.size(), 1u);
}
