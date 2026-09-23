#include "mojovm.h"

#include <cstdlib>
#include <iostream>
#include <string>

static int fails = 0;

static void Expect(const std::string& got, const std::string& want,
                   const char* what) {
  if (got == want) return;
  std::cerr << what << "\n  got  " << got << "\n  want " << want << "\n";
  ++fails;
}

int main() {
  const char* root = std::getenv("MOJOVM_MOJOM");
  if (!root) root = "C:\\Users\\grego\\Bruja\\third_party\\chromium-src";
  mojovm::Vm vm(root);
  const std::string sep(1, static_cast<char>(0x1f));

  Expect(vm.Call("extensions.mojom.MessagePort.DispatchDisconnect", ""),
         "ok" + sep + "1481416133" + sep + "2592569", "DispatchDisconnect");
  Expect(vm.Call("extensions.mojom.MessagePort.DeliverMessage", ""),
         "ok" + sep + "703090134" + sep + "1122766509", "DeliverMessage");
  Expect(vm.Call("extensions.mojom.MessagePortHost.ClosePort", ""),
         "ok" + sep + "2146739443" + sep + "568318427", "ClosePort");
  Expect(vm.Call("extensions.mojom.MessagePortHost.PostMessage", ""),
         "ok" + sep + "1165969513" + sep + "1529189607", "PostMessage");
  Expect(vm.Call("extensions.mojom.MessagePortHost.ResponsePending", ""),
         "ok" + sep + "1981901972" + sep + "1848577363", "ResponsePending");
  Expect(vm.Call("extensions.mojom.MessagePort.NoSuch", ""),
         "error: unknown method NoSuch", "missing method");
  Expect(vm.Call("no.such.module.Iface.Method", ""),
         "error: unknown module no.such.module", "missing module");

  if (fails) {
    std::cerr << "mojovm_test " << fails << " failed\n";
    return 1;
  }
  std::cout << "mojovm_test ok\n";
  return 0;
}
