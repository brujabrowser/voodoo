#include "test.h"

#include "mojo/public/c/system/core.h"

#include <cstdio>

int g_failures = 0;

int main() {
  MojoInitialize(nullptr);
  int ran = 0;
  for (const auto& t : Tests()) {
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
