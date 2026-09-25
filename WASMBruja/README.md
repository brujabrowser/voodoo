# WASMBruja

`brujac` reads a `.bruja` file (Web IDL), a WebKit `*.messages.in`, an Objective-C `@interface` header, or TypeScript, and writes one output file. The compiler runs on the host.

```
.bruja / .messages.in / .mm / .h  →  brujac  →  C++ header (quickjs binding)
                                             →  C++ header (V8 facade binding)
                                             →  Go++ source (.goxx)
.ts                               →  brujac  →  Go++ source (.goxx)
```

The C++ header is a pure-virtual interface plus a function that wraps a caller-owned implementation pointer in a JS object. JS names stay lowerCamelCase. C++ method names are PascalCase.

[voodoomc](https://github.com/brujabrowser/voodoo) writes the cross-process half of this job (Mojo `Proxy_` / `Stub_`). `brujac` writes the in-process half.

## Build

C++20. MinGW g++ or MSVC. `brujac` needs the C++ standard library. The golden tests fetch [quickjs-ng](https://github.com/quickjs-ng/quickjs) `v0.16.2` on the first configure, so that configure needs a network.

```
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

`-DBRUJA_FETCH_QUICKJS=OFF` configures the compiler and the frontend tests with no fetch.

Tests named `*_v8_roundtrip` build when a sibling checkout `../WASMv8bindings` is present. That facade ships inside the [voodoo](https://github.com/brujabrowser/voodoo) tree. `brujac --backend=v8` still writes a header when the sibling is absent.

## Compile one file

```
build\brujac.exe examples\console\console.bruja -o console_gen.h
build\brujac.exe examples\console\console.bruja -o console_v8_gen.h --backend=v8
build\brujac.exe examples\cocoa\cocoa.mm -o cocoa_gen.h --backend=v8
build\brujac.exe examples\messages\echo.messages.in -o echo_gen.h --backend=v8
build\brujac.exe examples\goxx\hello.ts -o hello.goxx
build\brujac.exe examples\console\console.bruja -o console.goxx --backend=goxx
```

| Flag | What it does |
| --- | --- |
| `-o FILE` | Output path. Required |
| `--namespace=NAME` | C++ namespace. Default `bruja_generated`. With `--backend=goxx` this becomes the Go package name |
| `--guard=NAME` | Include guard. Default is the output filename in capitals |
| `--backend=quickjs\|v8\|goxx` | Codegen. Default `quickjs`. A `.ts` input or a `.goxx` output selects `goxx` when the flag is omitted |

## What you can write

```
interface Name [: BaseName] {
  constructor([ParamType paramName, ...]);
  const Type CONST_NAME = literal;
  [readonly] attribute Type name;
  ReturnType methodName([optional] ParamType paramName [= default], ...);
};

interface mixin MixinName { ... };
TargetInterface includes MixinName;

enum Name { "value", ... };
dictionary Name [: BaseName] { Type field [= default]; ... };
callback Name = ReturnType (ParamType paramName, ...);
```

Types: `void`, `boolean`, `byte`, `octet`, `short`, `unsigned short`, `long`, `unsigned long`, `long long`, `unsigned long long`, `float`, `double`, `unrestricted float`, `unrestricted double`, `DOMString`, `USVString`, `any`, `object`, `sequence<T>`, nullable `T?`, another interface, `enum`, `dictionary`, `callback`, `Promise<T>` as a method return, and a union `(A or B)` as a parameter. The last parameter may be variadic (`Type... name`).

`interface X : Y` is C++ inheritance. The JS binding for a concrete interface flattens every ancestor's members onto that interface's own JS class. `interface mixin` and `includes` are copied into the including interface before codegen. A dictionary's inherited and own fields are both read off the one JS object the caller passes.

An interface-typed argument accepts that interface or any descendant. A return value is wrapped as the declared IDL type.

`Promise<T>` leaves the C++ method synchronous. The JS wrapper builds a promise and resolves it with the returned value. The `.then` callback runs when the job queue is pumped.

`constructor(...)` is a factory slot. The header declares `InstallXConstructor`. The embedder passes a factory once. `new X(...)` from JS calls that factory and wraps the pointer. The pointer stays caller-owned.

A union parameter tries each interface member in declared order, then the non-interface member.

Outside this grammar: extended attributes (`[Foo]`), `static` members, `iterable<T>`, a union as a return type, and multi-file imports. The V8 backend also leaves out variadic constructor parameters, and it says so at generation time. On that backend a write to a `readonly` attribute is ignored; the quickjs backend throws `TypeError`. `sequence<T>` and `Promise<T>` on the V8 backend use quickjs array and promise calls through the facade's value handle.

## TypeScript

`.ts` input emits Go++. The output extension is `.goxx`, so converted files sit next to handwritten `.go`. Covered: functions, classes, field-only interfaces, method-only interfaces, type aliases, `let` / `const` / `var`, `if` / `else`, `for`, `for-of`, `while`, `return`, `new C()` as `NewC()`, and `console.log` as `fmt.Println`. Top-level statements land in `func main()`. See `examples/goxx/hello.ts`.

## Examples

| Example | What it covers |
| --- | --- |
| `examples/console` | Two `DOMString` methods |
| `examples/navigator` | Readonly and writable attributes |
| `examples/document` | Readonly `title` and `url` |
| `examples/processes` | `long` and `boolean` methods |
| `examples/dom` | A DOM slice: inheritance, mixins, dictionaries, callbacks, sequences, unions, `Promise<T>`, constructors |
| `examples/cauldron` | Dictionaries and property registration |
| `examples/rml` | A live element surface |
| `examples/cocoa` | `.mm` IDL above a `// --- cpp ---` marker |
| `examples/messages` | A WebKit `*.messages.in` receiver |
| `examples/goxx` | TypeScript to Go++ |

`examples/dom/dom.bruja` is about 32 interfaces, five levels deep (`EventTarget` through a concrete `HTMLXxxElement`). `tests/golden/dom_roundtrip.cc` builds the generated header and calls it. `tests/golden/dom_v8_roundtrip.cc` does the same for `--backend=v8` when the facade is present. The implementation classes live in `include/bruja_dom/`.

A `.mm` or `.h` with `@interface` is translated to the same IR, with `TARGET_OS_IPHONE` off. A trailing completion-handler parameter becomes `Promise<T>`. `*.messages.in` becomes the JS surface of that receiver.

## Layout

| Path | What it is |
| --- | --- |
| `src/` | Lexer, parser, resolver, and the three generators |
| `examples/` | One language feature, or one input kind, per directory |
| `tests/` | Frontend tests and golden roundtrips |
| `include/bruja_dom/` | DOM implementation headers the golden tests compile against |

`build/` and generated headers stay local.

## License

BSD-3-Clause. See [LICENSE](LICENSE). quickjs-ng is fetched at build time and keeps its own license.
