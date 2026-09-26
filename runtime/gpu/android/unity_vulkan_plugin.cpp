#include "gpu/android/unity_vulkan_plugin.h"

#if defined(__ANDROID__)

#include "IUnityGraphics.h"
#include "IUnityGraphicsVulkan.h"
#include "IUnityInterface.h"
#include "gpu/android/shaders/embedded_shaders.h"
#include "gpu/vulkan/vulkan_device_identity.h"
#include "gpu/android/ahb_capabilities.h"

#include <android/hardware_buffer.h>
#include <unistd.h>
#include <vulkan/vulkan_android.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <chrono>
#include <vector>

namespace humanvision::gpu {
namespace {

struct AndroidSlot {
  struct SourceView {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
  };
  AHardwareBuffer *ahb = nullptr;
  VkDevice device = VK_NULL_HANDLE;
  VkImage image = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkCommandPool command_pool = VK_NULL_HANDLE;
  VkCommandBuffer command = VK_NULL_HANDLE;
  VkSemaphore semaphore = VK_NULL_HANDLE;
  VkFence submission_fence = VK_NULL_HANDLE;
  VkSampler sampler = VK_NULL_HANDLE;
  VkDescriptorSetLayout descriptor_layout = VK_NULL_HANDLE;
  VkDescriptorPool descriptor_pool = VK_NULL_HANDLE;
  VkDescriptorSet descriptor = VK_NULL_HANDLE;
  VkRenderPass render_pass = VK_NULL_HANDLE;
  VkFramebuffer framebuffer = VK_NULL_HANDLE;
  VkPipelineLayout pipeline_layout = VK_NULL_HANDLE;
  VkPipeline pipeline = VK_NULL_HANDLE;
  VkShaderModule vertex = VK_NULL_HANDLE;
  VkShaderModule fragment = VK_NULL_HANDLE;
  std::array<SourceView, 3> source_views{};
  std::mutex source_mutex;
  VkImageView active_source_view = VK_NULL_HANDLE;
  VkFormat format = VK_FORMAT_UNDEFINED;
  uint32_t width = 0, height = 0;
  bool destination_initialized = false;
  bool submitted = false;
  bool export_pending = false;
  const std::atomic<bool>* device_live = nullptr;
  bool Alive() const noexcept { return !device_live || device_live->load(std::memory_order_acquire); }
  ~AndroidSlot() {
    // An out-of-order Unity device shutdown quarantines the generation. The
    // VkDevice owner reclaims its children; later control cleanup must never
    // issue Vulkan destruction calls against that dead device.
    if (device && Alive()) {
    for (auto &source : source_views)
      if (source.view)
        vkDestroyImageView(device, source.view, nullptr);
    if (pipeline)
      vkDestroyPipeline(device, pipeline, nullptr);
    if (vertex)
      vkDestroyShaderModule(device, vertex, nullptr);
    if (fragment)
      vkDestroyShaderModule(device, fragment, nullptr);
    if (pipeline_layout)
      vkDestroyPipelineLayout(device, pipeline_layout, nullptr);
    if (framebuffer)
      vkDestroyFramebuffer(device, framebuffer, nullptr);
    if (render_pass)
      vkDestroyRenderPass(device, render_pass, nullptr);
    if (descriptor_pool)
      vkDestroyDescriptorPool(device, descriptor_pool, nullptr);
    if (descriptor_layout)
      vkDestroyDescriptorSetLayout(device, descriptor_layout, nullptr);
    if (sampler)
      vkDestroySampler(device, sampler, nullptr);
        if (submission_fence)
          vkDestroyFence(device, submission_fence, nullptr);
        if (semaphore)
      vkDestroySemaphore(device, semaphore, nullptr);
    if (command_pool)
      vkDestroyCommandPool(device, command_pool, nullptr);
    if (view)
      vkDestroyImageView(device, view, nullptr);
    if (image)
      vkDestroyImage(device, image, nullptr);
    if (memory)
      vkFreeMemory(device, memory, nullptr);
    }
    // Unity importer owner, then allocation owner. The ncnn backend retains
    // its own generation reference when it warms the consumer slot cache.
    if (ahb) {
      AHardwareBuffer_release(ahb);
      AHardwareBuffer_release(ahb);
    }
  }
};

class AndroidProducer {
public:
  AndroidProducer() : bridge_(MakeDispatch()), source_worker_([this] { SourceWorker(); }) {}
  ~AndroidProducer() {
    { std::lock_guard<std::mutex> lock(source_requests_mutex_); stop_source_worker_ = true; }
    source_requests_cv_.notify_one();
#if defined(HV_ANDROID_ADAPTER_TEST)
    { std::lock_guard<std::mutex> gate(test_worker_mutex_);
      test_pause_source_worker_ = false; }
    test_worker_cv_.notify_all();
#endif
    source_worker_.join();
  }

  UnityVulkanBridgeDispatch MakeDispatch() noexcept {
    return {this, Create, Drain, Access, PrepareSource, Release, QueueAccess, Blit,
            Color, Submit, Export, Complete, Cancel};
  }

  bool Configure(const AhbSelection &selection,
                 const SlotContract &contract) noexcept {
    std::lock_guard<std::mutex> lock(control_);
    return ConfigureLocked(selection, contract, false);
  }
  bool ConfigureLocked(const AhbSelection &selection,
                       const SlotContract &contract,
                       bool preserve_source_lease) noexcept {
    if (!preserve_source_lease) InvalidateSourceLease();
    if (!vulkan_ || !have_device_ || !device_live_.load(std::memory_order_acquire)) {
      diagnostic_ = "Unity Vulkan device is not initialized";
      return false;
    }
    runtime_error_.store(0, std::memory_order_release);
    bool configured = bridge_.Initialize(device_context_, selection, contract);
    if (!preserve_source_lease) ReleaseMeasuredNcnnLease();
    if (!device_live_.load(std::memory_order_acquire)) {
      bridge_.Shutdown();
      configured=false;
    }
    if (!configured && diagnostic_.empty()) diagnostic_="Android Vulkan generation creation rejected measured source/device/AHB contract or failed allocating cached resources";
    {
      std::lock_guard<std::mutex> queue_lock(queue_mutex_);
      queue_closed_ = !configured;
    }
    return configured;
  }
  void Shutdown() noexcept {
    std::lock_guard<std::mutex> lock(control_);
    InvalidateSourceLease();
    bridge_.Shutdown();
    ReleaseMeasuredNcnnLease();
  }
  bool BeginSourceLease(void *texture) noexcept {
    std::lock_guard<std::mutex> lock(control_);
    if (bridge_.IsQuarantined() || !texture || !device_live_.load(std::memory_order_acquire) ||
        source_lease_texture_.load(std::memory_order_acquire)) return false;
    source_image_.store(0, std::memory_order_release);
    source_lease_generation_.store(bridge_.Generation(), std::memory_order_release);
    source_lease_token_.fetch_add(1, std::memory_order_acq_rel);
#if defined(HV_ANDROID_GPU_GATE)
    gate_probe_token_ = 0;
#endif
    source_lease_texture_.store(texture, std::memory_order_release);
    return true;
  }
  void ConfigurationEvent(void *data) noexcept {
    const auto event_token = reinterpret_cast<uintptr_t>(data);
    if (!event_token ||
        event_token != configuration_event_token_.load(std::memory_order_acquire) ||
        !configuration_requested_.load(std::memory_order_acquire) ||
        event_token != source_lease_token_.load(std::memory_order_acquire) ||
        !vulkan_ || !device_live_.load(std::memory_order_acquire)) {
      return;
    }
    HV_AndroidGpuSubmissionV1 submission{};
    {
      std::lock_guard<std::mutex> lock(source_requests_mutex_);
      if (event_token != configuration_event_token_.load(std::memory_order_acquire)) return;
      submission = configuration_event_submission_;
    }
    if (source_lease_texture_.load(std::memory_order_acquire) != submission.unity_texture) return;
    UnityVulkanImage observed{};
    const bool ok = vulkan_->AccessTexture(submission.unity_texture,
        UnityVulkanWholeImage, VK_IMAGE_LAYOUT_GENERAL,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
        kUnityVulkanResourceAccess_ObserveOnly, &observed);
    if (ok) {
      VulkanSourceImage source{};
      source.width = observed.extent.width;
      source.height = observed.extent.height;
      source.format = observed.format;
      source.usage = observed.usage;
      source.tiling = observed.tiling;
      source.samples = observed.samples;
      source.layers = static_cast<uint32_t>(observed.layers);
      source.image_type = observed.type;
      {
        std::lock_guard<std::mutex> lock(source_requests_mutex_);
        if (event_token != configuration_event_token_.load(std::memory_order_acquire)) return;
        measured_source_ = source;
        measured_submission_ = submission;
        measured_lease_token_ = event_token;
        configuration_ready_.store(true, std::memory_order_release);
      }
      source_requests_cv_.notify_one();
    } else {
      std::lock_guard<std::mutex> lock(source_requests_mutex_);
      if (event_token != configuration_event_token_.load(std::memory_order_acquire)) return;
      runtime_error_.store(4, std::memory_order_release);
      configuration_failed_.store(true, std::memory_order_release);
    }
    std::lock_guard<std::mutex> lock(source_requests_mutex_);
    if (event_token == configuration_event_token_.load(std::memory_order_acquire))
      configuration_event_inflight_.store(false, std::memory_order_release);
  }
public:
  bool SourceRequiresRetention() const noexcept { return bridge_.IsQuarantined(); }
  static void UNITY_INTERFACE_API RenderEvent(int event_id, void *data) noexcept {
    if (event_id == 1) Get().ConfigurationEvent(data);
    else UnityVulkanBridge::RenderEvent(event_id, data);
  }
  void EndSourceLease() noexcept {
    std::lock_guard<std::mutex> lock(control_);
    InvalidateSourceLease();
    bridge_.Shutdown();
    // Quarantine joins CPU users but intentionally retains AndroidSlot owners.
    // Caller must inspect SourceRequiresRetention before destroying its texture.
    ReleaseMeasuredNcnnLease();
    std::lock_guard<std::mutex> queue(queue_mutex_);
    queue_closed_ = true;
  }
  BridgeResult Prepare(const HV_AndroidGpuSubmissionV1 &s,
                       void **out) noexcept {
    if (!device_live_.load(std::memory_order_acquire)) { if(out)*out=nullptr; return BridgeResult::Closed; }
    if (bridge_.IsClosed() && source_lease_texture_.load(std::memory_order_acquire) == s.unity_texture) {
      if (out) *out = nullptr;
      if (!configuration_failed_.load(std::memory_order_acquire) &&
          !configuration_event_inflight_.load(std::memory_order_acquire) &&
          !configuration_requested_.exchange(true, std::memory_order_acq_rel)) {
        const auto token = source_lease_token_.load(std::memory_order_acquire);
        {
          std::lock_guard<std::mutex> lock(source_requests_mutex_);
          configuration_event_submission_ = s;
          configuration_event_token_.store(token, std::memory_order_release);
        }
        configuration_event_inflight_.store(true, std::memory_order_release);
        // Unity treats event data as opaque. The never-reused lease sequence is
        // encoded in the pointer value, so a delayed callback has no record to
        // dereference and cannot alias a later request at the same texture.
        if (out) *out = reinterpret_cast<void*>(static_cast<uintptr_t>(token));
      }
      configuration_pending_drops_.fetch_add(1, std::memory_order_relaxed);
      return BridgeResult::Busy;
    }
    if (source_lease_texture_.load(std::memory_order_acquire) != s.unity_texture ||
        source_lease_generation_.load(std::memory_order_acquire) != bridge_.Generation()) {
      if (out) *out = nullptr;
      return BridgeResult::Closed;
    }
    return bridge_.Prepare(s, out);
  }
  void Status(HV_AndroidGpuBridgeStatusV1 &s) noexcept {
    bridge_.GetStatus(s);
    // Configuration pending frames are superseded by the measured generation.
    s.dropped_generation += configuration_pending_drops_.load(std::memory_order_relaxed);
  }
#if defined(HV_ANDROID_GPU_GATE)
  UnityVulkanBridge* GateBridge() noexcept { return &bridge_; }
  bool GateContext(VulkanDeviceContext& out) noexcept {
    std::lock_guard<std::mutex> lock(control_);
    if (!have_device_ || !device_live_.load(std::memory_order_acquire)) return false;
    out = producer_context_;
    return true;
  }
#endif
  const char *Diagnostic() const noexcept {
    switch (runtime_error_.load(std::memory_order_acquire)) {
    case 1:
      return "Android Vulkan source image-view creation failed; rebuild the bridge generation";
    case 2:
      return "Android Vulkan source image identity changed during its lease; end the source lease and rebuild the bridge generation before replacing the RenderTexture";
    case 3:
      return "Unity Vulkan device shutdown occurred before required control teardown; generation quarantined, call ShutdownUnityVulkanProducer before device destruction";
    case 4:
      return "Unity Vulkan AccessTexture failed while measuring the active camera source";
    default:
      break;
    }
    if (configuration_failed_.load(std::memory_order_acquire)) {
      thread_local std::string failure;
      std::lock_guard<std::mutex> lock(control_);
      failure = diagnostic_;
      return failure.c_str();
    }
    const char* bridge_error=bridge_.Diagnostic();
    if(*bridge_error)return bridge_error;
    thread_local std::string snapshot;
    std::lock_guard<std::mutex> lock(control_);
    snapshot = diagnostic_;
    return snapshot.c_str();
  }
#if defined(HV_ANDROID_GPU_GATE)
  const char *GateError() const noexcept {
    if (runtime_error_.load(std::memory_order_acquire) ||
        configuration_failed_.load(std::memory_order_acquire)) return Diagnostic();
    const char* bridge_error = bridge_.Diagnostic();
    return *bridge_error ? bridge_error : "";
  }
  const char *GateProbe() const noexcept {
    thread_local std::string snapshot;
    std::lock_guard<std::mutex> lock(control_);
    snapshot = gate_probe_token_ != 0 &&
        gate_probe_token_ == source_lease_token_.load(std::memory_order_acquire) &&
        source_lease_texture_.load(std::memory_order_acquire) &&
        !configuration_failed_.load(std::memory_order_acquire) ? diagnostic_ : "";
    return snapshot.c_str();
  }
#endif

  void Load(IUnityInterfaces *interfaces) noexcept {
    interfaces_ = interfaces;
    graphics_ = interfaces ? interfaces->Get<IUnityGraphics>() : nullptr;
    vulkan_ = interfaces ? interfaces->Get<IUnityGraphicsVulkanV2>() : nullptr;
    if (vulkan_) {
      interception_installed_ =
          vulkan_->InterceptInitialization(InitializeVulkan, this);
      if (!interception_installed_)
        diagnostic_ = "Unity Vulkan interception was installed too late; preload libhumanvision.so";
    }
    if (graphics_) {
      graphics_->RegisterDeviceEventCallback(DeviceEvent);
      DeviceEvent(kUnityGfxDeviceEventInitialize);
    }
  }
  void Unload() noexcept {
    if (graphics_)
      graphics_->UnregisterDeviceEventCallback(DeviceEvent);
    OnDevice(kUnityGfxDeviceEventShutdown);
    graphics_ = nullptr;
    vulkan_ = nullptr;
    interfaces_ = nullptr;
  }
  void OnDevice(UnityGfxDeviceEventType event) noexcept {
    // Unity may destroy VkDevice as soon as this callback returns. Control
    // creation/destruction must finish before it does; frame calls use the
    // bridge's separate nonblocking admission path.
    std::unique_lock<std::mutex> lock(control_);
    if (event == kUnityGfxDeviceEventShutdown ||
        event == kUnityGfxDeviceEventBeforeReset) {
      InvalidateSourceLease();
      // B6 must call the control seam before Unity reaches this callback.
      // This callback cannot wait for events or GPU completion.
      if (!bridge_.IsClosed()) {
        bridge_.CloseAdmission();
        runtime_error_.store(3,std::memory_order_release);
      }
      if (bridge_.IsClosed()) ReleaseMeasuredNcnnLease();
      device_live_.store(false,std::memory_order_release);
      have_device_ = false;
      device_context_ = {};
      producer_context_ = {};
      return;
    }
    if (event != kUnityGfxDeviceEventInitialize &&
        event != kUnityGfxDeviceEventAfterReset)
      return;
    have_device_ = false;
    device_context_ = {};
    producer_context_ = {};
    if (!graphics_ || graphics_->GetRenderer() != kUnityGfxRendererVulkan ||
        !vulkan_) {
      diagnostic_ =
          "Android GPU bridge requires Unity Vulkan as the active renderer";
      return;
    }
    if (!bridge_.IsClosed()) {
      diagnostic_="Unity Vulkan prior generation still requires control teardown";
      return;
    }
    if (!interception_installed_ || !required_extensions_enabled_.load(std::memory_order_acquire)) {
      diagnostic_ = "Unity Vulkan device is missing enabled VK_ANDROID_external_memory_android_hardware_buffer or VK_KHR_external_semaphore_fd";
      return;
    }
    const UnityVulkanInstance instance = vulkan_->Instance();
    if (!instance.instance || !instance.physicalDevice || !instance.device ||
        !instance.graphicsQueue) {
      diagnostic_ = "Unity returned an incomplete Vulkan device context";
      return;
    }
    if (intercepted_physical_device_.load(std::memory_order_acquire) !=
        reinterpret_cast<uintptr_t>(instance.physicalDevice) ||
        intercepted_device_.load(std::memory_order_acquire) !=
            reinterpret_cast<uintptr_t>(instance.device) ||
        created_instance_.load(std::memory_order_acquire) !=
            reinterpret_cast<uintptr_t>(instance.instance)) {
      diagnostic_ =
          "Unity Vulkan intercepted instance/physical/logical device does not match active device";
      return;
    }
    device_context_.instance = reinterpret_cast<uintptr_t>(instance.instance);
    device_context_.physical_device =
        reinterpret_cast<uintptr_t>(instance.physicalDevice);
    device_context_.device = reinterpret_cast<uintptr_t>(instance.device);
    device_context_.graphics_queue =
        reinterpret_cast<uintptr_t>(instance.graphicsQueue);
    device_context_.graphics_queue_family = instance.queueFamilyIndex;
    VulkanDeviceContext query{};
    query.instance = instance.instance;
    query.physical_device = instance.physicalDevice;
    query.device = instance.device;
    query.instance_api_version = instance_api_version_;
    query.properties2_extension = instance_properties2_;
    query.external_memory_capabilities_extension = instance_external_memory_;
    query.ahb_extension = true;
    const DeviceIdentity id = QueryUnityIdentity(query);
    const auto nonzero=[](const auto& uuid){return std::any_of(uuid.begin(),uuid.end(),[](uint8_t b){return b!=0;});};
    if (!id.queried || !nonzero(id.device_uuid) || !nonzero(id.driver_uuid)) {
      diagnostic_ = "Unity Vulkan physical-device UUID query failed";
      have_device_ = false;
      device_context_ = {};
      return;
    }
    device_context_.device_uuid = id.device_uuid;
    device_context_.driver_uuid = id.driver_uuid;
    producer_context_ = query;
    have_device_ = true;
    device_live_.store(true,std::memory_order_release);
    runtime_error_.store(0,std::memory_order_release);
    const UnityVulkanPluginEventConfig config{
        kUnityVulkanRenderPass_EnsureOutside,
        kUnityVulkanGraphicsQueueAccess_DontCare,
        kUnityVulkanEventConfigFlag_EnsurePreviousFrameSubmission};
    if (!vulkan_->ConfigureEvent) {
      have_device_=false;
      diagnostic_="Unity Vulkan ConfigureEvent is unavailable";
      return;
    }
    vulkan_->ConfigureEvent(0, &config);
    vulkan_->ConfigureEvent(1, &config);
    diagnostic_.clear();
  }

  static AndroidProducer &Get() {
    static AndroidProducer producer;
    return producer;
  }

#if defined(HV_ANDROID_ADAPTER_TEST)
  static inline thread_local bool test_inside_render_event = false;
  static bool TestInsideRenderEvent() noexcept { return test_inside_render_event; }
  static uint64_t TestGeneration() noexcept { return Get().bridge_.Generation(); }
  static bool TestCreate(uint32_t index,
                         const UnityVulkanDeviceContext &device,
                         const SlotContract &contract,
                         const AhbSelection &selection,
                         UnityVulkanSlotCache &out) noexcept {
    return Create(nullptr, index, device, contract, selection, out);
  }
  static void TestDrain(uint32_t index, UnityVulkanSlotCache &cache) noexcept {
    SyncFd fd;
    Drain(nullptr, index, AhbSlotState::Free, cache, fd);
  }
  static SourcePreparation TestPrepareSource(
      const UnityVulkanSlotCache &cache,
      const UnityTextureAccess &access) noexcept {
    test_inside_render_event = true;
    const auto result = PrepareSource(&Get(), cache, access);
    test_inside_render_event = false;
    return result;
  }
  static const char *TestDiagnostic() noexcept { return Get().Diagnostic(); }
#if defined(HV_ANDROID_GPU_GATE)
  static void TestGateProbePublication(uint64_t token, const char* diagnostic) noexcept {
    auto& self = Get();
    std::lock_guard<std::mutex> lock(self.control_);
    self.diagnostic_ = diagnostic;
    self.gate_probe_token_ = token;
  }
  static uint64_t TestSourceLeaseToken() noexcept {
    return Get().source_lease_token_.load(std::memory_order_acquire);
  }
#endif
  static bool TestBlit(const UnityVulkanSlotCache &cache,
                       const UnityTextureAccess &access,
                       const BridgeBarrier *barriers) noexcept {
    return Blit(nullptr, cache, access, barriers, 4);
  }
  static bool TestColor(const UnityVulkanSlotCache &cache,
                        const UnityTextureAccess &access,
                        const BridgeBarrier *barriers) noexcept {
    return Color(nullptr, cache, access, barriers, 4, true);
  }
  static VkResult TestInterceptCreateDevice(
      VkPhysicalDevice physical, const VkDeviceCreateInfo *create,
      VkDevice *device) noexcept {
    return InterceptCreateDevice(physical, create, nullptr, device);
  }
  static VkResult TestInterceptCreateInstance(const VkInstanceCreateInfo* create,
                                              VkInstance* instance) noexcept {
    return InterceptCreateInstance(create, nullptr, instance);
  }
  static void TestQuiesceSourceWorker() noexcept {
    auto& self = Get();
    for (int i = 0; i < 200; ++i) {
      {
        std::lock_guard<std::mutex> control(self.control_);
        std::lock_guard<std::mutex> pending(self.source_requests_mutex_);
        if (std::all_of(self.source_requests_.begin(), self.source_requests_.end(),
                        [](const auto& r) { return !r.slot; })) return;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }
  static void TestPauseSourceWorker() noexcept {
    auto& self=Get();
    std::lock_guard<std::mutex> lock(self.test_worker_mutex_);
    self.test_pause_source_worker_=true;
  }
  static void TestResumeSourceWorker() noexcept {
    auto& self=Get();
    {std::lock_guard<std::mutex> lock(self.test_worker_mutex_);
     self.test_pause_source_worker_=false;}
    self.test_worker_cv_.notify_all();
  }
  static void TestInstallGipa(PFN_vkGetInstanceProcAddr gipa) noexcept {
    Get().next_gipa_ = gipa;
    Get().required_extensions_enabled_.store(false);
    Get().intercepted_physical_device_.store(0);
    Get().intercept_instance_.store(0);
    Get().instance_api_version_ = VK_API_VERSION_1_1;
  }
  static PFN_vkVoidFunction TestInterceptGipa(VkInstance instance,
                                               const char *name) noexcept {
    return InterceptGetInstanceProcAddr(instance, name);
  }
  static bool TestRequiredExtensionsEnabled() noexcept {
    return Get().required_extensions_enabled_.load();
  }
  static uintptr_t TestInterceptedPhysicalDevice() noexcept {
    return Get().intercepted_physical_device_.load();
  }
  static void TestInstallUnityVulkan(IUnityGraphicsVulkanV2 *vulkan) noexcept {
    auto &self = Get();
    self.vulkan_ = vulkan;
    self.device_live_.store(true);
    std::lock_guard<std::mutex> lock(self.queue_mutex_);
    self.queue_closed_ = false;
  }
  static bool TestAccess(void *texture, UnityTextureAccess &out) noexcept {
    return Access(&Get(), texture, out);
  }
  static void TestRelease(void *texture,
                          const UnityTextureAccess &access) noexcept {
    Release(&Get(), texture, access);
  }
  static bool TestQueue() noexcept {
    return QueueAccess(&Get(), &UnityVulkanBridge::QueueEvent, nullptr);
  }
  static void TestCancel() noexcept { Cancel(&Get()); }
#endif

private:
  void ReleaseMeasuredNcnnLease() noexcept {
    // Keep the measured consumer-device instance lease with terminal resources.
    // A producer teardown is not proof that the consumer VkDevice can die.
    if (bridge_.IsQuarantined()) return;
#if !defined(HV_ANDROID_ADAPTER_TEST)
    if (measured_ncnn_lease_) {
      ReleaseNcnnGpuInstance();
      measured_ncnn_lease_ = false;
    }
#endif
  }
  struct SourceRequest {
    AndroidSlot* slot = nullptr;
    VkImage image = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    uint64_t bridge_generation = 0;
    uint64_t lease_token = 0;
  };
  void InvalidateSourceLease() noexcept {
#if defined(HV_ANDROID_GPU_GATE)
    gate_probe_token_ = 0;
#endif
    configuration_event_token_.store(0, std::memory_order_release);
    configuration_requested_.store(false, std::memory_order_release);
    configuration_event_inflight_.store(false, std::memory_order_release);
    configuration_ready_.store(false, std::memory_order_release);
    configuration_failed_.store(false, std::memory_order_release);
    source_lease_texture_.store(nullptr, std::memory_order_release);
    source_lease_generation_.store(0, std::memory_order_release);
    source_image_.store(0, std::memory_order_release);
    source_lease_token_.fetch_add(1, std::memory_order_acq_rel);
    std::lock_guard<std::mutex> pending(source_requests_mutex_);
    for (auto& request : source_requests_) request = {};
  }
  void ConfigureMeasuredSource() noexcept {
    // This worker is a control thread. The render event only observed Unity's
    // image and returned; neither ncnn initialization nor AHB allocation runs
    // on Unity's render thread.
    std::lock_guard<std::mutex> control(control_);
    const auto source = measured_source_;
    const auto submission = measured_submission_;
    if (!device_live_.load(std::memory_order_acquire) ||
        source_lease_texture_.load(std::memory_order_acquire) != submission.unity_texture ||
        measured_lease_token_ != source_lease_token_.load(std::memory_order_acquire) ||
        !bridge_.IsClosed()) return;
#if defined(HV_ANDROID_ADAPTER_TEST)
    diagnostic_ = "Android adapter test has no physical ncnn device";
    configuration_failed_.store(true, std::memory_order_release);
#else
    const VulkanDeviceContext producer = producer_context_;
    VulkanDeviceContext consumer{};
    if (!FindMatchedNcnnContext(producer, consumer, diagnostic_)) {
      configuration_failed_.store(true, std::memory_order_release);
      return;
    }
    measured_ncnn_lease_ = true;
    const AhbSelection selection = ProbeAndroidAhbCapabilities(
        producer, consumer, source, source.width, source.height);
    if (selection.path == HV_ANDROID_GPU_COPY_UNAVAILABLE) {
      diagnostic_ = selection.diagnostic;
      configuration_failed_.store(true, std::memory_order_release);
      return;
    }
    SlotContract contract{};
    contract.width = selection.contract.width;
    contract.height = selection.contract.height;
    contract.actual_format = selection.contract.format;
    contract.actual_usage = selection.contract.usage;
    contract.rotation = submission.rotation_degrees;
    contract.mirror = submission.mirrored != 0;
    // The same audited producer configuration path is used by the production
    // control worker and by deterministic host adapter tests.
    if (!ConfigureLocked(selection, contract, true)) {
      diagnostic_ = "Measured Android Vulkan AHB contract failed persistent slot creation: " + selection.diagnostic;
      configuration_failed_.store(true, std::memory_order_release);
      return;
    }
    source_lease_generation_.store(bridge_.Generation(), std::memory_order_release);
    { std::lock_guard<std::mutex> queue(queue_mutex_); queue_closed_ = false; }
    diagnostic_ = selection.diagnostic;
#if defined(HV_ANDROID_GPU_GATE)
    gate_probe_token_ = measured_lease_token_;
#endif
#endif
  }
  void SourceWorker() noexcept {
    for (;;) {
      {
        std::unique_lock<std::mutex> pending(source_requests_mutex_);
        source_requests_cv_.wait(pending, [&] {
          return stop_source_worker_ || configuration_ready_.load(std::memory_order_acquire) ||
              std::any_of(source_requests_.begin(), source_requests_.end(),
              [](const auto& r) { return r.slot != nullptr; });
        });
        if (stop_source_worker_) return;
      }
      if (configuration_ready_.exchange(false, std::memory_order_acq_rel)) {
        ConfigureMeasuredSource();
        continue;
      }
#if defined(HV_ANDROID_ADAPTER_TEST)
      { std::unique_lock<std::mutex> gate(test_worker_mutex_);
        test_worker_cv_.wait(gate, [&] { return !test_pause_source_worker_; }); }
#endif
      // The control lock owns slot lifetime and serializes device shutdown.
      std::lock_guard<std::mutex> control(control_);
      SourceRequest request;
      {
        std::lock_guard<std::mutex> pending(source_requests_mutex_);
        if (stop_source_worker_) return;
        for (auto& entry : source_requests_) if (entry.slot) { request = entry; entry.slot = nullptr; break; }
      }
      if (!request.slot || request.bridge_generation != bridge_.Generation() ||
          request.lease_token != source_lease_token_.load(std::memory_order_acquire) ||
          !source_lease_texture_.load(std::memory_order_acquire) ||
          reinterpret_cast<uintptr_t>(request.image) != source_image_.load(std::memory_order_acquire) ||
          !request.slot->Alive()) continue;
      std::unique_lock<std::mutex> source(request.slot->source_mutex);
      bool found = false;
      for (const auto& entry : request.slot->source_views)
        found |= entry.image == request.image && entry.format == request.format && entry.view != VK_NULL_HANDLE;
      if (found) continue;
      bool inserted = false;
      bool creation_failed = false;
      for (auto& entry : request.slot->source_views) {
        if (entry.view) continue;
        VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        view.image = request.image;
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = request.format;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(request.slot->device, &view, nullptr, &entry.view) != VK_SUCCESS) {
          runtime_error_.store(1, std::memory_order_release);
          creation_failed = true;
          break;
        }
        entry.image = request.image;
        entry.format = request.format;
        inserted = true;
        break;
      }
      source.unlock();
      if (creation_failed) {
        bridge_.CloseAdmission();
        std::lock_guard<std::mutex> pending(source_requests_mutex_);
        for (auto& entry : source_requests_) entry.slot = nullptr;
        continue;
      }
      if (!inserted) {
        runtime_error_.store(2, std::memory_order_release);
        bridge_.CloseAdmission();
        InvalidateSourceLease();
      }
    }
  }
  DeviceIdentity QueryUnityIdentity(const VulkanDeviceContext& context) const {
    DeviceIdentity id;
    const bool core=context.instance_api_version>=VK_API_VERSION_1_1;
    if(!next_gipa_ || (!core && !(context.properties2_extension && context.external_memory_capabilities_extension)))return id;
    const auto query=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(next_gipa_(context.instance,
        core ? "vkGetPhysicalDeviceProperties2" : "vkGetPhysicalDeviceProperties2KHR"));
    if(!query)return id;
    VkPhysicalDeviceIDProperties ids{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
    VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2,&ids};
    query(context.physical_device,&properties);
    std::copy_n(ids.deviceUUID,16,id.device_uuid.begin());
    std::copy_n(ids.driverUUID,16,id.driver_uuid.begin());
    id.queried=true;
    return id;
  }
  static PFN_vkGetInstanceProcAddr UNITY_INTERFACE_API InitializeVulkan(
      PFN_vkGetInstanceProcAddr next, void *userdata) {
    auto &self = *static_cast<AndroidProducer *>(userdata);
    self.next_gipa_ = next;
    self.required_extensions_enabled_.store(false);
    self.created_instance_.store(0);
    self.intercepted_device_.store(0);
    return InterceptGetInstanceProcAddr;
  }
  static PFN_vkVoidFunction VKAPI_PTR InterceptGetInstanceProcAddr(VkInstance instance,
                                                                    const char* name) {
    auto& self = Get();
    if (name && std::strcmp(name,"vkCreateInstance")==0)
      return reinterpret_cast<PFN_vkVoidFunction>(&InterceptCreateInstance);
    const bool create_device =
        name && std::strcmp(name, "vkCreateDevice") == 0;
    if (create_device && instance)
      self.intercept_instance_.store(reinterpret_cast<uintptr_t>(instance),
                                     std::memory_order_release);
    if (create_device && instance)
      return reinterpret_cast<PFN_vkVoidFunction>(&InterceptCreateDevice);
    return self.next_gipa_ ? self.next_gipa_(instance, name) : nullptr;
  }
  static VkResult VKAPI_PTR InterceptCreateInstance(const VkInstanceCreateInfo* create,
      const VkAllocationCallbacks* allocator, VkInstance* instance) {
    auto& self=Get();
    if (!self.next_gipa_ || !create || !instance) return VK_ERROR_INITIALIZATION_FAILED;
    const auto create_instance=reinterpret_cast<PFN_vkCreateInstance>(self.next_gipa_(nullptr,"vkCreateInstance"));
    if(!create_instance)return VK_ERROR_INITIALIZATION_FAILED;
    self.instance_api_version_=create->pApplicationInfo && create->pApplicationInfo->apiVersion ?
        create->pApplicationInfo->apiVersion : VK_API_VERSION_1_0;
    const auto enabled=[&](const char* name){
      for(uint32_t i=0;i<create->enabledExtensionCount;++i)
        if(std::strcmp(create->ppEnabledExtensionNames[i],name)==0)return true;
      return false;
    };
    std::vector<const char*> extensions;
    if (create->enabledExtensionCount && create->ppEnabledExtensionNames)
      extensions.assign(create->ppEnabledExtensionNames,
                        create->ppEnabledExtensionNames + create->enabledExtensionCount);
    const bool needs_extensions = self.instance_api_version_ < VK_API_VERSION_1_1;
    if (needs_extensions) {
      const auto enumerate=reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(
          self.next_gipa_(nullptr,"vkEnumerateInstanceExtensionProperties"));
      if (!enumerate) return VK_ERROR_EXTENSION_NOT_PRESENT;
      uint32_t count=0;
      if (enumerate(nullptr,&count,nullptr)!=VK_SUCCESS) return VK_ERROR_EXTENSION_NOT_PRESENT;
      std::vector<VkExtensionProperties> available(count);
      if (enumerate(nullptr,&count,available.data())!=VK_SUCCESS) return VK_ERROR_EXTENSION_NOT_PRESENT;
      const char* requirements[]={VK_KHR_GET_PHYSICAL_DEVICE_PROPERTIES_2_EXTENSION_NAME,
          VK_KHR_EXTERNAL_MEMORY_CAPABILITIES_EXTENSION_NAME,
          VK_KHR_EXTERNAL_SEMAPHORE_CAPABILITIES_EXTENSION_NAME};
      for (const char* name:requirements) {
        const bool supported=std::any_of(available.begin(),available.end(),[&](const auto& property){
          return std::strcmp(property.extensionName,name)==0;});
        if (!supported) { self.diagnostic_=std::string("Unity Vulkan instance lacks ")+name; return VK_ERROR_EXTENSION_NOT_PRESENT; }
        if (!enabled(name)) extensions.push_back(name);
      }
    }
    VkInstanceCreateInfo amended=*create;
    amended.enabledExtensionCount=static_cast<uint32_t>(extensions.size());
    amended.ppEnabledExtensionNames=extensions.data();
    self.instance_properties2_=needs_extensions;
    self.instance_external_memory_=needs_extensions;
    self.instance_external_semaphore_=needs_extensions;
    const VkResult result=create_instance(&amended,allocator,instance);
    self.created_instance_.store(result==VK_SUCCESS ? reinterpret_cast<uintptr_t>(*instance) : 0,
                                 std::memory_order_release);
    return result;
  }
  static VkResult VKAPI_PTR InterceptCreateDevice(VkPhysicalDevice physical,
      const VkDeviceCreateInfo* create, const VkAllocationCallbacks* allocator, VkDevice* device) {
    auto& self = Get();
    if (!self.next_gipa_ || !self.intercept_instance_.load()) return VK_ERROR_INITIALIZATION_FAILED;
    auto enumerate = reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(
        self.next_gipa_(
            reinterpret_cast<VkInstance>(
                self.intercept_instance_.load(std::memory_order_acquire)),
            "vkEnumerateDeviceExtensionProperties"));
    auto create_device = reinterpret_cast<PFN_vkCreateDevice>(
        self.next_gipa_(
            reinterpret_cast<VkInstance>(
                self.intercept_instance_.load(std::memory_order_acquire)),
            "vkCreateDevice"));
    if (!enumerate || !create_device || !create) return VK_ERROR_INITIALIZATION_FAILED;
    uint32_t count=0;
    if(enumerate(physical,nullptr,&count,nullptr)!=VK_SUCCESS)return VK_ERROR_EXTENSION_NOT_PRESENT;
    std::vector<VkExtensionProperties> props(count);
    if (enumerate(physical,nullptr,&count,props.data()) != VK_SUCCESS) return VK_ERROR_EXTENSION_NOT_PRESENT;
    const char* required[] = {VK_ANDROID_EXTERNAL_MEMORY_ANDROID_HARDWARE_BUFFER_EXTENSION_NAME,
                              VK_KHR_EXTERNAL_SEMAPHORE_FD_EXTENSION_NAME};
    std::vector<const char *> extensions;
    if (create->enabledExtensionCount && create->ppEnabledExtensionNames)
      extensions.assign(create->ppEnabledExtensionNames,
                        create->ppEnabledExtensionNames +
                            create->enabledExtensionCount);
    const auto query_properties=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(self.next_gipa_(
        reinterpret_cast<VkInstance>(self.intercept_instance_.load()),"vkGetPhysicalDeviceProperties"));
    VkPhysicalDeviceProperties physical_properties{};
    if(query_properties)query_properties(physical,&physical_properties);
    const bool core=self.instance_api_version_>=VK_API_VERSION_1_1 && physical_properties.apiVersion>=VK_API_VERSION_1_1;
    bool all_supported = core || (self.instance_properties2_ && self.instance_external_memory_ && self.instance_external_semaphore_);
    for (const char* requirement : required) {
      const bool supported=std::any_of(props.begin(),props.end(),[&](const VkExtensionProperties& p){return std::strcmp(p.extensionName,requirement)==0;});
      if (!supported) { all_supported = false; continue; }
      if (std::none_of(extensions.begin(),extensions.end(),[&](const char* e){return std::strcmp(e,requirement)==0;})) extensions.push_back(requirement);
    }
    // Vulkan 1.0 requires the unpromoted AHB/external-semaphore dependencies.
    if (!core) {
      const char* dependencies[]={VK_KHR_EXTERNAL_MEMORY_EXTENSION_NAME,
          VK_KHR_GET_MEMORY_REQUIREMENTS_2_EXTENSION_NAME, VK_KHR_DEDICATED_ALLOCATION_EXTENSION_NAME,
          VK_KHR_SAMPLER_YCBCR_CONVERSION_EXTENSION_NAME, VK_KHR_BIND_MEMORY_2_EXTENSION_NAME,
          VK_KHR_MAINTENANCE1_EXTENSION_NAME, VK_KHR_EXTERNAL_SEMAPHORE_EXTENSION_NAME};
      for(const char* dependency:dependencies) {
        const bool supported=std::any_of(props.begin(),props.end(),[&](const auto& p){return std::strcmp(p.extensionName,dependency)==0;});
        if(!supported){all_supported=false;continue;}
        if(std::none_of(extensions.begin(),extensions.end(),[&](const char* e){return std::strcmp(e,dependency)==0;}))extensions.push_back(dependency);
      }
    }
    VkDeviceCreateInfo amended=*create; amended.enabledExtensionCount=static_cast<uint32_t>(extensions.size()); amended.ppEnabledExtensionNames=extensions.data();
    const VkResult result=create_device(physical,&amended,allocator,device);
    self.required_extensions_enabled_.store(result == VK_SUCCESS && all_supported,
                                             std::memory_order_release);
    if (result == VK_SUCCESS && all_supported)
      self.intercepted_physical_device_.store(
          reinterpret_cast<uintptr_t>(physical), std::memory_order_release);
    self.intercepted_device_.store(result==VK_SUCCESS && all_supported ? reinterpret_cast<uintptr_t>(*device):0,
                                   std::memory_order_release);
    return result;
  }
  static void UNITY_INTERFACE_API DeviceEvent(UnityGfxDeviceEventType event) {
    Get().OnDevice(event);
  }
  static VkDevice D(const UnityVulkanDeviceContext &c) {
    return reinterpret_cast<VkDevice>(c.device);
  }
  static VkPhysicalDevice P(const UnityVulkanDeviceContext &c) {
    return reinterpret_cast<VkPhysicalDevice>(c.physical_device);
  }
  static VkQueue Q(const UnityVulkanDeviceContext &c) {
    return reinterpret_cast<VkQueue>(c.graphics_queue);
  }
  static AndroidSlot *S(const UnityVulkanSlotCache &c) {
    return reinterpret_cast<AndroidSlot *>(c.ahb);
  }

  static uint32_t MemoryType(VkPhysicalDevice physical,
                             uint32_t bits) noexcept {
    VkPhysicalDeviceMemoryProperties p{};
    vkGetPhysicalDeviceMemoryProperties(physical, &p);
    for (uint32_t i = 0; i < p.memoryTypeCount; ++i)
      if (bits & (1u << i))
        return i;
    return UINT32_MAX;
  }
  static VkImageLayout Layout(BridgeImageLayout l) noexcept {
    switch (l) {
    case BridgeImageLayout::TransferSource:
      return VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    case BridgeImageLayout::TransferDestination:
      return VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    case BridgeImageLayout::ShaderRead:
      return VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    case BridgeImageLayout::ColorAttachment:
      return VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    case BridgeImageLayout::External:
      return VK_IMAGE_LAYOUT_GENERAL;
    default:
      return VK_IMAGE_LAYOUT_GENERAL;
    }
  }
  static VkAccessFlags SrcAccess(BridgeImageLayout l) noexcept {
    if (l == BridgeImageLayout::TransferSource)
      return VK_ACCESS_TRANSFER_READ_BIT;
    if (l == BridgeImageLayout::TransferDestination)
      return VK_ACCESS_TRANSFER_WRITE_BIT;
    if (l == BridgeImageLayout::ShaderRead)
      return VK_ACCESS_SHADER_READ_BIT;
    if (l == BridgeImageLayout::ColorAttachment)
      return VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    return 0;
  }
  static VkAccessFlags DstAccess(BridgeImageLayout l) noexcept {
    return SrcAccess(l);
  }
  static VkPipelineStageFlags Stage(BridgeImageLayout l) noexcept {
    if (l == BridgeImageLayout::TransferSource ||
        l == BridgeImageLayout::TransferDestination)
      return VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (l == BridgeImageLayout::ShaderRead)
      return VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    if (l == BridgeImageLayout::ColorAttachment)
      return VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    return VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
  }
  static VkImageMemoryBarrier Barrier(const BridgeBarrier &b) noexcept {
    VkImageMemoryBarrier v{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    v.srcAccessMask = SrcAccess(b.old_layout);
    v.dstAccessMask = DstAccess(b.new_layout);
    v.oldLayout = Layout(b.old_layout);
    v.newLayout = Layout(b.new_layout);
    v.srcQueueFamilyIndex = b.source_queue_family;
    v.dstQueueFamilyIndex = b.destination_queue_family;
    v.image = reinterpret_cast<VkImage>(b.image);
    v.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    return v;
  }
  static bool Module(VkDevice d, const uint32_t *words, size_t bytes,
                     VkShaderModule &out) noexcept {
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = bytes;
    ci.pCode = words;
    return vkCreateShaderModule(d, &ci, nullptr, &out) == VK_SUCCESS;
  }
  static bool CreateColor(AndroidSlot &s) noexcept {
    VkSamplerCreateInfo sci{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    sci.magFilter = sci.minFilter = VK_FILTER_LINEAR;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = sci.addressModeV = sci.addressModeW =
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.maxLod = 1;
    if (vkCreateSampler(s.device, &sci, nullptr, &s.sampler) != VK_SUCCESS)
      return false;
    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    VkDescriptorSetLayoutCreateInfo dl{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    dl.bindingCount = 1;
    dl.pBindings = &binding;
    if (vkCreateDescriptorSetLayout(s.device, &dl, nullptr,
                                    &s.descriptor_layout) != VK_SUCCESS)
      return false;
    VkDescriptorPoolSize ps{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo dp{
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    dp.maxSets = 1;
    dp.poolSizeCount = 1;
    dp.pPoolSizes = &ps;
    if (vkCreateDescriptorPool(s.device, &dp, nullptr, &s.descriptor_pool) !=
        VK_SUCCESS)
      return false;
    VkDescriptorSetAllocateInfo da{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    da.descriptorPool = s.descriptor_pool;
    da.descriptorSetCount = 1;
    da.pSetLayouts = &s.descriptor_layout;
    if (vkAllocateDescriptorSets(s.device, &da, &s.descriptor) != VK_SUCCESS)
      return false;
    VkAttachmentDescription attachment{};
    attachment.format = s.format;
    attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &ref;
    VkRenderPassCreateInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rp.attachmentCount = 1;
    rp.pAttachments = &attachment;
    rp.subpassCount = 1;
    rp.pSubpasses = &sub;
    if (vkCreateRenderPass(s.device, &rp, nullptr, &s.render_pass) !=
        VK_SUCCESS)
      return false;
    VkFramebufferCreateInfo fb{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fb.renderPass = s.render_pass;
    fb.attachmentCount = 1;
    fb.pAttachments = &s.view;
    fb.width = s.width;
    fb.height = s.height;
    fb.layers = 1;
    if (vkCreateFramebuffer(s.device, &fb, nullptr, &s.framebuffer) !=
        VK_SUCCESS)
      return false;
    VkPushConstantRange push{VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4};
    VkPipelineLayoutCreateInfo pl{
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &s.descriptor_layout;
    pl.pushConstantRangeCount = 1;
    pl.pPushConstantRanges = &push;
    if (vkCreatePipelineLayout(s.device, &pl, nullptr, &s.pipeline_layout) !=
            VK_SUCCESS ||
        !Module(s.device, shaders::kFullscreenVertexSpv,
                sizeof(shaders::kFullscreenVertexSpv), s.vertex) ||
        !Module(s.device, shaders::kCopyRgbaFragmentSpv,
                sizeof(shaders::kCopyRgbaFragmentSpv), s.fragment))
      return false;
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                 nullptr,
                 0,
                 VK_SHADER_STAGE_VERTEX_BIT,
                 s.vertex,
                 "main",
                 nullptr};
    stages[1] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                 nullptr,
                 0,
                 VK_SHADER_STAGE_FRAGMENT_BIT,
                 s.fragment,
                 "main",
                 nullptr};
    VkPipelineVertexInputStateCreateInfo vi{
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo ia{
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkViewport viewport{0, 0, float(s.width), float(s.height), 0, 1};
    VkRect2D scissor{{0, 0}, {s.width, s.height}};
    VkPipelineViewportStateCreateInfo vp{
        VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    vp.viewportCount = 1;
    vp.pViewports = &viewport;
    vp.scissorCount = 1;
    vp.pScissors = &scissor;
    VkPipelineRasterizationStateCreateInfo rs{
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1;
    VkPipelineMultisampleStateCreateInfo ms{
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState cba{};
    cba.colorWriteMask = 0xf;
    VkPipelineColorBlendStateCreateInfo cb{
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &cba;
    VkGraphicsPipelineCreateInfo gp{
        VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    gp.stageCount = 2;
    gp.pStages = stages;
    gp.pVertexInputState = &vi;
    gp.pInputAssemblyState = &ia;
    gp.pViewportState = &vp;
    gp.pRasterizationState = &rs;
    gp.pMultisampleState = &ms;
    gp.pColorBlendState = &cb;
    gp.layout = s.pipeline_layout;
    gp.renderPass = s.render_pass;
    return vkCreateGraphicsPipelines(s.device, VK_NULL_HANDLE, 1, &gp, nullptr,
                                     &s.pipeline) == VK_SUCCESS;
  }
  static bool Create(void *p, uint32_t, const UnityVulkanDeviceContext &c,
                     const SlotContract &contract,
                     const AhbSelection &selection,
                     UnityVulkanSlotCache &out) noexcept {
    auto slot = std::make_unique<AndroidSlot>();
    slot->device_live=p ? &static_cast<AndroidProducer*>(p)->device_live_ : nullptr;
    slot->device = D(c);
    slot->width = contract.width;
    slot->height = contract.height;
    const auto it = std::find_if(
        selection.candidates.begin(), selection.candidates.end(),
        [&](const AhbCandidate &x) { return x.path == selection.path; });
    if (it == selection.candidates.end() || !it->producer.vk_format)
      return false;
    slot->format = static_cast<VkFormat>(it->producer.vk_format);
    AHardwareBuffer_Desc desc{};
    desc.width = contract.width;
    desc.height = contract.height;
    desc.layers = 1;
    desc.format = contract.actual_format;
    desc.usage = contract.actual_usage;
    if (AHardwareBuffer_allocate(&desc, &slot->ahb) != 0)
      return false;
    // The allocation and Unity importer own these two references. The ncnn
    // importer retains its reference independently during control warm-up.
    AHardwareBuffer_acquire(slot->ahb);
    AHardwareBuffer_Desc actual{};
    AHardwareBuffer_describe(slot->ahb, &actual);
    if (actual.width != contract.width || actual.height != contract.height ||
        actual.layers != 1 || actual.format != contract.actual_format ||
        actual.usage != contract.actual_usage ||
        actual.stride < actual.width)
      return false;
    VkExternalMemoryImageCreateInfo ext{
        VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
    ext.handleTypes =
        VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, &ext};
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = slot->format;
    image.extent = {contract.width, contract.height, 1};
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = it->producer.image_usage;
    image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(slot->device, &image, nullptr, &slot->image) !=
        VK_SUCCESS)
      return false;
    const auto get_ahb_properties =
        reinterpret_cast<PFN_vkGetAndroidHardwareBufferPropertiesANDROID>(
            vkGetDeviceProcAddr(slot->device,
                                "vkGetAndroidHardwareBufferPropertiesANDROID"));
    VkAndroidHardwareBufferFormatPropertiesANDROID format_props{
        VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID};
    VkAndroidHardwareBufferPropertiesANDROID props{
        VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID, &format_props};
    if (!get_ahb_properties ||
        get_ahb_properties(slot->device, slot->ahb, &props) != VK_SUCCESS)
      return false;
    // A concrete VkFormat image is gated by B2's concrete-format/tiling query.
    // AHB formatFeatures describes the external-format path and is retained by
    // the measured candidate for diagnostics; it is not intersected here.
    if (!props.allocationSize || !props.memoryTypeBits ||
        format_props.format != slot->format)
      return false;
    uint32_t type = MemoryType(P(c), props.memoryTypeBits);
    if (type == UINT32_MAX)
      return false;
    VkImportAndroidHardwareBufferInfoANDROID imp{
        VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID, nullptr,
        slot->ahb};
    VkMemoryDedicatedAllocateInfo dedicated{
        VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO, &imp, slot->image,
        VK_NULL_HANDLE};
    VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, &dedicated,
                               props.allocationSize, type};
    if (vkAllocateMemory(slot->device, &alloc, nullptr, &slot->memory) !=
            VK_SUCCESS ||
        vkBindImageMemory(slot->device, slot->image, slot->memory, 0) !=
            VK_SUCCESS)
      return false;
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    view.image = slot->image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = slot->format;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    if (vkCreateImageView(slot->device, &view, nullptr, &slot->view) !=
        VK_SUCCESS)
      return false;
    VkCommandPoolCreateInfo pool{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = c.graphics_queue_family;
    if (vkCreateCommandPool(slot->device, &pool, nullptr,
                            &slot->command_pool) != VK_SUCCESS)
      return false;
    VkCommandBufferAllocateInfo ca{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    ca.commandPool = slot->command_pool;
    ca.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    ca.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(slot->device, &ca, &slot->command) !=
        VK_SUCCESS)
      return false;
    VkExportSemaphoreCreateInfo export_info{
        VK_STRUCTURE_TYPE_EXPORT_SEMAPHORE_CREATE_INFO};
    export_info.handleTypes = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
    VkSemaphoreCreateInfo sem{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
                              &export_info};
    if (vkCreateSemaphore(slot->device, &sem, nullptr, &slot->semaphore) !=
        VK_SUCCESS)
      return false;
    VkFenceCreateInfo fence{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    if (vkCreateFence(slot->device, &fence, nullptr, &slot->submission_fence) != VK_SUCCESS)
      return false;
    if (selection.path == HV_ANDROID_GPU_COPY_COLOR_ATTACHMENT &&
        !CreateColor(*slot))
      return false;
    out.ahb = reinterpret_cast<uintptr_t>(slot.get());
    out.ahb_buffer = reinterpret_cast<uintptr_t>(slot->ahb);
    out.image = reinterpret_cast<uintptr_t>(slot->image);
    out.memory = reinterpret_cast<uintptr_t>(slot->memory);
    out.image_view = reinterpret_cast<uintptr_t>(slot->view);
    out.command_buffer = reinterpret_cast<uintptr_t>(slot->command);
    out.export_semaphore = reinterpret_cast<uintptr_t>(slot->semaphore);
    out.sampler = reinterpret_cast<uintptr_t>(slot->sampler);
    out.descriptor_set_layout =
        reinterpret_cast<uintptr_t>(slot->descriptor_layout);
    out.descriptor_pool = reinterpret_cast<uintptr_t>(slot->descriptor_pool);
    out.descriptor_set = reinterpret_cast<uintptr_t>(slot->descriptor);
    out.render_pass = reinterpret_cast<uintptr_t>(slot->render_pass);
    out.framebuffer = reinterpret_cast<uintptr_t>(slot->framebuffer);
    out.pipeline_layout = reinterpret_cast<uintptr_t>(slot->pipeline_layout);
    out.pipeline = reinterpret_cast<uintptr_t>(slot->pipeline);
    slot.release();
    return true;
  }
  static void Drain(void *, uint32_t, AhbSlotState, UnityVulkanSlotCache &cache,
                    SyncFd &fd) noexcept {
    fd.Reset();
    std::unique_ptr<AndroidSlot> s(S(cache));
    if (!s)
      return;
    // Queue admission and callbacks have already been joined by the control
    // shutdown. Only this plugin's submitted fence is ours to wait on; Unity's
    // other queues are not externally synchronized by this bridge.
    if (s->submitted && s->Alive())
      vkWaitForFences(s->device, 1, &s->submission_fence, VK_TRUE, UINT64_MAX);
    cache = {};
  }
  static bool Access(void *p, void *texture, UnityTextureAccess &out) noexcept {
    auto &self = *static_cast<AndroidProducer *>(p);
    if (!self.vulkan_ || !self.device_live_.load(std::memory_order_acquire))
      return false;
    UnityVulkanImage observed{};
    if (!self.vulkan_->AccessTexture(
            texture, UnityVulkanWholeImage, VK_IMAGE_LAYOUT_GENERAL,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, 0,
            kUnityVulkanResourceAccess_ObserveOnly, &observed))
      return false;
    const VkImageLayout prior = observed.layout;
    out.image = reinterpret_cast<uintptr_t>(observed.image);
    out.texture = texture;
    out.layout = BridgeImageLayout::SourceCurrent;
    out.native_layout = static_cast<uint32_t>(prior);
    out.native_stage = static_cast<uint32_t>(VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    out.native_access = static_cast<uint32_t>(VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT);
    out.format = static_cast<uint32_t>(observed.format);
    out.width = observed.extent.width; out.height = observed.extent.height;
    out.usage = observed.usage; out.samples = observed.samples; out.image_type = observed.type;
    out.tiling = observed.tiling; out.layers = static_cast<uint32_t>(observed.layers);
    return true;
  }
  static SourcePreparation PrepareSource(
      void *p, const UnityVulkanSlotCache &cache,
      const UnityTextureAccess &access) noexcept {
    auto *self = static_cast<AndroidProducer *>(p);
    auto *slot = S(cache);
    if (!slot || !slot->Alive())
      return SourcePreparation::Unsupported;
    if (!self || self->source_lease_texture_.load(std::memory_order_acquire) != access.texture ||
        self->source_lease_generation_.load(std::memory_order_acquire) != self->bridge_.Generation())
      return SourcePreparation::Unsupported;
    uintptr_t expected_image = 0;
    if (!self->source_image_.compare_exchange_strong(expected_image, access.image,
                                                     std::memory_order_acq_rel) &&
        expected_image != access.image) {
      self->runtime_error_.store(2, std::memory_order_release);
      return SourcePreparation::Unsupported;
    }
    if (!slot->pipeline) {
      slot->active_source_view = VK_NULL_HANDLE;
      return SourcePreparation::Ready;
    }
    const auto image = reinterpret_cast<VkImage>(access.image);
    const auto format = static_cast<VkFormat>(access.format);
    std::unique_lock<std::mutex> source_lock(slot->source_mutex, std::try_to_lock);
    if (!source_lock.owns_lock()) return SourcePreparation::Warmed;
    for (auto &source : slot->source_views) {
      if (source.image == image && source.format == format && source.view) {
        slot->active_source_view = source.view;
        return SourcePreparation::Ready;
      }
    }
    for (auto &source : slot->source_views) if (!source.view) {
      if (!self) return SourcePreparation::Unsupported;
      std::unique_lock<std::mutex> pending(self->source_requests_mutex_, std::try_to_lock);
      if (!pending.owns_lock()) return SourcePreparation::Warmed;
      for (auto& request : self->source_requests_) if (request.slot == slot && request.image == image) return SourcePreparation::Warmed;
      for (auto& request : self->source_requests_) if (!request.slot) {
        request = {slot, image, format, self->bridge_.Generation(),
                   self->source_lease_token_.load(std::memory_order_acquire)};
        pending.unlock();
        self->source_requests_cv_.notify_one();
        return SourcePreparation::Warmed;
      }
      return SourcePreparation::Warmed;
    }
    if (self) self->runtime_error_.store(2, std::memory_order_release);
    return SourcePreparation::Unsupported;
  }
  // Unity has no ReleaseTexture operation. The queue command restores the exact
  // per-event prior layout/access; this callback only closes bridge bookkeeping.
  static void Release(void *, void *, const UnityTextureAccess &) noexcept {}
  static void UNITY_INTERFACE_API QueueCallback(int, void *data) noexcept {
    UnityVulkanBridge::QueueEvent(data);
    auto &self = Get();
    { std::lock_guard<std::mutex> lock(self.queue_mutex_);
      self.queued_callbacks_.fetch_sub(1, std::memory_order_acq_rel); }
    self.queue_cv_.notify_all();
  }
  static bool QueueAccess(void *p, void (*callback)(void *) noexcept,
                          void *data) noexcept {
    auto &self = *static_cast<AndroidProducer *>(p);
    {
      std::lock_guard<std::mutex> lock(self.queue_mutex_);
      if (!self.vulkan_ || self.queue_closed_ ||
          callback != &UnityVulkanBridge::QueueEvent)
        return false;
      self.queued_callbacks_.fetch_add(1, std::memory_order_acq_rel);
    }
    // AccessQueue may invoke the callback synchronously. Do not hold the drain
    // mutex across the Unity call; the admitted callback count prevents
    // teardown from observing a false zero in either schedule.
    self.vulkan_->AccessQueue(&QueueCallback, 0, data, true);
    return true;
  }
  static bool Begin(AndroidSlot &s, const UnityTextureAccess& access,
                    const BridgeBarrier *b, uint32_t n) noexcept {
    if (!s.Alive() || n != 4 || vkResetCommandBuffer(s.command, 0) != VK_SUCCESS)
      return false;
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (vkBeginCommandBuffer(s.command, &bi) != VK_SUCCESS)
      return false;
    for (uint32_t i = 0; i < 2; ++i) {
      auto v = Barrier(b[i]);
      if (i == 0) {
        v.oldLayout = static_cast<VkImageLayout>(access.native_layout);
        v.srcAccessMask = static_cast<VkAccessFlags>(access.native_access);
      }
      if (i == 1 && !s.destination_initialized)
        v.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
      vkCmdPipelineBarrier(s.command,
                           i == 0 ? static_cast<VkPipelineStageFlags>(
                                        access.native_stage)
                                  : Stage(b[i].old_layout),
                           Stage(b[i].new_layout), 0, 0, nullptr, 0, nullptr, 1,
                           &v);
    }
    return true;
  }
  static void EndBarriers(AndroidSlot &s, const UnityTextureAccess& access,
                          const BridgeBarrier *b) noexcept {
    for (uint32_t i = 2; i < 4; ++i) {
      auto v = Barrier(b[i]);
      if (i == 3) {
        v.newLayout = static_cast<VkImageLayout>(access.native_layout);
        v.dstAccessMask = static_cast<VkAccessFlags>(access.native_access);
      }
      vkCmdPipelineBarrier(s.command, Stage(b[i].old_layout),
                           i == 3 ? static_cast<VkPipelineStageFlags>(access.native_stage) : Stage(b[i].new_layout), 0, 0, nullptr, 0, nullptr, 1,
                           &v);
    }
  }
  static bool Blit(void *, const UnityVulkanSlotCache &c,
                   const UnityTextureAccess &access, const BridgeBarrier *b,
                   uint32_t n) noexcept {
    auto *s = S(c);
    if (!s || !Begin(*s, access, b, n))
      return false;
    VkImageBlit region{};
    region.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.srcOffsets[1] = {int32_t(access.width), int32_t(access.height), 1};
    region.dstSubresource = region.srcSubresource;
    region.dstOffsets[1] = {int32_t(s->width), int32_t(s->height), 1};
    vkCmdBlitImage(s->command, reinterpret_cast<VkImage>(b[0].image),
                   VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, s->image,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region,
                   VK_FILTER_LINEAR);
    EndBarriers(*s, access, b);
    return vkEndCommandBuffer(s->command) == VK_SUCCESS;
  }
  static bool Color(void *p, const UnityVulkanSlotCache &c,
                    const UnityTextureAccess &access, const BridgeBarrier *b,
                    uint32_t n, bool gpu_shader_conversion) noexcept {
    (void)p;
    auto *s = S(c);
    if (!s || !gpu_shader_conversion || !Begin(*s, access, b, n))
      return false;
    if (!s->active_source_view)
      return false;
    VkDescriptorImageInfo di{s->sampler, s->active_source_view,
                             VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet write{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    write.dstSet = s->descriptor;
    write.dstBinding = 0;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &di;
    vkUpdateDescriptorSets(s->device, 1, &write, 0, nullptr);
    VkRenderPassBeginInfo rp{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rp.renderPass = s->render_pass;
    rp.framebuffer = s->framebuffer;
    rp.renderArea.extent = {s->width, s->height};
    vkCmdBeginRenderPass(s->command, &rp, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(s->command, VK_PIPELINE_BIND_POINT_GRAPHICS, s->pipeline);
    vkCmdBindDescriptorSets(s->command, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            s->pipeline_layout, 0, 1, &s->descriptor, 0,
                            nullptr);
    // Typed BGRA sampling already yields semantic RGBA components.
    uint32_t swap = 0u;
    vkCmdPushConstants(s->command, s->pipeline_layout,
                       VK_SHADER_STAGE_FRAGMENT_BIT, 0, 4, &swap);
    vkCmdDraw(s->command, 3, 1, 0, 0);
    vkCmdEndRenderPass(s->command);
    EndBarriers(*s, access, b);
    return vkEndCommandBuffer(s->command) == VK_SUCCESS;
  }
  static bool Submit(void *, const UnityVulkanDeviceContext &dc,
                     const UnityVulkanSlotCache &c) noexcept {
    auto *s = S(c);
    if(!s || !s->Alive())return false;
    if (vkResetFences(s->device, 1, &s->submission_fence) != VK_SUCCESS) return false;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &s->command;
    si.signalSemaphoreCount = 1;
    si.pSignalSemaphores = &s->semaphore;
    const bool ok = vkQueueSubmit(Q(dc), 1, &si, s->submission_fence) == VK_SUCCESS;
    if (ok) {
      s->destination_initialized = true;
      s->submitted = true;
      s->export_pending = true;
    }
    return ok;
  }
  static bool Export(void *, const UnityVulkanDeviceContext &dc,
                     const UnityVulkanSlotCache &c, SyncFd &out) noexcept {
    auto *s = S(c);
    if(!s || !s->Alive())return false;
    auto fn = reinterpret_cast<PFN_vkGetSemaphoreFdKHR>(
        vkGetDeviceProcAddr(D(dc), "vkGetSemaphoreFdKHR"));
    if (!fn)
      return false;
    VkSemaphoreGetFdInfoKHR info{VK_STRUCTURE_TYPE_SEMAPHORE_GET_FD_INFO_KHR};
    info.semaphore = s->semaphore;
    info.handleType = VK_EXTERNAL_SEMAPHORE_HANDLE_TYPE_SYNC_FD_BIT;
    int fd = -2;
    if (fn(D(dc), &info, &fd) != VK_SUCCESS)
      return false;
    s->export_pending = false;
    out = SyncFd(
        fd,
        [](void *, int owned) noexcept {
          if (owned >= 0)
            ::close(owned);
        },
        nullptr);
    return out.HasPayload();
  }
  static bool Complete(void *p, const UnityVulkanDeviceContext &dc,
                       const UnityVulkanSlotCache &c) noexcept {
    auto* s = S(c);
    if (!s || !s->Alive() || vkGetFenceStatus(s->device, s->submission_fence) != VK_SUCCESS)
      return false;
    // A completed queue signal still leaves the binary semaphore signaled if
    // export failed. Retry the payload transfer and close the discarded fd;
    // never recycle it for another signal until that transfer succeeds.
    if (s->export_pending) {
      SyncFd discarded;
      if (!Export(p, dc, c, discarded)) return false;
    }
    return true;
  }
  static void Cancel(void *p) noexcept {
    auto &self = *static_cast<AndroidProducer *>(p);
    std::unique_lock<std::mutex> lock(self.queue_mutex_);
    self.queue_closed_ = true;
    self.queue_cv_.wait(lock, [&] {
      return self.queued_callbacks_.load(std::memory_order_acquire) == 0;
    });
  }

  IUnityInterfaces *interfaces_ = nullptr;
  IUnityGraphics *graphics_ = nullptr;
  IUnityGraphicsVulkanV2 *vulkan_ = nullptr;
  UnityVulkanDeviceContext device_context_{};
  bool have_device_ = false;
  std::atomic<bool> device_live_{false};
  std::string diagnostic_ = "Unity Vulkan device is not initialized";
  mutable std::mutex control_;
  std::mutex access_;
  VkImageLayout prior_layout_ = VK_IMAGE_LAYOUT_GENERAL;
  VkFormat source_format_ = VK_FORMAT_UNDEFINED;
  VkExtent3D source_extent_{};
  std::atomic<uint32_t> queued_callbacks_{0};
  std::mutex queue_mutex_;
  std::condition_variable queue_cv_;
  bool queue_closed_ = true;
  bool interception_installed_ = false;
  std::atomic<bool> required_extensions_enabled_{false};
  std::atomic<uintptr_t> intercepted_physical_device_{0};
  std::atomic<uintptr_t> intercepted_device_{0};
  std::atomic<uintptr_t> created_instance_{0};
  std::atomic<uintptr_t> intercept_instance_{0};
  uint32_t instance_api_version_ = 0;
  bool instance_properties2_ = false;
  bool instance_external_memory_ = false;
  bool instance_external_semaphore_ = false;
  std::atomic<uint32_t> runtime_error_{0};
  PFN_vkGetInstanceProcAddr next_gipa_ = nullptr;
  UnityVulkanBridge bridge_;
  std::mutex source_requests_mutex_;
  std::condition_variable source_requests_cv_;
  std::array<SourceRequest, 3> source_requests_{};
  bool stop_source_worker_ = false;
  std::thread source_worker_;
  std::atomic<void*> source_lease_texture_{nullptr};
  std::atomic<uint64_t> source_lease_generation_{0};
  std::atomic<uint64_t> source_lease_token_{0};
  std::atomic<uintptr_t> source_image_{0};
  static_assert(sizeof(void*) >= sizeof(uint64_t), "Android GPU event tokens require a 64-bit player");
  HV_AndroidGpuSubmissionV1 configuration_event_submission_{};
  std::atomic<uint64_t> configuration_event_token_{0};
  HV_AndroidGpuSubmissionV1 measured_submission_{};
  uint64_t measured_lease_token_ = 0;
#if defined(HV_ANDROID_GPU_GATE)
  uint64_t gate_probe_token_ = 0;
#endif
  VulkanSourceImage measured_source_{};
  VulkanDeviceContext producer_context_{};
  bool measured_ncnn_lease_ = false;
  std::atomic<bool> configuration_requested_{false};
  std::atomic<bool> configuration_event_inflight_{false};
  std::atomic<bool> configuration_ready_{false};
  std::atomic<bool> configuration_failed_{false};
  std::atomic<uint64_t> configuration_pending_drops_{0};
#if defined(HV_ANDROID_ADAPTER_TEST)
  std::mutex test_worker_mutex_;
  std::condition_variable test_worker_cv_;
  bool test_pause_source_worker_ = false;
#endif
};
} // namespace

bool ConfigureUnityVulkanProducer(const AhbSelection &s,
                                  const SlotContract &c) noexcept {
  return AndroidProducer::Get().Configure(s, c);
}
bool BeginUnityVulkanSourceLease(void *texture) noexcept {
  return AndroidProducer::Get().BeginSourceLease(texture);
}
void EndUnityVulkanSourceLease() noexcept {
  AndroidProducer::Get().EndSourceLease();
}
bool UnityVulkanSourceRequiresRetention() noexcept {
  return AndroidProducer::Get().SourceRequiresRetention();
}
void ShutdownUnityVulkanProducer() noexcept {
  AndroidProducer::Get().Shutdown();
}
BridgeResult PrepareUnityVulkanFrame(const HV_AndroidGpuSubmissionV1 &s,
                                     void **out) noexcept {
  return AndroidProducer::Get().Prepare(s, out);
}
void GetUnityVulkanProducerStatus(HV_AndroidGpuBridgeStatusV1 &s) noexcept {
  AndroidProducer::Get().Status(s);
}
void *UnityVulkanRenderEventFunction() noexcept {
  return reinterpret_cast<void *>(&AndroidProducer::RenderEvent);
}
const char *UnityVulkanProducerDiagnostic() noexcept {
  return AndroidProducer::Get().Diagnostic();
}
#if defined(HV_ANDROID_GPU_GATE)
UnityVulkanBridge* UnityVulkanProducerBridge() noexcept {
  return AndroidProducer::Get().GateBridge();
}
bool UnityVulkanProducerContext(VulkanDeviceContext& out) noexcept {
  return AndroidProducer::Get().GateContext(out);
}
const char* UnityVulkanProducerGateError() noexcept {
  return AndroidProducer::Get().GateError();
}
const char* UnityVulkanProducerGateProbe() noexcept {
  return AndroidProducer::Get().GateProbe();
}
#endif
} // namespace humanvision::gpu

extern "C" UNITY_INTERFACE_EXPORT void UNITY_INTERFACE_API
UnityPluginLoad(IUnityInterfaces *interfaces) {
  humanvision::gpu::AndroidProducer::Get().Load(interfaces);
}
extern "C" UNITY_INTERFACE_EXPORT void UNITY_INTERFACE_API UnityPluginUnload() {
  humanvision::gpu::AndroidProducer::Get().Unload();
}
#if defined(HV_ANDROID_GPU_GATE)
// Gate-only additive diagnostic. Never blocks or consumes/reset the terminal
// latch; the managed owner must query again after GateEnd joins its worker.
extern "C" UNITY_INTERFACE_EXPORT uint32_t HV_CALL HV_AndroidGpuGateMustRetainSource() {
  return humanvision::gpu::UnityVulkanSourceRequiresRetention() ? 1u : 0u;
}
#endif

#else
namespace humanvision::gpu {
bool ConfigureUnityVulkanProducer(const AhbSelection &,
                                  const SlotContract &) noexcept {
  return false;
}
bool BeginUnityVulkanSourceLease(void *) noexcept { return false; }
void EndUnityVulkanSourceLease() noexcept {}
bool UnityVulkanSourceRequiresRetention() noexcept { return false; }
void ShutdownUnityVulkanProducer() noexcept {}
BridgeResult PrepareUnityVulkanFrame(const HV_AndroidGpuSubmissionV1 &,
                                     void **out) noexcept {
  if (out)
    *out = nullptr;
  return BridgeResult::Closed;
}
void GetUnityVulkanProducerStatus(HV_AndroidGpuBridgeStatusV1 &) noexcept {}
void *UnityVulkanRenderEventFunction() noexcept { return nullptr; }
const char *UnityVulkanProducerDiagnostic() noexcept {
  return "Android Vulkan producer bridge is unavailable in this build";
}
} // namespace humanvision::gpu
#endif
