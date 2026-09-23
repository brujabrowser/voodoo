// mojovm call <module.Interface.Method> [arg]
// Same two-string shape as gocvm.Call. The .mojom is parsed on that call.
#include "mojovm.h"

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
  std::string root = "C:\\Users\\grego\\Bruja\\third_party\\chromium-src";
  if (const char* env = std::getenv("MOJOVM_MOJOM")) root = env;

  std::string api;
  std::string arg;
  std::string out_path =
      "C:\\Users\\grego\\Bruja\\out\\mojovm-portfolio.txt";
  bool portfolio = false;
  for (int i = 1; i < argc; ++i) {
    const std::string a = argv[i];
    if (a == "--root" && i + 1 < argc) {
      root = argv[++i];
    } else if (a == "--out" && i + 1 < argc) {
      out_path = argv[++i];
    } else if (a == "call") {
      continue;
    } else if (a == "portfolio") {
      portfolio = true;
    } else if (api.empty()) {
      api = a;
    } else if (arg.empty()) {
      arg = a;
    }
  }
  mojovm::Vm vm(root);
  if (portfolio) {
    const std::string reply = vm.Portfolio(out_path);
    std::cout << reply << "\n" << out_path << "\n";
    return reply.rfind("error:", 0) == 0 ? 1 : 0;
  }
  if (api.empty()) {
    std::cerr << "usage: mojovm call <module.Interface.Method> [arg]\n"
              << "       mojovm portfolio [--out file]\n";
    return 2;
  }
  const std::string reply = vm.Call(api, arg);
  std::cout << reply << "\n";
  return reply.rfind("error:", 0) == 0 ? 1 : 0;
}
