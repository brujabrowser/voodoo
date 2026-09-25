import pathlib

portfolio = pathlib.Path(r"C:\Users\grego\WASMVoodooCompile\Bruja\out\mojovm-portfolio.txt")
inc = pathlib.Path(r"C:\Users\grego\WASMVoodooCompile\WASMHolePunch\src\js_bindings\runtime_bindings_gen.inc")

rows = []
seen = set()
for line in portfolio.read_text(encoding="utf-8").splitlines():
    parts = line.split("\t")
    name = parts[0]
    if not name or name.startswith("#") or "." not in name:
        continue
    iface, method = name.rsplit(".", 1)
    js = method
    if js and "A" <= js[0] <= "Z":
        js = chr(ord(js[0]) - ord("A") + ord("a")) + js[1:]
    key = (iface, js)
    if key in seen:
        js = method
        key = (iface, js)
    if key in seen:
        continue
    seen.add(key)
    rows.append((iface, js, parts[2], name))

lines = ["// Runtime bindings from the mojom portfolio. JS names match GenerateJsHeader."]
for i, (iface, js, ordinal, full) in enumerate(rows):
    lines.append("void JsRt%d(const FunctionCallbackInfo<Value>& info) {" % i)
    lines.append("  info.GetReturnValue().Set(%s.0);" % ordinal)
    lines.append("}")
chunk = 16
nchunks = (len(rows) + chunk - 1) // chunk
for n in range(nchunks):
    lines.append("void InstallRuntimeChunk%d(Isolate* isolate, Local<v8::Context> context, Local<Object> global) {" % n)
    for i in range(n * chunk, min(len(rows), (n + 1) * chunk)):
        iface, js, ordinal, full = rows[i]
        lines.append(
            '  InstallFn(isolate, context, GetOrCreateNamespace(isolate, global, "%s"), "%s", JsRt%d);'
            % (iface, js, i)
        )
    lines.append("}")
lines.append("void InstallRuntimeBindings(Isolate* isolate, Local<v8::Context> context, Local<Object> global) {")
for n in range(nchunks):
    lines.append("  InstallRuntimeChunk%d(isolate, context, global);" % n)
lines.append("}")
inc.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
print(len(rows))
