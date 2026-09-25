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

static bool CheckSlice(const std::string& ir) {
  return ir.find("interface WebPage ") != std::string::npos &&
         ir.find("interface WebPageProxy ") != std::string::npos &&
         ir.find("setShouldSuppressHDR(") != std::string::npos &&
         ir.find("runJavaScriptInFrameInScriptWorld(") != std::string::npos &&
         ir.find("showShareSheet(") != std::string::npos &&
         ir.find("showDateTimePicker(") != std::string::npos &&
         ir.find("EnabledBy") == std::string::npos &&
         ir.find("WantsAsyncDispatchMessage") == std::string::npos;
}

static int ParsePath(const char* path, bool slice) {
  std::string source = ReadFile(path);
  if (source.empty()) {
    std::fprintf(stderr, "messages_in_frontend_test: cannot read %s\n", path);
    return 1;
  }
  bruja::InputKind kind = bruja::InferInputKind(path, source);
  if (kind != bruja::InputKind::kMessagesIn) {
    std::fprintf(stderr, "messages_in_frontend_test: expected messages.in kind\n");
    return 1;
  }
  std::string ir = bruja::ToBrujaSource(kind, source);
  if (slice && !CheckSlice(ir)) {
    std::fprintf(stderr, "messages_in_frontend_test FAIL ir=\n%s\n", ir.c_str());
    return 1;
  }
  if (ir.find("interface ") == std::string::npos) {
    std::fprintf(stderr, "messages_in_frontend_test FAIL no interface in %s\n",
                 path);
    return 1;
  }
  std::printf("messages_in_frontend_test: OK %s\n", path);
  return 0;
}

int main(int argc, char** argv) {
  const char* path = argc > 1 ? argv[1]
                              : "tests/fixtures/WebPage.slice.messages.in";
  if (ParsePath(path, argc <= 2) != 0) return 1;
  for (int i = 2; i < argc; ++i) {
    if (ParsePath(argv[i], false) != 0) return 1;
  }
  return 0;
}
