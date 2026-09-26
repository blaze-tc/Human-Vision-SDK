#pragma once
#include <string>
#include <cstdint>

namespace humanvision::runtime {
// Only the error-reporting boundary is substituted. Android ABI validation,
// result mapping, status and producer calls compile from production sources.
class RuntimeSession {
public:
  void ReportError(const char* message) { error=message; }
  void RecordGpuDimensions(uint32_t, uint32_t) noexcept {}
  void RecordGpuCaptureAttempt() noexcept {}
  void RecordGpuNoSlotDrop() noexcept {}
  void SetGpuSourceLeaseActive(bool) noexcept {}
  bool UsesGpuRoute() const noexcept { return true; }
  std::string error;
};
}
