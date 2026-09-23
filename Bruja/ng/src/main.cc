// ng call <module.Interface.Method>
// ng fire <key> [name] [args]
// ng fire-name <module.Interface.Method> expected|voodoo [args]
#include "ng/module.h"

#include <cstdlib>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
  std::string root = "C:\\Users\\grego\\Bruja\\third_party\\chromium-src";
  if (const char* env = std::getenv("MOJOVM_MOJOM")) root = env;

  std::string cmd;
  std::string a;
  std::string b;
  std::string c;
  for (int i = 1; i < argc; ++i) {
    const std::string s = argv[i];
    if (s == "--root" && i + 1 < argc) {
      root = argv[++i];
    } else if (cmd.empty()) {
      cmd = s;
    } else if (a.empty()) {
      a = s;
    } else if (b.empty()) {
      b = s;
    } else if (c.empty()) {
      c = s;
    }
  }
  if (cmd.empty()) {
    std::cerr << "usage: ng call <module.Interface.Method>\n"
              << "       ng fire <key> [name] [args]\n"
              << "       ng fire-name <module.Interface.Method> expected|voodoo [args]\n";
    return 2;
  }
  ng::Module mod(root);
  std::string reply;
  if (cmd == "call") {
    reply = mod.Call(a, b);
  } else if (cmd == "fire") {
    reply = mod.Fire(std::stoull(a), b, c);
  } else if (cmd == "fire-name") {
    const bool expected = b != "voodoo";
    reply = mod.FireName(a, c, expected);
  } else {
    std::cerr << "unknown command " << cmd << "\n";
    return 2;
  }
  std::cout << reply << "\n";
  return reply.rfind("error:", 0) == 0 ? 1 : 0;
}
