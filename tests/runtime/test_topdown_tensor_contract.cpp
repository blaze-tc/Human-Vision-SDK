#include "plugins/pipeline/simcc/topdown_tensor_contract.h"
#include "plugins/backend/ncnn/ncnn_dense_output.h"
#include <gtest/gtest.h>

using humanvision::runtime::MatchesTopDownTensor;
using humanvision::runtime::ncnn_backend::DenseOutputLayout;
using humanvision::runtime::ncnn_backend::DescribeDenseOutput;

TEST(TopDownTensorContract, PinnedNcnnDetectorAndPoseOutputsKeepExactLogicalShape) {
    struct Case { const char* name; int rows; int cols; };
    for (const Case spec : {Case{"cls",2100,1}, Case{"bbox",2100,4},
                            Case{"simcc_x",26,384}, Case{"simcc_y",26,512}}) {
        DenseOutputLayout layout{};
        const auto logical_bytes=uint64_t(spec.rows)*spec.cols*sizeof(float);
        ASSERT_TRUE(DescribeDenseOutput(2,spec.cols,spec.rows,1,1,
            size_t(spec.rows)*spec.cols,logical_bytes,layout));
        float sentinel=0;
        HV_TensorViewV1 view{};
        view.name=spec.name;view.element_type=1;view.data=&sentinel;
        view.rank=layout.rank;view.byte_count=layout.logical_bytes;
        for(int i=0;i<4;++i)view.dimensions[i]=layout.dimensions[i];
        EXPECT_TRUE(MatchesTopDownTensor(view,spec.name,spec.rows,spec.cols));
        view.dimensions[0]=spec.cols;view.dimensions[1]=spec.rows;
        EXPECT_FALSE(MatchesTopDownTensor(view,spec.name,spec.rows,spec.cols));
        view.dimensions[0]=spec.rows;view.dimensions[1]=spec.cols;
        --view.byte_count;
        EXPECT_FALSE(MatchesTopDownTensor(view,spec.name,spec.rows,spec.cols));
    }
}
