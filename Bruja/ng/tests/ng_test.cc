#include "ng/module.h"

#include <cstdlib>
#include <iostream>
#include <string>

static int fails = 0;

static void Expect(const std::string& got, const std::string& want, const char* what) {
  if (got == want) return;
  std::cerr << what << "\n  got  " << got << "\n  want " << want << "\n";
  ++fails;
}

int main() {
  const char* root = std::getenv("MOJOVM_MOJOM");
  if (!root) root = "C:\\Users\\grego\\Bruja\\third_party\\chromium-src";
  ng::Module mod(root);
  const std::string sep(1, static_cast<char>(0x1f));
  const std::string api = "extensions.mojom.MessagePort.DispatchDisconnect";

  Expect(mod.Call(api, ""), "ok" + sep + "1481416133" + sep + "2592569", "call");
  Expect(mod.FireName(api, "", true), "ok" + sep + "2592569" + sep + "1" + sep + "ok",
         "expected");
  Expect(mod.FireName(api, "", false), "ok" + sep + "1481416133" + sep + "2" + sep + "ok",
         "voodoo");
  Expect(mod.Fire(ng::kSecrpc, "SECRPC", ""),
         "ok" + sep + "91556947316803" + sep + "3" + sep + "ok", "secrpc");
  Expect(mod.Fire(ng::kRunMessageId, "Run", ""),
         "ok" + sep + "4294967295" + sep + "4" + sep + "3", "run");

  if (mod.log().size() != 4) {
    std::cerr << "log size\n";
    ++fails;
  } else {
    for (size_t i = 0; i < mod.log().size(); ++i) {
      const ng::Fired& row = mod.log()[i];
      if (!row.proved || row.request_id != i + 1) {
        std::cerr << "unproved request " << row.request_id << "\n";
        ++fails;
      }
    }
    if (mod.log()[2].key != ng::kSecrpc || mod.log()[3].reply != "3") {
      std::cerr << "reply mismatch\n";
      ++fails;
    }
  }
  if (fails) {
    std::cerr << "ng_test " << fails << " failed\n";
    return 1;
  }
  std::cout << "ng_test ok\n";
  return 0;
}
