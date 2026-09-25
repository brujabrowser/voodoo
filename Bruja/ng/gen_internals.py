import pathlib

portfolio = pathlib.Path(r"C:\Users\grego\WASMVoodooCompile\Bruja\out\mojovm-portfolio.txt")
header = pathlib.Path(r"C:\Users\grego\WASMVoodooCompile\WASMHolePunch\include\whp\js_bindings\chrome_internals_table.h")
inc = pathlib.Path(r"C:\Users\grego\WASMVoodooCompile\WASMHolePunch\src\js_bindings\chrome_internals_gen.inc")

rows = []
for line in portfolio.read_text(encoding="utf-8").splitlines():
    parts = line.split("\t")
    name = parts[0]
    if "PageHandler" not in name:
        continue
    iface, method = name.rsplit(".", 1)
    rows.append((iface, method, parts[2]))

h = []
h.append("#ifndef WHP_JS_BINDINGS_CHROME_INTERNALS_TABLE_H_")
h.append("#define WHP_JS_BINDINGS_CHROME_INTERNALS_TABLE_H_")
h.append("")
h.append("namespace whp_js_bindings {")
h.append("")
h.append("struct ChromeInternalsRow {")
h.append("  const char* iface;")
h.append("  const char* method;")
h.append("  double ordinal;")
h.append("};")
h.append("")
h.append("inline constexpr ChromeInternalsRow kChromeInternals[] = {")
for iface, method, ordinal in rows:
    h.append('    {"%s", "%s", %s.0},' % (iface, method, ordinal))
h.append("};")
h.append("")
h.append("inline constexpr int kChromeInternalsCount =")
h.append("    static_cast<int>(sizeof(kChromeInternals) / sizeof(kChromeInternals[0]));")
h.append("")
h.append("}  // namespace whp_js_bindings")
h.append("")
h.append("#endif  // WHP_JS_BINDINGS_CHROME_INTERNALS_TABLE_H_")
header.write_text("\n".join(h) + "\n", encoding="utf-8", newline="\n")

c = ["// Generated from mojovm-portfolio.txt. Every PageHandler method."]
for i, (iface, method, ordinal) in enumerate(rows):
    c.append("void JsPH%d(const FunctionCallbackInfo<Value>& info) {" % i)
    c.append('  ReturnOrdinal(info, %s.0, "%s");' % (ordinal, iface))
    c.append("}")
chunk = 16
nchunks = (len(rows) + chunk - 1) // chunk
for n in range(nchunks):
    c.append("void InstallPageHandlers%d(Isolate* isolate, Local<v8::Context> context, Local<Object> global) {" % n)
    for i in range(n * chunk, min(len(rows), (n + 1) * chunk)):
        iface, method, ordinal = rows[i]
        c.append('  InstallFn(isolate, context, GetOrCreateNamespace(isolate, global, "%s"), "%s", JsPH%d);' % (iface, method, i))
    c.append("}")
c.append("void InstallAllPageHandlers(Isolate* isolate, Local<v8::Context> context, Local<Object> global) {")
for n in range(nchunks):
    c.append("  InstallPageHandlers%d(isolate, context, global);" % n)
c.append("}")
inc.write_text("\n".join(c) + "\n", encoding="utf-8", newline="\n")
print(len(rows))
