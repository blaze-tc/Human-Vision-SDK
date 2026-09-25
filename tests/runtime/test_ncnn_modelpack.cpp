#include "host/model_pack_manager.h"
#include <gtest/gtest.h>
#include "json/json.hpp"
#include <fstream>
#include <chrono>

TEST(NcnnModelPack, LocalEligibleDetectorAndBodyResolveWithBoundedOutputs) {
    const auto root=std::filesystem::path(HV_TEST_PROJECT_ROOT)/"out/c3-local-runtime/modelpacks";
    const auto manifest=root/"precision-t-26-ncnn-fp16/modelpack.json";
    if (!std::filesystem::is_regular_file(manifest)) GTEST_SKIP() << "Local Task2 evaluation assets not generated";
    humanvision::runtime::ModelPackManager packs(root);std::string error;
    const auto pack=packs.Resolve("precision-t-26-ncnn-fp16",error);
    ASSERT_TRUE(pack)<<error;ASSERT_EQ(pack->models.size(),2u);
    EXPECT_EQ(pack->models[0].role,"detector");EXPECT_EQ(pack->models[1].role,"body");
    const auto json=nlohmann::json::parse(pack->manifest_json);
    EXPECT_TRUE(json.at("local_evaluation_only").get<bool>());
    const auto body=json.at("models").at(1);
    EXPECT_EQ(body.at("input_contract").at("elempack"),4);
    EXPECT_EQ(body.at("output_contract").at("max_output_bytes").at("simcc_x"),39936);
    EXPECT_EQ(body.at("output_contract").at("max_output_bytes").at("simcc_y"),53248);
    EXPECT_FALSE(body.at("backend_options").at("use_subgroup_ops").get<bool>());
    EXPECT_FALSE(body.at("backend_options").at("use_fp16_arithmetic").get<bool>());
}

TEST(NcnnModelPack, ChangedHashAndEscapedAssetFailResolution) {
    const auto source=std::filesystem::path(HV_TEST_PROJECT_ROOT)/"out/c3-local-runtime/modelpacks/precision-t-26-ncnn-fp16";
    if (!std::filesystem::is_regular_file(source/"modelpack.json")) GTEST_SKIP() << "Local Task2 evaluation assets not generated";
    const auto root=std::filesystem::temp_directory_path()/("hv-ncnn-pack-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto target=root/"precision-t-26-ncnn-fp16";
    std::filesystem::create_directories(root);
    struct Cleanup {std::filesystem::path path;~Cleanup(){std::error_code ec;std::filesystem::remove_all(path,ec);}} cleanup{root};
    std::filesystem::copy(source,target,std::filesystem::copy_options::recursive);
    std::ifstream input(target/"modelpack.json");nlohmann::json original;input>>original;input.close();
    for (const auto& key:{"param_sha256","bin_sha256","param_path"}) {
        auto changed=original;changed["models"][1][key]=std::string(key)=="param_path"?"../../outside.param":std::string(64,'0');
        {std::ofstream output(target/"modelpack.json");output<<changed.dump();}
        humanvision::runtime::ModelPackManager packs(root);std::string error;
        EXPECT_FALSE(packs.Resolve("precision-t-26-ncnn-fp16",error)) << key;
        EXPECT_FALSE(error.empty());
    }
}
