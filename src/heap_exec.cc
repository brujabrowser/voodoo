// Name each method on CHPT, its type record on TPT, and the callee on EPT.
// Then resolve all three and call. The line written is that call's result.
#include "mojo/public/cpp/bindings/lib/type_intern.h"
#include "src/sandbox/external-pointer-table.h"

#include <cstdint>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace {

struct Slot {
  uint32_t seq = 0;
  uint32_t address = 0;
  std::string proto;
  std::string out;
};

void Run(Slot* slot) {
  slot->out = "ran " + std::to_string(slot->seq) + " " + slot->proto +
              " address=" + std::to_string(slot->address);
}

using Fn = void (*)(Slot*);

std::string Execute(v8::internal::ExternalPointerTable* ept, uint32_t seq,
                    uint32_t address, const std::string& proto) {
  auto* key = new std::string("interface:" + proto);
  const void* type_key = key->c_str();
  mojo::internal::InternType(type_key);
  const mojo::internal::InternedTypeRec* rec =
      mojo::internal::InternedType(type_key);
  v8::CppHeapPointerTag tag = mojo::internal::TypeTag(type_key);

  Slot slot;
  slot.seq = seq;
  slot.address = address;
  slot.proto = proto;
  v8::CppHeapPointerHandle chpt =
      mojo::internal::TypeHeap().AllocateAndInitializeEntry(&slot, tag);

  const auto etag = v8::internal::ExternalPointerTag::kFirstManagedResourceTag;
  v8::internal::ExternalPointerHandle eph =
      ept->AllocateAndInitializeEntry(reinterpret_cast<void*>(&Run), etag);

  void* obj = mojo::internal::TypeHeap().Get(chpt, tag);
  void* raw = ept->Get(eph, etag);
  Fn fn = reinterpret_cast<Fn>(raw);
  if (obj == nullptr || fn != &Run || rec == nullptr ||
      rec->type_key != type_key) {
    mojo::internal::TypeHeap().FreeEntry(chpt);
    ept->FreeEntry(eph);
    return "miss";
  }
  fn(static_cast<Slot*>(obj));
  std::string out = slot.out;
  out += " chpt=" + std::to_string(chpt);
  out += " tpt=" + std::to_string(rec->intern_id);
  out += " ept=" + std::to_string(eph);
  mojo::internal::TypeHeap().FreeEntry(chpt);
  ept->FreeEntry(eph);
  return out;
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: heap_exec rows.tsv results.tsv\n";
    return 2;
  }
  std::ifstream in(argv[1]);
  if (!in) {
    std::cerr << "cannot read " << argv[1] << "\n";
    return 1;
  }
  std::ofstream out(argv[2], std::ios::binary);
  if (!out) {
    std::cerr << "cannot write " << argv[2] << "\n";
    return 1;
  }
  v8::internal::ExternalPointerTable ept;
  std::string line;
  uint32_t seq = 0;
  uint32_t ran = 0;
  while (std::getline(in, line)) {
    if (line.empty() || line[0] == '#') continue;
    std::string name;
    std::string method;
    std::string dagger;
    std::string voodoo;
    std::string mojo;
    std::istringstream row(line);
    if (!std::getline(row, name, '\t') || !std::getline(row, method, '\t') ||
        !std::getline(row, dagger, '\t') || !std::getline(row, voodoo, '\t') ||
        !std::getline(row, mojo, '\t')) {
      std::cerr << "bad row\n";
      return 1;
    }
    ++seq;
    const uint32_t address = static_cast<uint32_t>(std::stoul(voodoo));
    const std::string result = Execute(&ept, seq, address, name);
    if (result.rfind("ran ", 0) == 0) ++ran;
    out << name << '\t' << result << '\n';
    if (seq % 1000 == 0) {
      std::cerr << "heap " << seq << "\n";
    }
  }
  std::cerr << "heap rows " << seq << " ran " << ran << "\n";
  return ran == seq && seq > 0 ? 0 : 1;
}
