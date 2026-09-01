#include "humanvision/humanvision_c.h"

#include <gtest/gtest.h>

TEST(SdkVersion, IsNonEmpty) {
    const char* version = HV_GetVersionString();

    ASSERT_NE(version, nullptr);
    EXPECT_NE(version[0], '\0');
}
