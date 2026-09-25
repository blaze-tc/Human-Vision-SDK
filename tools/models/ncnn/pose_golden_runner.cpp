#include "net.h"
#include "gpu.h"
#include "command.h"
#include <cstdio>
#include <cstdint>
#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 6 && argc != 7) { std::fprintf(stderr, "usage: pose_golden_runner param bin input.fp32 x.fp32 y.fp32 [diagnostic-cpu]\n"); return 2; }
    const bool strict_vkmat = argc == 7 && std::string(argv[6]) == "strict-vkmat";
    const bool diagnostic_cpu_blob = argc == 7 && std::string(argv[6]).find("diagnostic-cpu-blob=") == 0;
    const bool diagnostic_cpu_pack4 = argc == 7 && std::string(argv[6]) == "diagnostic-cpu-pack4";
    const bool diagnostic_cpu_dump = argc == 7 && std::string(argv[6]) == "diagnostic-cpu-pack4-layer-dump";
    const bool diagnostic_gpu_dump = argc == 7 && std::string(argv[6]) == "diagnostic-vulkan-fp32-arith-layer-dump";
    const bool diagnostic_cpu = (argc == 7 && std::string(argv[6]) == "diagnostic-cpu") || diagnostic_cpu_blob || diagnostic_cpu_pack4 || diagnostic_cpu_dump;
    const bool diagnostic_pack1_blob = argc == 7 && std::string(argv[6]).find("diagnostic-vulkan-pack1-blob=") == 0;
    const bool diagnostic_pack1 = (argc == 7 && std::string(argv[6]) == "diagnostic-vulkan-pack1") || diagnostic_pack1_blob;
    const bool diagnostic_gpu_pack4_blob = argc == 7 && std::string(argv[6]).find("diagnostic-vulkan-gpu-pack4-blob=") == 0;
    const bool diagnostic_gpu_pack4 = strict_vkmat || (argc == 7 && std::string(argv[6]) == "diagnostic-vulkan-gpu-pack4") || diagnostic_gpu_pack4_blob;
    const bool diagnostic_fp32 = argc == 7 && std::string(argv[6]) == "diagnostic-vulkan-fp32";
    const bool diagnostic_fp32_arith = strict_vkmat || (argc == 7 && (std::string(argv[6]) == "diagnostic-vulkan-fp32-arith" || diagnostic_gpu_dump));
    const bool diagnostic_layers = argc == 7 && std::string(argv[6]) == "diagnostic-layers";
    const std::string diagnostic_blob = diagnostic_cpu_blob ? std::string(argv[6]).substr(20) :
        (diagnostic_pack1_blob ? std::string(argv[6]).substr(29) :
        (diagnostic_gpu_pack4_blob ? std::string(argv[6]).substr(33) :
        (argc == 7 && std::string(argv[6]).find("diagnostic-blob=") == 0 ? std::string(argv[6]).substr(16) : "")));
    if (argc == 7 && !diagnostic_cpu && !diagnostic_pack1 && !diagnostic_gpu_pack4 && !diagnostic_fp32 && !diagnostic_fp32_arith && !diagnostic_layers && diagnostic_blob.empty()) return 2;
    if (!diagnostic_cpu) ncnn::create_gpu_instance();
    if (!diagnostic_cpu && ncnn::get_gpu_count() < 1) { std::fprintf(stderr, "no Vulkan device\n"); return 3; }
    ncnn::Net net;
    net.opt.use_vulkan_compute = !diagnostic_cpu;
    net.opt.use_fp16_storage = !diagnostic_fp32;
    net.opt.use_fp16_arithmetic = !diagnostic_fp32 && !diagnostic_fp32_arith;
    net.opt.use_fp16_packed = !diagnostic_fp32;
    net.opt.use_subgroup_ops = false;
    std::fprintf(stderr, "requested use_subgroup_ops=%d before load_param/model\n", net.opt.use_subgroup_ops);
    if (net.load_param(argv[1]) || net.load_model(argv[2])) return 4;
    if (strict_vkmat || (!diagnostic_cpu && !diagnostic_pack1 && !diagnostic_gpu_pack4)) {
        int unsupported = 0;
        for (const auto* layer : net.layers()) {
            if (!layer->support_vulkan) {
                std::fprintf(stderr, "CPU fallback layer %s %s\n", layer->type.c_str(), layer->name.c_str());
                ++unsupported;
            }
        }
        std::fprintf(stderr, "Vulkan layer audit count=%zu unsupported=%d\n", net.layers().size(), unsupported);
        if (unsupported || (strict_vkmat && net.layers().size() != 166)) return 21;
    }
    std::vector<float> data(3 * 256 * 192);
    FILE* file = std::fopen(argv[3], "rb");
    if (!file) return 5;
    const size_t count = std::fread(data.data(), sizeof(float), data.size(), file);
    std::fclose(file);
    if (count != data.size()) return 6;
    ncnn::Mat tensor(192, 256, 3);
    for (int c = 0; c < 3; ++c)
        for (int y = 0; y < 256; ++y)
            for (int x = 0; x < 192; ++x)
                tensor.channel(c).row(y)[x] = data[(c * 256 + y) * 192 + x];
    ncnn::Mat cpu_padded;
    if (diagnostic_cpu_pack4 || diagnostic_cpu_dump) {
        cpu_padded.create(192, 256, 4);
        for (int c = 0; c < 4; ++c)
            for (int y = 0; y < 256; ++y)
                for (int x = 0; x < 192; ++x)
                    cpu_padded.channel(c).row(y)[x] = c < 3 ? data[(c * 256 + y) * 192 + x] : 0.0f;
    }
    ncnn::Mat packed;
    if (!diagnostic_cpu) {
        packed.create(192, 256, 1, static_cast<size_t>(8), 4);
        if (packed.empty()) return 7;
        for (int y = 0; y < 256; ++y) {
            auto* row = reinterpret_cast<uint16_t*>(packed.channel(0).row(y));
            for (int x = 0; x < 192; ++x) {
                for (int c = 0; c < 3; ++c)
                    row[x * 4 + c] = ncnn::float32_to_float16(data[(c * 256 + y) * 192 + x]);
                row[x * 4 + 3] = 0;
            }
        }
    }
    ncnn::VkMat gpu_packed;
    ncnn::VkAllocator* gpu_blob_allocator = nullptr;
    ncnn::VkAllocator* gpu_staging_allocator = nullptr;
    if (diagnostic_gpu_pack4) {
        ncnn::VulkanDevice* device = ncnn::get_gpu_device();
        gpu_blob_allocator = device->acquire_blob_allocator();
        gpu_staging_allocator = device->acquire_staging_allocator();
        ncnn::Option opt = net.opt;
        opt.blob_vkallocator = gpu_blob_allocator;
        opt.staging_vkallocator = gpu_staging_allocator;
        ncnn::Mat rgba(192, 256, 4);
        for (int c = 0; c < 4; ++c)
            for (int y = 0; y < 256; ++y)
                for (int x = 0; x < 192; ++x)
                    rgba.channel(c).row(y)[x] = c < 3 ? data[(c * 256 + y) * 192 + x] : 0.0f;
        ncnn::VkMat uploaded;
        ncnn::VkCompute upload(device);
        upload.record_upload(rgba, uploaded, opt);
        device->convert_packing(uploaded, gpu_packed, 4, 2, upload, opt);
        const int upload_result = upload.submit_and_wait();
        std::fprintf(stderr, "gpu-upload rc=%d dims=%d w=%d h=%d c=%d pack=%d bits=%d\n",
                     upload_result, gpu_packed.dims, gpu_packed.w, gpu_packed.h, gpu_packed.c,
                     gpu_packed.elempack, gpu_packed.elembits());
        if (upload_result != 0 || gpu_packed.empty() || gpu_packed.dims != 3 ||
            gpu_packed.w != 192 || gpu_packed.h != 256 || gpu_packed.c != 1 ||
            gpu_packed.elempack != 4 || gpu_packed.elembits() != 16) return 22;
        if (!strict_vkmat) {
        ncnn::VkCompute echo_command(device);
        ncnn::VkMat unpacked;
        device->convert_packing(gpu_packed, unpacked, 1, 1, echo_command, opt);
        ncnn::Option download_option = opt;
        download_option.use_packing_layout = false;
        ncnn::Mat echo;
        echo_command.record_download(unpacked, echo, download_option);
        if (echo_command.submit_and_wait() != 0 || echo.empty()) return 23;
        std::fprintf(stderr, "gpu-echo dims=%d w=%d h=%d c=%d pack=%d bits=%d\n",
                     echo.dims, echo.w, echo.h, echo.c, echo.elempack, echo.elembits());
        if (echo.dims != 3 || echo.w != 192 || echo.h != 256 || echo.c != 4 || echo.elempack != 1 || echo.elembits() != 32) return 24;
        for (int c = 0; c < 4; ++c) {
            std::fprintf(stderr, "gpu-echo-channel=%d first16=", c);
            for (int i = 0; i < 16; ++i)
                std::fprintf(stderr, "%s%.7g", i ? "," : "", echo.channel(c).row(0)[i]);
            std::fprintf(stderr, "\n");
            std::fprintf(stderr, "gpu-echo-channel=%d center16=", c);
            for (int i = 0; i < 16; ++i)
                std::fprintf(stderr, "%s%.7g", i ? "," : "", echo.channel(c).row(128)[80 + i]);
            std::fprintf(stderr, "\n");
        }
        }
    }
    std::fprintf(stderr, "requested vulkan=%d fp16-packed=%d fp16-storage=%d fp16-arithmetic=%d input-pack=%d input-bits=%zu\n",
                 net.opt.use_vulkan_compute, net.opt.use_fp16_packed, net.opt.use_fp16_storage,
                 net.opt.use_fp16_arithmetic, diagnostic_gpu_pack4 ? gpu_packed.elempack :
                 (diagnostic_cpu || diagnostic_pack1 ? tensor.elempack : packed.elempack),
                 diagnostic_gpu_pack4 ? gpu_packed.elembits() :
                 (diagnostic_cpu || diagnostic_pack1 ? tensor.elemsize * 8 : packed.elemsize * 8 / packed.elempack));
    ncnn::Extractor extractor = net.create_extractor();
    if (diagnostic_gpu_pack4) {
        extractor.set_blob_vkallocator(gpu_blob_allocator);
        extractor.set_workspace_vkallocator(gpu_blob_allocator);
        extractor.set_staging_vkallocator(gpu_staging_allocator);
        if (extractor.input("in0", gpu_packed)) return 7;
    } else if (extractor.input("in0", (diagnostic_cpu_pack4 || diagnostic_cpu_dump) ? cpu_padded : (diagnostic_cpu || diagnostic_pack1 ? tensor : packed))) return 7;
    if (!diagnostic_blob.empty()) {
        ncnn::Mat value;
        if (extractor.extract(diagnostic_blob.c_str(), value)) return 13;
        if (value.elemsize / value.elempack != 4 || value.elempack != 1) return 14;
        file = std::fopen(argv[4], "wb");
        if (!file) return 15;
        for (int c = 0; c < value.c; ++c)
            for (int y = 0; y < value.h; ++y)
                std::fwrite(value.channel(c).row(y), sizeof(float), value.w, file);
        std::fclose(file);
        std::fprintf(stderr, "BLOB %s w=%d h=%d c=%d\n", diagnostic_blob.c_str(), value.w, value.h, value.c);
        return 0;
    }
    if (diagnostic_layers || diagnostic_cpu_dump || diagnostic_gpu_dump) {
        const bool dump = diagnostic_cpu_dump || diagnostic_gpu_dump;
        std::ifstream graph(argv[1]);
        std::string line;
        std::getline(graph, line);
        std::getline(graph, line);
        int index = 0;
        while (std::getline(graph, line)) {
            std::istringstream stream(line);
            std::string kind, name, blob;
            int input_count = 0, output_count = 0;
            stream >> kind >> name >> input_count >> output_count;
            for (int i = 0; i < input_count; ++i) stream >> blob;
            for (int i = 0; i < output_count; ++i) {
                stream >> blob;
                ncnn::Mat value;
                const int rc = extractor.extract(blob.c_str(), value);
                if (rc) { std::fprintf(stderr, "FIRST_ERROR layer=%d %s %s blob=%s rc=%d\n", index, kind.c_str(), name.c_str(), blob.c_str(), rc); return 11; }
                const size_t count = value.total() * value.elempack;
                const size_t bytes = value.elemsize / value.elempack;
                if (dump) {
                    if (bytes != 4 || value.elempack != 1) {
                        std::fprintf(stderr, "DUMP_FORMAT layer=%d blob=%s bytes=%zu pack=%d\n", index, blob.c_str(), bytes, value.elempack);
                        return 25;
                    }
                    const std::string path = std::string(argv[4]) + "/" + std::to_string(index) + "_" + std::to_string(i) + ".fp32";
                    FILE* out = std::fopen(path.c_str(), "wb");
                    if (!out) return 26;
                    for (int c = 0; c < value.c; ++c)
                        for (int y = 0; y < value.h; ++y)
                            std::fwrite(value.channel(c).row(y), sizeof(float), value.w, out);
                    std::fclose(out);
                }
                size_t nonfinite = 0;
                if (bytes == 4) {
                    const auto* raw = static_cast<const uint32_t*>(value.data);
                    for (size_t j = 0; j < count; ++j) if ((raw[j] & 0x7f800000u) == 0x7f800000u) { if (nonfinite < 4) std::fprintf(stderr, "NONFINITE index=%zu bits=%08x\n", j, raw[j]); ++nonfinite; }
                } else if (bytes == 2) {
                    const auto* raw = static_cast<const uint16_t*>(value.data);
                    for (size_t j = 0; j < count; ++j) if ((raw[j] & 0x7c00u) == 0x7c00u) { if (nonfinite < 4) std::fprintf(stderr, "NONFINITE index=%zu bits=%04x\n", j, raw[j]); ++nonfinite; }
                }
                std::fprintf(stderr, "LAYER layer=%d %s blob=%s w=%d h=%d c=%d pack=%d count=%zu scalar_bytes=%zu nonfinite=%zu\n", index, kind.c_str(), blob.c_str(), value.w, value.h, value.c, value.elempack, count, bytes, nonfinite);
                if (nonfinite) return 12;
            }
            ++index;
        }
        return 0;
    }
    const auto run_start = std::chrono::steady_clock::now();
    for (int i = 0; i < 2; ++i) {
        ncnn::Mat output;
        const char* blob = i == 0 ? "simcc_x" : "simcc_y";
        if (strict_vkmat) {
            auto* device = ncnn::get_gpu_device();
            ncnn::Option opt = net.opt;
            opt.blob_vkallocator = gpu_blob_allocator;
            opt.workspace_vkallocator = gpu_blob_allocator;
            opt.staging_vkallocator = gpu_staging_allocator;
            ncnn::VkMat gpu_output, fp32_output;
            ncnn::VkCompute download(device);
            if (extractor.extract(blob, gpu_output, download)) return 8;
            device->convert_packing(gpu_output, fp32_output, 1, 1, download, opt);
            ncnn::Option download_option = opt;
            download_option.use_packing_layout = false;
            download.record_download(fp32_output, output, download_option);
            if (download.submit_and_wait() != 0 || fp32_output.empty() ||
                fp32_output.elempack != 1 || fp32_output.elembits() != 32) return 8;
        } else if (extractor.extract(blob, output)) return 8;
        const size_t expected = i == 0 ? 26 * 384 : 26 * 512;
        if (output.total() != expected || output.elempack != 1 || output.elemsize != 4) {
            std::fprintf(stderr, "%s shape/pack mismatch dims=%d w=%d h=%d c=%d pack=%d bytes=%zu\n",
                         blob, output.dims, output.w, output.h, output.c, output.elempack, output.elemsize);
            return 9;
        }
        if (strict_vkmat && (output.w != (i == 0 ? 384 : 512) || output.h != 26 || output.c != 1)) return 9;
        file = std::fopen(argv[4 + i], "wb");
        if (!file) return 10;
        for (int c = 0; c < output.c; ++c)
            for (int y = 0; y < output.h; ++y)
                std::fwrite(output.channel(c).row(y), sizeof(float), output.w, file);
        std::fclose(file);
    }
    const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - run_start).count();
    std::fprintf(stderr, "mode=%s output-ms=%.3f\n", diagnostic_cpu ? "diagnostic-cpu" : "Vulkan", elapsed);
    if (strict_vkmat) {
        std::fprintf(stderr, "HV_POSE_AUDIT {\"input_route\":\"vkmat\",\"input_dims\":3,\"input_w\":192,\"input_h\":256,\"input_c\":1,\"input_pack\":4,\"input_bits\":16,\"vulkan_layers\":166,\"unsupported_layers\":0,\"fp16_packed\":true,\"fp16_storage\":true,\"fp16_arithmetic\":false,\"subgroup\":false,\"output_route\":\"vkmat-to-fp32-pack1\",\"x_shape\":[1,26,384],\"y_shape\":[1,26,512],\"x_bytes\":39936,\"y_bytes\":53248}\n");
    }
    return 0;
}
