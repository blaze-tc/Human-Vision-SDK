#include "net.h"
#include "gpu.h"
#include "command.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

using Clock = std::chrono::steady_clock;
static double ms(Clock::time_point a, Clock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

int main(int argc, char** argv) {
    if (argc != 4) return 2;
    ncnn::create_gpu_instance();
    if (ncnn::get_gpu_count() < 1) return 3;
    ncnn::Net net;
    net.opt.use_vulkan_compute = true;
    net.opt.use_fp16_storage = true;
    net.opt.use_fp16_arithmetic = true;
    net.opt.use_fp16_packed = true;
    if (net.load_param(argv[1]) || net.load_model(argv[2])) return 4;
    std::vector<float> data(3 * 300 * 300);
    FILE* f = std::fopen(argv[3], "rb");
    if (!f) return 5;
    bool ok = std::fread(data.data(), 4, data.size(), f) == data.size();
    std::fclose(f);
    if (!ok) return 6;
    ncnn::Mat input(300, 300, 3);
    for (int c = 0; c < 3; ++c)
        for (int y = 0; y < 300; ++y)
            for (int x = 0; x < 300; ++x)
                input.channel(c).row(y)[x] = data[(c * 300 + y) * 300 + x];
    ncnn::VulkanDevice* dev = ncnn::get_gpu_device();
    ncnn::VkAllocator* blob = dev->acquire_blob_allocator();
    ncnn::VkAllocator* staging = dev->acquire_staging_allocator();
    ncnn::Option opt = net.opt;
    opt.blob_vkallocator = blob;
    opt.staging_vkallocator = staging;
    ncnn::VkMat vin;
    {
        ncnn::VkCompute cmd(dev);
        cmd.record_upload(input, vin, opt);
        if (cmd.submit_and_wait()) return 7;
    }
    std::printf("input,c=%d,pack=%d,bits=%d\n", vin.c, vin.elempack, vin.elembits());
    const char* names[] = {"mbox_loc", "mbox_conf_flatten", "mbox_priorbox"};
    std::vector<double> times;
    times.reserve(100);
    std::printf("sample,graph_ms,download_ms,total_ms,checksum\n");
    for (int i = 0; i < 120; ++i) {
        auto t0 = Clock::now();
        ncnn::VkMat gpu[3];
        {
            ncnn::VkCompute cmd(dev);
            ncnn::Extractor ex = net.create_extractor();
            ex.set_blob_vkallocator(blob);
            ex.set_workspace_vkallocator(blob);
            ex.set_staging_vkallocator(staging);
            int e = ex.input("data", vin);
            for (int j = 0; j < 3 && !e; ++j) e = ex.extract(names[j], gpu[j], cmd);
            if (!e) e = cmd.submit_and_wait();
            if (e) { std::fprintf(stderr, "graph_error=%d\n", e); return 8; }
        }
        auto t1 = Clock::now();
        ncnn::Mat cpu[3];
        {
            ncnn::VkCompute cmd(dev);
            ncnn::Option download = opt;
            download.use_packing_layout = false;
            for (int j = 0; j < 3; ++j) {
                ncnn::VkMat unpacked;
                dev->convert_packing(gpu[j], unpacked, 1, 1, cmd, opt);
                cmd.record_download(unpacked, cpu[j], download);
            }
            if (cmd.submit_and_wait()) return 9;
        }
        auto t2 = Clock::now();
        double checksum = 0;
        for (int j = 0; j < 3; ++j) {
            if (cpu[j].empty()) return 10;
            if (i == 0) std::printf("output,%s,%d,%d,%d,%d,%zu\n", names[j], cpu[j].w, cpu[j].h, cpu[j].c, cpu[j].elempack, cpu[j].elemsize);
            checksum += ((const float*)cpu[j].data)[0];
        }
        if (i >= 20) {
            double elapsed = ms(t0, t2);
            times.push_back(elapsed);
            std::printf("%d,%.4f,%.4f,%.4f,%.7f\n", i - 20, ms(t0, t1), ms(t1, t2), elapsed, checksum);
        }
    }
    std::sort(times.begin(), times.end());
    std::printf("summary,n=100,p50=%.4f,p95=%.4f,max=%.4f\n", times[49], times[94], times.back());
    dev->reclaim_blob_allocator(blob);
    dev->reclaim_staging_allocator(staging);
    return 0;
}
