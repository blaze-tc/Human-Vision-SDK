#include "gpu/vulkan/vulkan_device_identity.h"
#include <gtest/gtest.h>

using namespace humanvision::gpu;
namespace {
DeviceIdentity Device(uint8_t device, uint8_t driver) {
    DeviceIdentity result;
    result.queried = true;
    result.device_uuid.fill(device);
    result.driver_uuid.fill(driver);
    result.vendor_id = 123;
    result.device_id = 456;
    return result;
}
}

TEST(VulkanDeviceIdentity, RequiresBothUuidsOnOnePhysicalDevice) {
    const auto unity = Device(1, 2);
    EXPECT_EQ(MatchDevice(unity, {{0, Device(3, 2)}, {1, Device(1, 4)}}).status,
              DeviceMatchStatus::NoMatch);
    const auto match = MatchDevice(unity, {{0, Device(3, 2)}, {7, unity}});
    ASSERT_EQ(match.status, DeviceMatchStatus::Matched);
    EXPECT_EQ(match.index, 7);
    EXPECT_EQ(match.identity.device_uuid, unity.device_uuid);
}

TEST(VulkanDeviceIdentity, ComparesEveryByteAndNeverSelectsADefault) {
    auto unity = Device(1, 2);
    auto changed = unity;
    changed.device_uuid.back() ^= 1;
    EXPECT_EQ(MatchDevice(unity, {{0, changed}}).index, -1);
    changed = unity;
    changed.driver_uuid.back() ^= 1;
    EXPECT_EQ(MatchDevice(unity, {{0, changed}}).index, -1);
    EXPECT_EQ(MatchDevice(unity, {}).status, DeviceMatchStatus::NoMatch);
    EXPECT_EQ(MatchDevice(unity, {{0, unity}, {4, unity}}).status,
              DeviceMatchStatus::MultipleMatches);
    EXPECT_EQ(MatchDevice(unity, {{0, unity}, {4, unity}}).index, -1);
}

TEST(VulkanDeviceIdentity, RejectsUnavailableOrZeroUuidsAndInvalidIndices) {
    for (int field = 0; field < 3; ++field) {
        auto bad = Device(1, 2);
        if (field == 0) bad.queried = false;
        if (field == 1) bad.device_uuid.fill(0);
        if (field == 2) bad.driver_uuid.fill(0);
        EXPECT_EQ(MatchDevice(bad, {{0, bad}}).status, DeviceMatchStatus::InvalidIdentity);
        EXPECT_EQ(MatchDevice(Device(1, 2), {{0, bad}}).status,
                  DeviceMatchStatus::InvalidIdentity);
    }
    EXPECT_EQ(MatchDevice(Device(1, 2), {{-1, Device(1, 2)}}).status,
              DeviceMatchStatus::InvalidIdentity);
    const auto result = MatchDevice(Device(1, 2), {{5, Device(3, 4)}});
    EXPECT_NE(result.diagnostic.find("deviceUUID="), std::string::npos);
    EXPECT_NE(result.diagnostic.find("driverUUID="), std::string::npos);
    EXPECT_NE(result.diagnostic.find("index=5"), std::string::npos);
}
