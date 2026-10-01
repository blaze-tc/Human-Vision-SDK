#include <gtest/gtest.h>
#include "plugins/pipeline/yolo/yolo_decoder.h"
#include "json/json.hpp"
#include <filesystem>
#include <fstream>
#include <limits>
#include "picosha2/picosha2.h"

void BeginNativeAllocationProbe() noexcept;
std::size_t EndNativeAllocationProbe() noexcept;

using namespace humanvision::runtime::yolo;
namespace {
HV_TensorViewV1 View(const char* name, std::vector<float>& values, int rows, int cols) {
    HV_TensorViewV1 v{};v.struct_size=sizeof(v);v.api_version=HV_PLUGIN_API_V1;
    v.name=name;v.data=values.data();v.element_type=1;v.rank=2;
    v.dimensions[0]=rows;v.dimensions[1]=cols;v.byte_count=values.size()*sizeof(float);return v;
}
std::vector<float> Read(const std::filesystem::path& path) {
    std::ifstream f(path,std::ios::binary|std::ios::ate);if(!f)return {};
    std::vector<float> data(static_cast<size_t>(f.tellg())/sizeof(float));f.seekg(0);
    f.read(reinterpret_cast<char*>(data.data()),data.size()*sizeof(float));return data;
}
std::string Hash(const std::filesystem::path& path) {
    std::ifstream f(path,std::ios::binary);std::vector<char> bytes((std::istreambuf_iterator<char>(f)),{});
    return picosha2::hash256_hex_string(bytes);
}
}
TEST(YoloDecoder, EmptyMalformedNonfiniteAndCapacity) {
    Geometry g{};ASSERT_TRUE(BuildGeometry(1280,720,320,g));
    const int n=AnchorCount(320);std::vector<float> det(n*65,-80),kp(n*51,0);
    HV_TensorViewV1 views[]={View("out0",det,n,65),View("out1",kp,n,51)};
    Decoder decoder(320);std::vector<HV_BodyObservationV1> bodies(8);std::vector<float> scores(8);uint32_t count=0;
    ASSERT_TRUE(decoder.Decode(views,2,g,123,bodies.data(),scores.data(),8,count));EXPECT_EQ(count,0u);
    views[0].dimensions[0]--;EXPECT_FALSE(decoder.Decode(views,2,g,123,bodies.data(),scores.data(),8,count));
    views[0].dimensions[0]++;kp.back()=std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(decoder.Decode(views,2,g,123,bodies.data(),scores.data(),8,count));kp.back()=0;
    EXPECT_FALSE(decoder.Decode(views,2,g,123,bodies.data(),scores.data(),0,count));
}
TEST(YoloDecoder, PinnedSevenPersonCpuAndGpuGolden) {
    const auto root=std::filesystem::path(HV_YOLO_FIXTURE_ROOT);
    ASSERT_EQ(Hash(root/"fixture.json"),"bced8ed630778369db514fd2c335456b77a8c28e116eb0def46efc8a147bca39");
    std::ifstream f(root/"fixture.json");ASSERT_TRUE(f.good());nlohmann::json meta;f>>meta;
    const auto& geometry=meta.at("geometry");Geometry g{};
    ASSERT_TRUE(BuildGeometry(geometry.at("source_width"),geometry.at("source_height"),416,g));
    ASSERT_EQ(g.width,geometry.at("width"));ASSERT_EQ(g.height,geometry.at("height"));
    ASSERT_EQ(g.left,geometry.at("left"));ASSERT_EQ(g.top,geometry.at("top"));
    std::ifstream r(root/"comparison.json");ASSERT_TRUE(r.good());nlohmann::json reference;r>>reference;
    for(const char* mode:{"cpu","gpu-fp32"}) {
        auto det=Read(root/(std::string("ncnn-")+mode+"-out0.fp32"));
        auto kp=Read(root/(std::string("ncnn-")+mode+"-out1.fp32"));
        ASSERT_EQ(Hash(root/(std::string("ncnn-")+mode+"-out0.fp32")),std::string(mode)=="cpu"?
            "9637a49d24c52d8a60c042db422bdbb1d433b88d746fb3b15e0d2184df8b54c9":
            "4e9506c7b8b0e3e0bd528fe87ad6cacabae852ac834dcdf90ae3bcf58e983dcc");
        ASSERT_EQ(Hash(root/(std::string("ncnn-")+mode+"-out1.fp32")),std::string(mode)=="cpu"?
            "dd5156aa9e04b7d02057f1bad0fbec61911e7572b113ccf8fedf1292b18e1fc0":
            "8299846b941308696fdd8f4eca9aa1215b494f9d65ae3858626b7c8e92fb00fa");
        ASSERT_FALSE(det.empty());ASSERT_FALSE(kp.empty());
        HV_TensorViewV1 views[]={View("out0",det,AnchorCount(416),65),View("out1",kp,AnchorCount(416),51)};
        Decoder decoder(416);HV_BodyObservationV1 bodies[8]{};float scores[8]{};uint32_t count=0;
        ASSERT_TRUE(decoder.Decode(views,2,g,123456,bodies,scores,8,count));ASSERT_EQ(count,7u);
        for(uint32_t b=0;b<count;++b) {
            const auto& expected=reference.at("reference").at(b);
            EXPECT_NEAR(scores[b],expected.at("score").get<float>(),.0001f);
            EXPECT_NEAR(bodies[b].bbox_px.x,expected.at("box").at(0).get<float>(),.002f);
            EXPECT_NEAR(bodies[b].bbox_px.y,expected.at("box").at(1).get<float>(),.002f);
            EXPECT_NEAR(bodies[b].bbox_px.x+bodies[b].bbox_px.width,expected.at("box").at(2).get<float>(),.002f);
            EXPECT_NEAR(bodies[b].bbox_px.y+bodies[b].bbox_px.height,expected.at("box").at(3).get<float>(),.002f);
            for(int j=0;j<17;++j) {
                const auto& actual=bodies[b].joints[CanonicalIndex(j)];
                EXPECT_NEAR(actual.x_px,expected.at("joints").at(j).at(0).get<float>(),.002f);
                EXPECT_NEAR(actual.y_px,expected.at("joints").at(j).at(1).get<float>(),.002f);
                EXPECT_NEAR(actual.confidence,expected.at("joints").at(j).at(2).get<float>(),.0001f);
                EXPECT_EQ(actual.observation_timestamp_us,123456);
                EXPECT_NEAR(actual.x_norm,actual.x_px/g.source_width,1e-6f);
            }
            EXPECT_FALSE(bodies[b].joints[HV_CANONICAL_HAND_LEFT].valid);
            EXPECT_FALSE(bodies[b].joints[HV_CANONICAL_THUMB_RIGHT].valid);
        }
        for(uint32_t capacity=1;capacity<=8;++capacity) {
            ASSERT_TRUE(decoder.Decode(views,2,g,123456,bodies,scores,capacity,count));
            EXPECT_EQ(count,std::min(capacity,7u));
        }
        BeginNativeAllocationProbe();
        const bool decoded=decoder.Decode(views,2,g,123456,bodies,scores,8,count);
        const auto allocations=EndNativeAllocationProbe();
        EXPECT_TRUE(decoded);EXPECT_EQ(allocations,0u);
    }
}
