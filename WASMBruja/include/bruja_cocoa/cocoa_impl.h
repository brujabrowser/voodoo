#ifndef BRUJA_COCOA_COCOA_IMPL_H_
#define BRUJA_COCOA_COCOA_IMPL_H_

#include <cctype>
#include <string>

#include "cocoa_v8_gen.h"

namespace bruja_cocoa {

class NSStringExtrasImpl : public bruja_generated::NSStringExtras {
 public:
  bool HasPrefix(const std::string& string, const std::string& prefix) override {
    return string.size() >= prefix.size() &&
           string.compare(0, prefix.size(), prefix) == 0;
  }
  bool HasSuffix(const std::string& string, const std::string& suffix) override {
    return string.size() >= suffix.size() &&
           string.compare(string.size() - suffix.size(), suffix.size(), suffix) ==
               0;
  }
  std::string StringByAppendingString(const std::string& string,
                                      const std::string& other) override {
    return string + other;
  }
  std::string ConvertToASCIIUppercase(const std::string& string) override {
    std::string out = string;
    for (char& c : out)
      c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return out;
  }
  std::string ConvertToASCIILowercase(const std::string& string) override {
    std::string out = string;
    for (char& c : out)
      c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
  }
};

class NSURLExtrasImpl : public bruja_generated::NSURLExtras {
 public:
  bool IsValid(const std::string& spec) override {
    return spec.find("://") != std::string::npos;
  }
  std::string Protocol(const std::string& spec) override {
    auto pos = spec.find("://");
    return pos == std::string::npos ? std::string() : spec.substr(0, pos);
  }
  std::string Host(const std::string& spec) override {
    auto pos = spec.find("://");
    if (pos == std::string::npos) return {};
    size_t start = pos + 3;
    auto slash = spec.find('/', start);
    std::string hostPort = slash == std::string::npos
                               ? spec.substr(start)
                               : spec.substr(start, slash - start);
    auto colon = hostPort.find(':');
    return colon == std::string::npos ? hostPort : hostPort.substr(0, colon);
  }
  std::string UserVisibleString(const std::string& spec) override { return spec; }
};

}  // namespace bruja_cocoa

#endif  // BRUJA_COCOA_COCOA_IMPL_H_
