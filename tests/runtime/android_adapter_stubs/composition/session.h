#pragma once
#include <string>

namespace humanvision::runtime {
// Only the error-reporting boundary is substituted. Android ABI validation,
// result mapping, status and producer calls compile from production sources.
class RuntimeSession {
public:
  void ReportError(const char* message) { error=message; }
  std::string error;
};
}
