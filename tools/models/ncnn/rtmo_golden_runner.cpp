// Offline model eligibility runner. Upload fixture input; download model outputs only.
#include "net.h"
#include "gpu.h"
#include "command.h"
#include "layer.h"
#include <cstdio>
#include <string>
#include <vector>

// Declared before Net/Extractor/VkMat, so it outlives every allocation user.
// Commands are synchronously submitted and waited before their VkMat scope ends.
struct GpuAllocators {
    ncnn::VulkanDevice* device = nullptr;
    ncnn::VkAllocator* blob = nullptr;
    ncnn::VkAllocator* staging = nullptr;
    explicit GpuAllocators(bool gpu) {
        if (!gpu) return;
        device = ncnn::get_gpu_device();
        blob = device->acquire_blob_allocator();
        staging = device->acquire_staging_allocator();
    }
    ~GpuAllocators() {
        if (blob) device->reclaim_blob_allocator(blob);
        if (staging) device->reclaim_staging_allocator(staging);
    }
    GpuAllocators(const GpuAllocators&) = delete;
    GpuAllocators& operator=(const GpuAllocators&) = delete;
};

int main(int argc, char** argv) {
    if (argc != 6) { std::fprintf(stderr, "usage: rtmo_runner raw|dcc8 param bin fixture-directory cpu|gpu\n"); return 2; }
    const bool raw = std::string(argv[1]) == "raw";
    const bool gpu = std::string(argv[5]) == "gpu";
    if (!raw && std::string(argv[1]) != "dcc8") return 2;
    if (!gpu && std::string(argv[5]) != "cpu") return 2;
    if (gpu) ncnn::create_gpu_instance();
    if (gpu && ncnn::get_gpu_count() < 1) return 3;
    GpuAllocators allocators(gpu);
    if (gpu && (!allocators.blob || !allocators.staging)) return 13;
    ncnn::Net net;
    net.opt.blob_vkallocator = allocators.blob;
    net.opt.workspace_vkallocator = allocators.blob;
    net.opt.staging_vkallocator = allocators.staging;
    net.opt.use_vulkan_compute = gpu;
    net.opt.use_fp16_packed = gpu;
    net.opt.use_fp16_storage = gpu;
    net.opt.use_fp16_arithmetic = false; // Explicit storage16/compute32 eligibility baseline.
    net.opt.use_subgroup_ops = false;
    if (net.load_param(argv[2]) || net.load_model(argv[3])) return 4;
    int unsupported = 0;
    for (const auto* layer : net.layers()) {
        if (gpu && !layer->support_vulkan) {
            std::fprintf(stderr, "UNSUPPORTED_VULKAN %s %s\n", layer->type.c_str(), layer->name.c_str());
            ++unsupported;
        }
    }
    std::fprintf(stderr, "layers=%zu unsupported=%d storage16=%d arithmetic16=%d\n",
                 net.layers().size(), unsupported, net.opt.use_fp16_storage, net.opt.use_fp16_arithmetic);
    if (unsupported) return 21;
    const std::vector<std::string> inputs = raw ? std::vector<std::string>{"input"} : std::vector<std::string>{"pose", "boxes", "grids", "visibility"};
    const std::vector<std::string> outputs = raw ? std::vector<std::string>{"cls16", "bbox16", "vis16", "pose16", "cls32", "bbox32", "vis32", "pose32"} : std::vector<std::string>{"keypoints"};
    auto ex = net.create_extractor();
    if (gpu) {
        ex.set_blob_vkallocator(allocators.blob);
        ex.set_workspace_vkallocator(allocators.blob);
        ex.set_staging_vkallocator(allocators.staging);
    }
    auto* device = allocators.device;
    std::vector<ncnn::VkMat> uploaded(inputs.size());
    for (size_t i = 0; i < inputs.size(); ++i) {
        const int widths[] = {192, 5, 2, 17};
        ncnn::Mat input = raw ? ncnn::Mat(416, 416, 3) : ncnn::Mat(widths[i], 8);
        const std::string path = std::string(argv[4]) + "/" + inputs[i] + ".fp32";
        FILE* file = std::fopen(path.c_str(), "rb");
        if (!file) return 5;
        for (int c = 0; c < input.c; ++c) {
            const size_t count = static_cast<size_t>(input.w) * input.h;
            if (std::fread(input.channel(c), sizeof(float), count, file) != count) return 6;
        }
        std::fclose(file);
        if (gpu) {
            ncnn::VkMat transferred;
            ncnn::VkCompute cmd(device);
            cmd.record_upload(input, transferred, net.opt);
            if (transferred.empty()) return 7;
            device->convert_packing(transferred, uploaded[i], 1, 2, cmd, net.opt);
            if (cmd.submit_and_wait()) return 7;
            std::fprintf(stderr, "input %s dims=%d w=%d h=%d c=%d pack=%d bits=%d\n", inputs[i].c_str(), uploaded[i].dims, uploaded[i].w, uploaded[i].h, uploaded[i].c, uploaded[i].elempack, uploaded[i].elembits());
            if (uploaded[i].empty() || uploaded[i].elempack != 1 || uploaded[i].elembits() != 16) return 14;
            if (ex.input(inputs[i].c_str(), uploaded[i])) return 8;
        } else if (ex.input(inputs[i].c_str(), input)) return 8;
    }
    for (const auto& name : outputs) {
        ncnn::Mat output;
        if (gpu) {
            ncnn::VkCompute cmd(device);
            ncnn::VkMat result;
            if (ex.extract(name.c_str(), result, cmd)) return 9;
            ncnn::Option download = net.opt;
            download.use_packing_layout = false;
            cmd.record_download(result, output, download);
            if (cmd.submit_and_wait()) return 10;
        } else if (ex.extract(name.c_str(), output)) return 9;
        if (output.empty() || output.elempack != 1 || output.elembits() != 32) return 11;
        const std::string path = std::string(argv[4]) + "/ncnn-" + argv[5] + "-" + name + ".fp32";
        FILE* file = std::fopen(path.c_str(), "wb");
        if (!file) return 12;
        for (int c = 0; c < output.c; ++c)
            std::fwrite(output.channel(c), sizeof(float), output.w * output.h * output.d, file);
        std::fclose(file);
        std::fprintf(stderr, "output %s dims=%d w=%d h=%d d=%d c=%d\n", name.c_str(), output.dims, output.w, output.h, output.d, output.c);
    }
    return 0;
}
