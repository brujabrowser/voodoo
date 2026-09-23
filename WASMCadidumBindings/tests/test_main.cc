#include "test.h"

#include "mojo/public/c/system/core.h"

#include <cstdio>
#include <cstring>

int g_failures = 0;

int main(int argc, char** argv) {
  std::setvbuf(stdout, nullptr, _IONBF, 0);
  std::setvbuf(stderr, nullptr, _IONBF, 0);
  const char* filter = argc > 1 ? argv[1] : nullptr;
  MojoInitialize(nullptr);
  int ran = 0;
  for (const auto& t : Tests()) {
    if (filter && !std::strstr(t.name, filter)) {
      continue;
    }
    std::printf("RUN  %s\n", t.name);
    const int before = g_failures;
    t.fn();
    if (g_failures == before) {
      std::printf("OK   %s\n", t.name);
    } else {
      std::printf("FAIL %s\n", t.name);
    }
    ++ran;
  }
  MojoShutdown(nullptr);
  std::printf("%d tests, %d failures\n", ran, g_failures);
  return g_failures ? 1 : 0;
}
