#include <gtest/gtest.h>
#include "plugins/backend/ncnn/ncnn_dispatch_budget.h"

using humanvision::runtime::ncnn_backend::DispatchPrecision;
using humanvision::runtime::ncnn_backend::PendingDispatchBudget;

TEST(NcnnDispatchBudget, QualifiedAdreno660GetsBoundedBudget) {
    EXPECT_EQ(256u * 1024u, PendingDispatchBudget("Adreno (TM) 660", DispatchPrecision{}));
}

TEST(NcnnDispatchBudget, OtherAndUnknownDevicesKeepOriginalBudget) {
    const char* devices[] = {nullptr, "", "Mali-G610", "Adreno (TM) 640", "Adreno (TM) 740", "Adreno (TM) 660 other"};
    for (const char* device : devices)
        EXPECT_EQ(32u * 1024u, PendingDispatchBudget(device, DispatchPrecision{}));
}

TEST(NcnnDispatchBudget, EveryUnqualifiedPrecisionKeepsOriginalBudget) {
    for (int field = 0; field < 5; ++field) {
        DispatchPrecision precision{};
        if (field == 0) precision.fp16_packed = true;
        if (field == 1) precision.fp16_storage = true;
        if (field == 2) precision.fp16_arithmetic = true;
        if (field == 3) precision.subgroup = true;
        if (field == 4) precision.packing_layout = false;
        EXPECT_EQ(32u * 1024u, PendingDispatchBudget("Adreno (TM) 660", precision));
    }
}
