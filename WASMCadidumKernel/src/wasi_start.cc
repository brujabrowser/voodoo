// WASI reactor for wck (WASMCadidumKernel). Linked --export-all --no-entry.
// Constructors run via _initialize (WASI reactor ABI).

extern "C" {

void __wasm_call_ctors(void) __attribute__((weak));

__attribute__((export_name("_initialize"))) void _initialize(void) {
  if (__wasm_call_ctors) {
    __wasm_call_ctors();
  }
}

}  // extern "C"
