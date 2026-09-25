#include "frontends.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

static std::string ReadFile(const char* path) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return {};
  std::ostringstream ss;
  ss << in.rdbuf();
  return ss.str();
}

int main(int argc, char** argv) {
  const char* path = argc > 1 ? argv[1]
                              : "tests/fixtures/WKWebView.slice.h";
  std::string source = ReadFile(path);
  if (source.empty()) {
    std::fprintf(stderr, "objc_frontend_test: cannot read %s\n", path);
    return 1;
  }
  bruja::InputKind kind = bruja::InferInputKind(path, source);
  if (kind != bruja::InputKind::kCocoaMm) {
    std::fprintf(stderr, "objc_frontend_test: expected cocoa/objc kind\n");
    return 1;
  }
  std::string ir = bruja::ToBrujaSource(kind, source);
  bool ok = ir.find("interface WKWebView") != std::string::npos &&
            ir.find("readonly attribute DOMString? title") != std::string::npos &&
            ir.find("loadHTMLString(") != std::string::npos &&
            ir.find("Promise<any> evaluateJavaScript") != std::string::npos &&
            ir.find("interface WKUserContentController") != std::string::npos &&
            ir.find("addScriptMessageHandler(") != std::string::npos &&
            ir.find("UIView") == std::string::npos;
  if (!ok) {
    std::fprintf(stderr, "objc_frontend_test FAIL ir=\n%s\n", ir.c_str());
    return 1;
  }
  std::printf("objc_frontend_test: OK\n");
  return 0;
}
