#pragma once
#include "plugins/backend/rknn/rknn_backend.h"
#include <memory>
#include <string>

namespace humanvision::runtime::rknn {
using Context = uint64_t;
enum class Layout { Unknown, Nchw, Nhwc };
enum class Type { Unknown, Float32, Float16, UInt8, Int8, Int16, UInt16, Int32, UInt32, Int64 };
struct Attribute {
    uint32_t index = 0, rank = 0;
    uint64_t dimensions[8]{}, elements = 0;
    char name[256]{};
    Layout layout = Layout::Unknown;
    Type type = Type::Unknown;
};
struct OutputBuffer { uint32_t index; float* data; uint32_t bytes; };

// Vendor-only resource operations. Injection exercises the real backend validation
// and lifetime logic without pretending that a host has a Rockchip NPU.
class Vendor {
public:
    virtual ~Vendor() = default;
    virtual int Initialize(Context&, const void* model, uint32_t bytes) = 0;
    virtual int Versions(Context, char* runtime, char* driver, uint32_t capacity) = 0;
    virtual int SetCoreMask(Context, uint32_t mask) = 0;
    virtual int IoCount(Context, uint32_t& inputs, uint32_t& outputs) = 0;
    virtual int InputAttribute(Context, Attribute&) = 0;
    virtual int OutputAttribute(Context, Attribute&) = 0;
    virtual int PrepareOutputs(const OutputBuffer*, uint32_t count) = 0;
    virtual int InputsSet(Context, const void* rgb, uint32_t bytes) = 0;
    virtual int Run(Context) = 0;
    virtual int OutputsGet(Context, OutputBuffer*, uint32_t count) = 0;
    virtual int OutputsRelease(Context, uint32_t count) noexcept = 0;
    virtual void Destroy(Context) noexcept = 0;
};
class Loader {
public:
    virtual ~Loader() = default;
    virtual std::unique_ptr<Vendor> Load(const char* path, std::string& error) = 0;
};
// Platform loader symbol contract, shared with the Android implementation.
class Library {
public:
    virtual ~Library() = default;
    virtual bool Open(const char* path, std::string& error) = 0;
    virtual void* Symbol(const char* name) = 0;
    virtual void Close() noexcept = 0;
};
struct Symbols { void* values[8]{}; };
bool ResolveSymbols(Library&, const char* path, Symbols&, std::string& error);
HV_Result CreateRknnSession(const HV_BackendConfigV1*, void**, HV_ErrorBufferV1*, Loader&);
std::unique_ptr<Loader> MakePlatformLoader();
}
