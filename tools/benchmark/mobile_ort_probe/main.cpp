// Private real-device experiment: identical FP32 tensors, bounded thread counts,
// all output tensors saved for parity. This is model time, not skeleton FPS.
#include <onnxruntime_cxx_api.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 7 && argc != 8) {
        std::fprintf(stderr, "usage: probe model.onnx input.fp32 side threads iterations output-prefix [cpu|xnnpack]\n");
        return 2;
    }
    const int side=std::atoi(argv[3]), threads=std::atoi(argv[4]), iterations=std::atoi(argv[5]);
    const std::string provider=argc==8?argv[7]:"cpu";
    if(provider!="cpu" && provider!="xnnpack") return 2;
    if (side<32 || side>2048 || threads<1 || threads>8 || iterations<1 || iterations>500) return 2;
    try {
        std::vector<float> input(size_t(side)*side*3);
        std::ifstream file(argv[2],std::ios::binary);
        file.read(reinterpret_cast<char*>(input.data()),input.size()*sizeof(float));
        if (!file || file.peek()!=std::char_traits<char>::eof()) return 3;
        for (float x:input) if(!std::isfinite(x)) return 3;
        Ort::Env environment(ORT_LOGGING_LEVEL_WARNING,"HumanVisionThreadProbe");
        Ort::SessionOptions options;
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        options.SetExecutionMode(ExecutionMode::ORT_SEQUENTIAL);
        options.SetIntraOpNumThreads(provider=="cpu"?threads:1); options.SetInterOpNumThreads(1);
        options.AddConfigEntry("session.intra_op.allow_spinning","0");
        options.AddConfigEntry("session.inter_op.allow_spinning","0");
        if(provider=="xnnpack") {
            const char* keys[]{"intra_op_num_threads"};
            const auto count=std::to_string(threads); const char* values[]{count.c_str()};
            Ort::ThrowOnError(Ort::GetApi().SessionOptionsAppendExecutionProvider(options,"XNNPACK",keys,values,1));
        }
        Ort::Session session(environment,argv[1],options);
        if(session.GetInputCount()!=1) return 4;
        auto declared=session.GetInputTypeInfo(0).GetTensorTypeAndShapeInfo();
        std::vector<int64_t> shape{1,3,side,side};
        const auto declared_shape=declared.GetShape();
        if(declared.GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT || declared_shape.size()!=shape.size()) return 4;
        for(size_t i=0;i<shape.size();++i) if(declared_shape[i]>0 && declared_shape[i]!=shape[i]) return 4;
        Ort::AllocatorWithDefaultOptions allocator;
        auto input_name=session.GetInputNameAllocated(0,allocator);
        std::vector<std::string> output_names;
        for(size_t i=0;i<session.GetOutputCount();++i) output_names.emplace_back(session.GetOutputNameAllocated(i,allocator).get());
        std::vector<const char*> names;
        for(const auto& name:output_names) names.push_back(name.c_str());
        auto memory=Ort::MemoryInfo::CreateCpu(OrtArenaAllocator,OrtMemTypeDefault);
        auto tensor=Ort::Value::CreateTensor<float>(memory,input.data(),input.size(),shape.data(),shape.size());
        const char* name=input_name.get();
        std::vector<double> times; std::vector<Ort::Value> output;
        for(int i=0;i<iterations+10;++i) {
            auto begin=std::chrono::steady_clock::now();
            output=session.Run(Ort::RunOptions{nullptr},&name,&tensor,1,names.data(),names.size());
            auto end=std::chrono::steady_clock::now();
            if(i>=10) times.push_back(std::chrono::duration<double,std::milli>(end-begin).count());
        }
        double sum=0;for(double ms:times) sum+=ms;
        std::sort(times.begin(),times.end());
        std::printf("{\"threads\":%d,\"iterations\":%d,\"mean_ms\":%.6f,\"p95_ms\":%.6f,\"max_ms\":%.6f,\"outputs\":[",threads,iterations,sum/times.size(),times[(times.size()-1)*95/100],times.back());
        for(size_t i=0;i<output.size();++i) {
            auto info=output[i].GetTensorTypeAndShapeInfo();
            if(info.GetElementType()!=ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) return 5;
            const auto count=info.GetElementCount(); const auto* values=output[i].GetTensorData<float>();
            for(size_t j=0;j<count;++j) if(!std::isfinite(values[j])) return 5;
            std::ofstream saved(std::string(argv[6])+"-"+output_names[i]+".fp32",std::ios::binary);
            saved.write(reinterpret_cast<const char*>(values),count*sizeof(float));
            if(!saved) return 6;
            std::printf("%s{\"name\":\"%s\",\"shape\":[",i?",":"",output_names[i].c_str());
            const auto dimensions=info.GetShape();
            for(size_t j=0;j<dimensions.size();++j) std::printf("%s%lld",j?",":"",static_cast<long long>(dimensions[j]));
            std::printf("]}");
        }
        std::printf("]}\n"); return 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"%s\n",error.what());return 1;}
}
