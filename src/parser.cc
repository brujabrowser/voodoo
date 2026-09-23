#include "parser.h"

#include <algorithm>
#include <limits>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace voodoom {

namespace {

const std::unordered_map<std::string, TypeKind> kScalarKeywords = {
    {"bool", TypeKind::kBool},     {"int8", TypeKind::kInt8},
    {"uint8", TypeKind::kUint8},   {"int16", TypeKind::kInt16},
    {"uint16", TypeKind::kUint16}, {"int32", TypeKind::kInt32},
    {"uint32", TypeKind::kUint32}, {"int64", TypeKind::kInt64},
    {"uint64", TypeKind::kUint64}, {"float", TypeKind::kFloat},
    {"double", TypeKind::kDouble},
};

const std::unordered_map<std::string, TypeKind> kPendingKeywords = {
    {"pending_remote", TypeKind::kPendingRemote},
    {"pending_receiver", TypeKind::kPendingReceiver},
    {"pending_associated_remote", TypeKind::kPendingAssociatedRemote},
    {"pending_associated_receiver", TypeKind::kPendingAssociatedReceiver},
};

// Scans the whole token stream up front for every `struct NAME` / `union
// NAME` / `enum NAME` declaration header, so type references can resolve to
// kStructRef / kUnionRef / kEnumRef regardless of where in the file the
// reference sits relative to the declaration (interfaces/enums have no
// ordering restriction -- only struct/union-embeds-struct/union does,
// checked separately as structs/unions are parsed). Interfaces are
// deliberately not tracked here: a pending_kind<NAME> has never validated
// its NAME against a declared-interfaces set (same permissiveness in v15 --
// see ParseTypeSpecInner's pending_kind case and OwnerOf), so unlike
// struct/union/enum there's nothing for a pre-scan to check membership
// against.
//
// (v16) Brace-depth aware, to additionally figure out which interface/
// struct (if any) each enum is nested inside -- every '{'/'}' in this
// grammar corresponds 1:1 to a struct/union/enum/interface body (no other
// brace usage exists: method param lists use '(...)', attribute lists use
// '[...]'), so a simple stack of "whose body is this brace" is enough.
// This still doesn't *restrict* a nested enum's bare name to only resolve
// within its own declaring container -- see enum_container/container_enums'
// own comments, and ContainerOf below, for why this compiler doesn't do
// real per-container scoping.
struct DeclaredNames {
  std::unordered_set<std::string> structs;
  std::unordered_set<std::string> unions;
  std::unordered_set<std::string> enums;
  // enum name -> interface/struct name it's nested inside, "" if top-level.
  std::unordered_map<std::string, std::string> enum_container;
  // interface/struct name -> the set of enum names nested inside it.
  std::unordered_map<std::string, std::unordered_set<std::string>>
      container_enums;
  // (v24) Nested struct/union inside an interface -- same flat-name
  // permissiveness as nested enums (bare name resolves from anywhere).
  std::unordered_map<std::string, std::string> struct_container;
  std::unordered_map<std::string, std::unordered_set<std::string>>
      container_structs;
  std::unordered_map<std::string, std::string> union_container;
  std::unordered_map<std::string, std::unordered_set<std::string>>
      container_unions;
};

DeclaredNames CollectDeclaredNames(const std::vector<Token>& tokens) {
  DeclaredNames names;
  // The struct/union/enum/interface name most recently seen, waiting to be
  // pushed onto container_stack once its own '{' actually appears (a
  // best-effort association -- like CollectEnumValues below, this doesn't
  // re-validate the grammar, so a malformed file just gets an
  // approximate/empty scan here; the real parser is what actually rejects
  // it).
  std::string pending_kind;
  std::string pending_name;
  std::vector<std::string> container_stack;
  for (size_t i = 0; i < tokens.size(); ++i) {
    const Token& tok = tokens[i];
    if (tok.type == TokenType::kName && i + 1 < tokens.size() &&
        tokens[i + 1].type == TokenType::kName &&
        (tok.text == "struct" || tok.text == "union" ||
         tok.text == "enum" || tok.text == "interface")) {
      pending_kind = tok.text;
      pending_name = tokens[i + 1].text;
      if (pending_kind == "struct") {
        names.structs.insert(pending_name);
        std::string owner =
            container_stack.empty() ? "" : container_stack.back();
        names.struct_container[pending_name] = owner;
        if (!owner.empty()) names.container_structs[owner].insert(pending_name);
      } else if (pending_kind == "union") {
        names.unions.insert(pending_name);
        std::string owner =
            container_stack.empty() ? "" : container_stack.back();
        names.union_container[pending_name] = owner;
        if (!owner.empty()) names.container_unions[owner].insert(pending_name);
      } else if (pending_kind == "enum") {
        names.enums.insert(pending_name);
        std::string owner = container_stack.empty() ? "" : container_stack.back();
        names.enum_container[pending_name] = owner;
        if (!owner.empty()) names.container_enums[owner].insert(pending_name);
      }
      continue;
    }
    if (tok.type == TokenType::kLBrace) {
      // Only a struct/interface body can validly contain a nested enum --
      // union/enum bodies (and anything malformed) push "" so nothing
      // spurious gets attributed to them.
      bool nestable = pending_kind == "struct" || pending_kind == "interface";
      container_stack.push_back(nestable ? pending_name : "");
      pending_kind.clear();
      pending_name.clear();
      continue;
    }
    if (tok.type == TokenType::kRBrace) {
      if (!container_stack.empty()) container_stack.pop_back();
      continue;
    }
  }
  return names;
}

// (real-mojom-parity phase 1/2) base 0 lets std::stoll auto-detect a "0x"/
// "0X" hex prefix (from a kHexNumber token) and otherwise parse plain
// decimal -- safe for a plain decimal kNumber too, since the lexer already
// rejects any leading-zero multi-digit run as an octal-looking literal
// (see lexer.cc), so base-0 auto-detection can never misinterpret a
// legitimately-lexed decimal token as octal.
int64_t ParseIntLiteral(bool negative, const std::string& digits) {
  if (negative) {
    return -std::stoll(digits, nullptr, 0);
  }
  // uint64 constants such as 0xffffffffffffffff do not fit in int64.
  // The bit pattern is stored in int64 and printed back as unsigned.
  return static_cast<int64_t>(std::stoull(digits, nullptr, 0));
}

// (real-mojom-parity phase 2) The set of TypeKinds a const (or a struct
// field default -- see ParseDefaultValue, which recognizes the exact same
// set via its switch) may have. Never struct/union/array/map/pending_* --
// none of those have a literal/named-reference value shape this compiler
// (or, for struct/union/array/map, real mojom's own const grammar as far
// as this project's design reference goes) supports.
bool IsLegalConstType(TypeKind k) {
  switch (k) {
    case TypeKind::kBool:
    case TypeKind::kInt8:
    case TypeKind::kUint8:
    case TypeKind::kInt16:
    case TypeKind::kUint16:
    case TypeKind::kInt32:
    case TypeKind::kUint32:
    case TypeKind::kInt64:
    case TypeKind::kUint64:
    case TypeKind::kFloat:
    case TypeKind::kDouble:
    case TypeKind::kString:
    case TypeKind::kEnumRef:
      return true;
    default:
      return false;
  }
}

// Scans the whole token stream up front for every enum's full value list
// (name -> value, same implicit-increment/explicit-@N-free rule
// ParseEnumValue uses), so a field default like `Status s = OK;` can
// resolve OK's value regardless of whether the enum is declared before or
// after the struct referencing it -- matching how the enum's own NAME is
// already order-independent (see CollectDeclaredNames above). This is a
// lightweight tokens-only scan, not a second copy of Parser::ParseEnum():
// it doesn't validate anything (a malformed enum body just gets a
// best-effort partial or empty entry here) -- the real ParseEnum(), run
// later from ParseBody's normal top-level-decl loop, is what actually
// catches malformed input and throws. Never called for prelude imports'
// enums -- those get merged into this same map by SeedFromPrelude instead
// (they're already fully parsed and validated by the time an import is
// resolved).
std::unordered_map<std::string, EnumDecl> CollectEnumValues(
    const std::vector<Token>& tokens) {
  std::unordered_map<std::string, EnumDecl> result;
  size_t i = 0;
  while (i + 1 < tokens.size()) {
    if (tokens[i].type != TokenType::kName || tokens[i].text != "enum" ||
        tokens[i + 1].type != TokenType::kName) {
      ++i;
      continue;
    }
    EnumDecl e;
    e.name = tokens[i + 1].text;
    size_t j = i + 2;
    if (j < tokens.size() && tokens[j].type == TokenType::kLBrace) {
      ++j;
      int32_t next_value = 0;
      while (j < tokens.size()) {
        if (tokens[j].type == TokenType::kLBracket) {
          while (j < tokens.size() &&
                 tokens[j].type != TokenType::kRBracket) {
            ++j;
          }
          if (j < tokens.size()) ++j;
          continue;
        }
        if (tokens[j].type != TokenType::kName) break;
        EnumValue v;
        v.name = tokens[j].text;
        ++j;
        if (j < tokens.size() && tokens[j].type == TokenType::kEquals) {
          ++j;
          bool negative = false;
          if (j < tokens.size() && tokens[j].type == TokenType::kMinus) {
            negative = true;
            ++j;
          }
          if (j < tokens.size() && (tokens[j].type == TokenType::kNumber ||
                                    tokens[j].type == TokenType::kHexNumber)) {
            v.value = static_cast<int32_t>(
                ParseIntLiteral(negative, tokens[j].text));
            ++j;
          } else if (j < tokens.size() && tokens[j].type == TokenType::kName) {
            std::string alias = tokens[j].text;
            ++j;
            for (const EnumValue& prev : e.values) {
              if (prev.name == alias) {
                v.value = prev.value;
                break;
              }
            }
          }
        } else {
          v.value = next_value;
        }
        next_value = v.value + 1;
        e.values.push_back(v);
        if (j < tokens.size() && tokens[j].type == TokenType::kComma) {
          ++j;
          continue;
        }
        break;
      }
    }
    result[e.name] = std::move(e);
    i = j;
  }
  return result;
}

}  // namespace

// The real parser state, split into ParseHeader() (module + imports) and
// ParseBody() (everything else) so module_loader.cc can resolve this file's
// imports -- using ParseHeader()'s result -- before ParseBody() needs to
// know what types they made visible.
struct Parser::Impl {
  explicit Impl(const std::vector<Token>& tokens)
      : t_(tokens),
        declared_(CollectDeclaredNames(tokens)),
        enum_values_(CollectEnumValues(tokens)) {}

  const std::vector<Token>& t_;
  size_t pos_ = 0;
  // Set by ParseHeader. Same-file references written as
  // `network.mojom.Type` resolve against this, not against an import path.
  std::string module_name_;
  DeclaredNames declared_;
  // Every enum this file declares, by name, values included -- see
  // CollectEnumValues. Order-independent (like declared_.enums already
  // is), unlike const_by_name_ below. Also gets prelude enums merged in
  // by SeedFromPrelude, so an imported enum's values resolve too.
  std::unordered_map<std::string, EnumDecl> enum_values_;
  // Snapshotted at the start of ParseBody, before SeedFromPrelude. A name
  // this file declares itself wins over an imported name spelled the same
  // (cookie_manager.mojom's ContextType vs extensions.mojom's ContextType).
  std::unordered_set<std::string> local_names_;
  std::unordered_map<std::string, EnumDecl> local_enum_values_;
  // Every const this file has parsed *so far*, by name -- unlike
  // enum_values_, this is populated incrementally as ParseBody encounters
  // each `const` (see ParseBody's const branch), not pre-scanned, so a
  // const field default can only reference a const declared earlier in
  // the file (or one of its imports, via SeedFromPrelude) -- consts are
  // not pre-scanned the way struct/union/enum names are.
  std::unordered_map<std::string, ConstDecl> const_by_name_;
  // (real-mojom-parity phase 2) Position counter for ConstDecl::decl_index
  // -- shared across top-level *and* nested (interface/struct) consts, so
  // C++ emission order (see cpp_generator.cc's GenerateCppHeader) can match
  // this file's real, whole-file const declaration order. Never seeded
  // from a prelude (unlike next_embed_order_): an imported const is never
  // re-emitted by the importing file at all (each file generates its own
  // header -- see module_loader.h), so only this file's own consts' mutual
  // ordering matters here.
  int next_const_order_ = 0;
  // Shared position counter across struct AND union declarations (this
  // file's own, plus anything a prelude seeded in): a struct can embed an
  // earlier union and a union can embed an earlier struct, so "declared
  // earlier" has to be checked against one combined ordering, not two
  // separate per-kind vector indices.
  std::unordered_map<std::string, int> embed_order_;  // name -> decl index
  int next_embed_order_ = 0;
  // (real-mojom-parity phase 6) One synthesized two-arm UnionDecl per
  // `result<T, E>` method response encountered anywhere in the file (see
  // ParseMethod's "result" branch) -- ParseMethod has no direct access to
  // ParseBody's local `mod`, so these accumulate here and get spliced into
  // mod.unions right before ParseBody returns. Each entry's own
  // UnionDecl::decl_index is still assigned from next_embed_order_ at the
  // point it's synthesized, so cpp_generator.cc's complete-type Embed-sort
  // places it relative to every real struct/union in the file.
  std::vector<UnionDecl> pending_result_unions_;
  // (v15) name -> the raw `module` statement of the file that declared it,
  // for every struct/union/enum/interface/const seeded from a Prelude (see
  // SeedFromPrelude below). A name with no entry here was declared in the
  // file being parsed right now -- TypeSpec::owner_namespace/
  // DefaultValue::named_expr_owner_namespace are left empty for those.
  std::unordered_map<std::string, std::string> owner_by_name_;
  // (v16) The prelude-side counterparts of declared_.enum_container/
  // declared_.container_enums -- derived in SeedFromPrelude directly from
  // the prelude Module's own Interface::enums/StructDecl::enums (no new
  // Prelude field needed; those sub-vectors already carry the info).
  std::unordered_map<std::string, std::string> container_by_name_;
  std::unordered_map<std::string, std::unordered_set<std::string>>
      container_enums_;
  // (real-mojom-parity phase 2) const name -> its declaring interface/
  // struct name ("" or absent = top-level), for both this file's own
  // nested consts and prelude-seeded ones. Unlike enums, nested consts
  // don't need a pre-scan/container_enums-style membership set: consts are
  // already order-dependent (const_by_name_ is populated incrementally,
  // never pre-scanned), so "is CONST_NAME a member of CONTAINER" is just
  // "does const_by_name_ already have CONST_NAME, and does
  // const_container_[CONST_NAME] say CONTAINER" -- see
  // ResolveQualifiedConst.
  std::unordered_map<std::string, std::string> const_container_;

  const Token& Cur() const { return t_[pos_]; }
  bool At(TokenType type) const { return Cur().type == type; }
  bool AtKeyword(const char* kw) const {
    return At(TokenType::kName) && Cur().text == kw;
  }
  void Advance() {
    if (pos_ + 1 < t_.size()) ++pos_;
  }
  const Token& Expect(TokenType type, const char* what) {
    if (!At(type)) {
      throw ParseError(std::string("expected ") + what + ", got '" +
                            (Cur().text.empty() ? "<eof>" : Cur().text) + "'",
                        Cur().line);
    }
    const Token& tok = Cur();
    Advance();
    return tok;
  }
  const Token& ExpectKeyword(const char* kw) {
    if (!AtKeyword(kw)) {
      throw ParseError(std::string("expected '") + kw + "', got '" +
                            (Cur().text.empty() ? "<eof>" : Cur().text) + "'",
                        Cur().line);
    }
    const Token& tok = Cur();
    Advance();
    return tok;
  }

  // -1..0 sign, followed by a NUMBER or (real-mojom-parity phase 1) HEX
  // token. Returns the raw digit text (with its "0x"/"0X" prefix intact,
  // for a hex literal -- see ParseIntLiteral) and whether a leading '-'
  // was consumed.
  std::pair<bool, std::string> ParseSignedNumber() {
    bool negative = false;
    if (At(TokenType::kMinus)) {
      negative = true;
      Advance();
    }
    if (At(TokenType::kHexNumber)) {
      const Token& num = Cur();
      Advance();
      return {negative, num.text};
    }
    const Token& num = Expect(TokenType::kNumber, "number");
    return {negative, num.text};
  }

  // (real-mojom-parity phase 2) -1..0 sign, followed by a FLOAT, HEX, or
  // plain NUMBER token -- used wherever a float/double *value* is
  // expected. A plain integer literal is accepted too (e.g.
  // `const double x = 5;`), matching real mojom's own literal grammar
  // (`float`/`int` are both plain alternatives of the same `literal`
  // production, not mutually exclusive by target type).
  double ParseFloatValue() {
    bool negative = false;
    if (At(TokenType::kMinus)) {
      negative = true;
      Advance();
    }
    double v;
    if (At(TokenType::kFloatNumber)) {
      v = std::stod(Cur().text);
      Advance();
    } else if (At(TokenType::kHexNumber) || At(TokenType::kNumber)) {
      v = static_cast<double>(std::stoll(Cur().text, nullptr, 0));
      Advance();
    } else {
      throw ParseError("expected a float/double literal", Cur().line);
    }
    return negative ? -v : v;
  }

  // (v23) Consume a leading attribute list. If it precedes `module` or
  // `import`, discard it (Chromium-specific, ignored). Otherwise stash it
  // for ParseBody -- the '[' belonged to a top-level decl.
  bool ConsumeHeaderAttributeList() {
    if (!At(TokenType::kLBracket)) return false;
    std::vector<ParsedAttribute> attrs = ParseAttributeList();
    if (AtKeyword("module") || AtKeyword("import")) return true;
    leftover_attrs_ = std::move(attrs);
    leftover_attrs_set_ = true;
    return false;
  }

  FileHeader ParseHeader() {
    FileHeader h;
    if (At(TokenType::kLBracket) && !ConsumeHeaderAttributeList()) {
      return h;
    }
    if (AtKeyword("module")) {
      Advance();
      h.module_name = Expect(TokenType::kName, "module name").text;
      while (At(TokenType::kDot)) {
        Advance();
        h.module_name +=
            "." + Expect(TokenType::kName, "module name segment").text;
      }
      Expect(TokenType::kSemi, "';' after module name");
      module_name_ = h.module_name;
    }
    while (true) {
      if (At(TokenType::kLBracket)) {
        if (!ConsumeHeaderAttributeList()) break;
      }
      if (!AtKeyword("import")) break;
      h.imports.push_back(ParseImport());
    }
    return h;
  }

  ImportDecl ParseImport() {
    ExpectKeyword("import");
    ImportDecl imp;
    imp.line = Cur().line;
    imp.path = Expect(TokenType::kStringLiteral, "import path string").text;
    Expect(TokenType::kSemi, "';' after import statement");
    return imp;
  }

  Module ParseBody(const Prelude* prelude) {
    local_names_.insert(declared_.enums.begin(), declared_.enums.end());
    local_names_.insert(declared_.structs.begin(), declared_.structs.end());
    local_names_.insert(declared_.unions.begin(), declared_.unions.end());
    local_enum_values_ = enum_values_;
    if (prelude) SeedFromPrelude(*prelude);
    Module mod;
    while (!At(TokenType::kEnd)) {
      // A leading '[' at top level starts an interface_decl's, a
      // union_decl's, an enum_decl's, a struct_decl's, or (real-mojom-
      // parity phase 3) a const_decl's attribute_list
      // (`[Extensible] interface Foo {...};`, ...). Struct/const don't
      // *act* on any attribute today (only a struct *field*'s own
      // `[MinVersion=N]` does), so their attrs are parsed then discarded
      // here rather than threaded through ParseStruct/ParseConst (neither
      // of which takes one) -- accepted, not a parse error, same
      // accept-and-ignore treatment every unrecognized attribute name
      // already gets elsewhere (see ParsedAttribute's comment). Parse the
      // attribute list once, up front, then dispatch on whichever keyword
      // actually follows it.
      if (leftover_attrs_set_ || At(TokenType::kLBracket) ||
          AtKeyword("interface") || AtKeyword("union") || AtKeyword("enum") ||
          AtKeyword("struct") || AtKeyword("const") || AtKeyword("feature")) {
        std::vector<ParsedAttribute> top_attrs;
        if (leftover_attrs_set_) {
          top_attrs = std::move(leftover_attrs_);
          leftover_attrs_set_ = false;
        } else if (At(TokenType::kLBracket)) {
          top_attrs = ParseAttributeList();
        }
        if (AtKeyword("interface")) {
          mod.interfaces.push_back(ParseInterface(top_attrs));
        } else if (AtKeyword("union")) {
          UnionDecl u = ParseUnion(top_attrs);
          u.decl_index = next_embed_order_++;
          embed_order_[u.name] = u.decl_index;
          mod.unions.push_back(std::move(u));
        } else if (AtKeyword("enum")) {
          mod.enums.push_back(ParseEnum(top_attrs));
        } else if (AtKeyword("struct")) {
          StructDecl s = ParseStruct(top_attrs);
          s.decl_index = next_embed_order_++;
          embed_order_[s.name] = s.decl_index;
          mod.structs.push_back(std::move(s));
        } else if (AtKeyword("const")) {
          ConstDecl c = ParseConst();
          for (const ParsedAttribute& a : top_attrs) FillEnableIf(c.enable_if, a);
          c.decl_index = next_const_order_++;
          const_by_name_[c.name] = c;
          mod.consts.push_back(std::move(c));
        } else if (AtKeyword("feature")) {
          FeatureDecl ft = ParseFeature();
          for (const ParsedAttribute& a : top_attrs) {
            FillEnableIf(ft.enable_if, a);
          }
          mod.features.push_back(std::move(ft));
        } else {
          throw ParseError(
              "expected 'interface', 'union', 'enum', 'struct', 'const', "
              "or 'feature' after attribute list, got '" +
                  (Cur().text.empty() ? "<eof>" : Cur().text) + "'",
              Cur().line);
        }
      } else if (AtKeyword("module")) {
        throw ParseError(
            "'module' statement must be the first thing in the file",
            Cur().line);
      } else if (AtKeyword("import")) {
        throw ParseError(
            "'import' statements must come before any other declaration",
            Cur().line);
      } else {
        throw ParseError(
            "expected 'interface', 'struct', 'union', 'enum', or 'const', "
            "got '" +
                (Cur().text.empty() ? "<eof>" : Cur().text) + "'",
            Cur().line);
      }
    }
    // (real-mojom-parity phase 6) Splice in every `result<T, E>` response's
    // synthesized union, accumulated across the whole file -- see
    // pending_result_unions_'s own comment for why this can't just be
    // pushed into mod.unions directly from ParseMethod. Vector position
    // doesn't matter for emission order (cpp_generator.cc's Embed-sort
    // orders purely by UnionDecl::decl_index, already assigned correctly
    // by MakeResultUnion), only decl_index does.
    for (UnionDecl& u : pending_result_unions_) {
      mod.unions.push_back(std::move(u));
    }
    CheckCompleteTypeAcyclic(mod);
    return mod;
  }

  // Makes a prelude's struct/union/enum declarations resolvable as types in
  // this file -- see CheckCompleteTypeAcyclic. Also makes its enum values and consts
  // resolvable for field defaults (`Status s = OK;` / `int32 x = kMax;`)
  // -- see enum_values_/const_by_name_ above. Interfaces need no such
  // seeding into declared_: they're never ordering-checked (see the
  // parser.h grammar comment) and, unlike struct/union/enum, a
  // pending_kind's interface NAME is never membership-checked either (see
  // DeclaredNames' comment) -- only owner_by_name_ below matters for one.
  // owner_by_name_ (v15) is copied in wholesale: every name `prelude.module`
  // carries has a corresponding owner in `prelude.owner_by_name`, since
  // `prelude.module` only ever holds *other* files' declarations (see
  // Prelude's own comment in parser.h).
  void SeedFromPrelude(const Prelude& prelude) {
    owner_by_name_ = prelude.owner_by_name;
    for (const StructDecl& s : prelude.module.structs) {
      declared_.structs.insert(s.name);
      embed_order_[s.name] = s.decl_index;
      next_embed_order_ = std::max(next_embed_order_, s.decl_index + 1);
      // (v16) This struct's own nested enums resolve too -- same treatment
      // as a top-level enum below, just sourced from s.enums instead of
      // prelude.module.enums.
      for (const EnumDecl& e : s.enums) {
        declared_.enums.insert(e.name);
        enum_values_[e.name] = e;
        container_by_name_[e.name] = s.name;
        container_enums_[s.name].insert(e.name);
      }
      // (real-mojom-parity phase 2) This struct's own nested consts.
      for (const ConstDecl& c : s.consts) {
        const_by_name_[c.name] = c;
        const_container_[c.name] = s.name;
      }
    }
    for (const UnionDecl& u : prelude.module.unions) {
      declared_.unions.insert(u.name);
      embed_order_[u.name] = u.decl_index;
      next_embed_order_ = std::max(next_embed_order_, u.decl_index + 1);
    }
    for (const EnumDecl& e : prelude.module.enums) {
      declared_.enums.insert(e.name);
      enum_values_[e.name] = e;
    }
    // Interfaces are not seeded into declared_: nothing checks membership
    // there (see DeclaredNames' comment) -- owner_by_name_ above is already
    // enough for ParseTypeSpecInner's pending_kind case to qualify a
    // prelude interface reference. Their own nested enums (v16) still need
    // the same treatment structs' do, just via container_by_name_/
    // container_enums_ instead of declared_.enum_container/
    // declared_.container_enums (this file's own containers aren't parsed
    // yet, so there's no equivalent "this file's own interfaces" set to
    // add these to).
    for (const Interface& iface : prelude.module.interfaces) {
      for (const EnumDecl& e : iface.enums) {
        declared_.enums.insert(e.name);
        enum_values_[e.name] = e;
        container_by_name_[e.name] = iface.name;
        container_enums_[iface.name].insert(e.name);
      }
      // (real-mojom-parity phase 2) This interface's own nested consts.
      for (const ConstDecl& c : iface.consts) {
        const_by_name_[c.name] = c;
        const_container_[c.name] = iface.name;
      }
      for (const StructDecl& s : iface.structs) {
        declared_.structs.insert(s.name);
        declared_.struct_container[s.name] = iface.name;
        declared_.container_structs[iface.name].insert(s.name);
        embed_order_[EmbedDeclKey(iface.name, s.name)] = s.decl_index;
        next_embed_order_ = std::max(next_embed_order_, s.decl_index + 1);
        for (const EnumDecl& e : s.enums) {
          declared_.enums.insert(e.name);
          enum_values_[e.name] = e;
          container_by_name_[e.name] = s.name;
          container_enums_[s.name].insert(e.name);
        }
        for (const ConstDecl& c : s.consts) {
          const_by_name_[c.name] = c;
          const_container_[c.name] = s.name;
        }
      }
      for (const UnionDecl& u : iface.unions) {
        declared_.unions.insert(u.name);
        declared_.union_container[u.name] = iface.name;
        declared_.container_unions[iface.name].insert(u.name);
        embed_order_[EmbedDeclKey(iface.name, u.name)] = u.decl_index;
        next_embed_order_ = std::max(next_embed_order_, u.decl_index + 1);
      }
    }
    for (const ConstDecl& c : prelude.module.consts) {
      const_by_name_[c.name] = c;
    }
    // (real-mojom-parity phase 7) A prelude's own feature consts.
    for (const FeatureDecl& ft : prelude.module.features) {
      for (const ConstDecl& c : ft.consts) {
        const_by_name_[c.name] = c;
        const_container_[c.name] = ft.name;
      }
    }
  }

  // One '[' NAME ('=' value)? (',' ...)* ']' item's raw parse -- just a
  // name + optional value, no interpretation of what's valid where. Each
  // call site (ParseMethod, ParseInterface, ParseStruct's field loop,
  // ParseUnion, ParseUnion's field loop, ParseEnum) decides which
  // names/value-shapes it actually *acts* on (`[Sync]`/`[MinVersion=N]`/
  // `[Extensible]`, still the only three with real behavior) and throws a
  // specific error for a *recognized* name used the wrong way (wrong
  // value shape, wrong site) -- but (real-mojom-parity phase 3) an
  // unrecognized name is no longer a parse error: it's accepted, parsed
  // into `value_kind`'s appropriate field, and silently ignored, the same
  // way this compiler already accepts-and-ignores every semantically
  // Chromium-specific attribute real .mojom files use
  // (`[ServiceSandbox]`, `[Uuid]`, `[Stable]`, `[EnableIf=...]`, ...) --
  // rejecting those would make real-file compatibility unreachable. See
  // README's "Known simplifications".
  struct ParsedAttribute {
    std::string name;
    bool has_value = false;
    // (real-mojom-parity phase 3) Which of the fields below is meaningful
    // -- mirrors real mojom's own attribute grammar
    // (`identifier | evaluated_literal | pipe_delimited_names |
    // amps_delimited_names`), widened from this compiler's original
    // integer-literal-only value. `value`/`int_value` is the *pre-v17*
    // field name, kept as-is (not renamed) since the three real,
    // acted-on attributes (`[MinVersion=N]`, all integer) only ever read
    // it -- renaming would touch call sites for no behavioral benefit.
    enum class ValueKind {
      kNone,
      kInt,
      kFloat,
      kBool,
      kString,
      kIdentifier,
      kPipeList,
      kAmpList,
    };
    ValueKind value_kind = ValueKind::kNone;
    int64_t value = 0;              // kInt
    double float_value = 0.0;       // kFloat
    bool bool_value = false;        // kBool
    std::string string_value;       // kString
    std::string identifier_value;   // kIdentifier
    std::vector<std::string> list_values;  // kPipeList / kAmpList
    int line = 0;
  };

  // (v23) See ParseHeader: a leading '[' consumed as a module/import
  // attribute that actually belonged to a top-level decl.
  std::vector<ParsedAttribute> leftover_attrs_;
  bool leftover_attrs_set_ = false;

  std::vector<ParsedAttribute> ParseAttributeList() {
    Expect(TokenType::kLBracket, "'['");
    std::vector<ParsedAttribute> attrs;
    while (true) {
      ParsedAttribute a;
      const Token& name_tok = Expect(TokenType::kName, "attribute name");
      a.name = name_tok.text;
      a.line = name_tok.line;
      if (At(TokenType::kEquals)) {
        Advance();
        a.has_value = true;
        if (At(TokenType::kStringLiteral)) {
          a.value_kind = ParsedAttribute::ValueKind::kString;
          a.string_value = Cur().text;
          Advance();
        } else if (AtKeyword("true") || AtKeyword("false")) {
          a.value_kind = ParsedAttribute::ValueKind::kBool;
          a.bool_value = AtKeyword("true");
          Advance();
        } else if (At(TokenType::kName)) {
          // A bare identifier, or the first element of a pipe/amp-
          // delimited list (real mojom: `[EnableIf=some_flag]` vs.
          // `[EnableIf=a|b]`) -- which one only becomes clear after
          // seeing (or not seeing) a '|'/'&' immediately following.
          std::string first = Cur().text;
          Advance();
          // [RequireContext=sandbox.mojom.Context.kBrowser]
          while (At(TokenType::kDot)) {
            Advance();
            first += "." + Expect(TokenType::kName, "name after '.'").text;
          }
          if (At(TokenType::kPipe)) {
            a.value_kind = ParsedAttribute::ValueKind::kPipeList;
            a.list_values.push_back(first);
            while (At(TokenType::kPipe)) {
              Advance();
              a.list_values.push_back(
                  Expect(TokenType::kName, "name after '|'").text);
            }
          } else if (At(TokenType::kAmpersand)) {
            a.value_kind = ParsedAttribute::ValueKind::kAmpList;
            a.list_values.push_back(first);
            while (At(TokenType::kAmpersand)) {
              Advance();
              a.list_values.push_back(
                  Expect(TokenType::kName, "name after '&'").text);
            }
          } else {
            a.value_kind = ParsedAttribute::ValueKind::kIdentifier;
            a.identifier_value = first;
          }
        } else {
          // Numeric literal -- optional '-', then either an int
          // (NUMBER/HEX) or a float (FLOAT) token; look one token past a
          // leading '-' to tell which, since ParseSignedNumber/
          // ParseFloatValue each only accept their own kind.
          bool negative = At(TokenType::kMinus);
          size_t peek_pos = negative ? pos_ + 1 : pos_;
          TokenType peek_type =
              peek_pos < t_.size() ? t_[peek_pos].type : TokenType::kEnd;
          if (peek_type == TokenType::kFloatNumber) {
            a.value_kind = ParsedAttribute::ValueKind::kFloat;
            a.float_value = ParseFloatValue();
          } else {
            auto [neg, digits] = ParseSignedNumber();
            a.value_kind = ParsedAttribute::ValueKind::kInt;
            a.value = ParseIntLiteral(neg, digits);
          }
        }
      }
      attrs.push_back(std::move(a));
      if (At(TokenType::kComma)) {
        Advance();
        continue;
      }
      break;
    }
    Expect(TokenType::kRBracket, "']'");
    return attrs;
  }

  void FillEnableIf(EnableIf& dst, const ParsedAttribute& a) {
    if (a.name != "EnableIf" && a.name != "EnableIfNot") return;
    if (!a.has_value) {
      throw ParseError("'[" + a.name + "=...]' requires a flag name", a.line);
    }
    EnableIfClause clause;
    if (a.value_kind == ParsedAttribute::ValueKind::kIdentifier) {
      clause.mode = EnableIfClause::Mode::kAny;
      clause.flags.push_back(a.identifier_value);
    } else if (a.value_kind == ParsedAttribute::ValueKind::kPipeList) {
      clause.mode = EnableIfClause::Mode::kAny;
      clause.flags = a.list_values;
    } else if (a.value_kind == ParsedAttribute::ValueKind::kAmpList) {
      clause.mode = EnableIfClause::Mode::kAll;
      clause.flags = a.list_values;
    } else {
      throw ParseError("'[" + a.name +
                           "=...]' value must be a flag name, a|b, or a&b",
                       a.line);
    }
    if (a.name == "EnableIf") {
      dst.enable_if = std::move(clause);
    } else {
      dst.enable_if_not = std::move(clause);
    }
  }

  // `top_attrs` is whatever attribute_list (if any) preceded 'interface'
  // -- already parsed by ParseBody's dispatch, since a leading '[' is
  // ambiguous between an interface_decl and a union_decl until the
  // keyword after it is seen.
  Interface ParseInterface(const std::vector<ParsedAttribute>& top_attrs) {
    Interface iface;
    for (const ParsedAttribute& a : top_attrs) {
      if (a.name == "Extensible" && !a.has_value) {
        iface.is_extensible = true;
      } else if (a.name == "Extensible") {
        throw ParseError("'[Extensible]' doesn't take a value", a.line);
      } else {
        FillEnableIf(iface.enable_if, a);
      }
    }
    ExpectKeyword("interface");
    iface.name = Expect(TokenType::kName, "interface name").text;
    Expect(TokenType::kLBrace, "'{'");
    bool any_explicit = false;
    bool any_implicit = false;
    uint32_t next_implicit = 0;
    while (!At(TokenType::kRBrace)) {
      // (v16) A leading '[' is ambiguous here too, same as top-level
      // ParseBody's -- it could be a nested enum's `[Extensible]` or a
      // method's `[Sync]`/`[MinVersion=N]`. Parse it once, then dispatch.
      std::vector<ParsedAttribute> attrs;
      if (At(TokenType::kLBracket)) {
        attrs = ParseAttributeList();
      }
      if (AtKeyword("enum")) {
        iface.enums.push_back(ParseEnum(attrs));
        continue;
      }
      if (AtKeyword("struct")) {
        StructDecl s = ParseStruct(attrs);
        s.owner_container = iface.name;
        s.decl_index = next_embed_order_++;
        embed_order_[EmbedDeclKey(s.owner_container, s.name)] = s.decl_index;
        iface.structs.push_back(std::move(s));
        continue;
      }
      if (AtKeyword("union")) {
        UnionDecl u = ParseUnion(attrs);
        u.owner_container = iface.name;
        u.decl_index = next_embed_order_++;
        embed_order_[EmbedDeclKey(u.owner_container, u.name)] = u.decl_index;
        iface.unions.push_back(std::move(u));
        continue;
      }
      if (AtKeyword("const")) {
        // (real-mojom-parity phase 3) ParseConst() doesn't take an
        // attribute list (no top-level/other-nested const does either) --
        // `attrs` is simply discarded, same accept-and-ignore treatment
        // as any unrecognized attribute name gets (see ParsedAttribute's
        // comment).
        ConstDecl c = ParseConst();
        for (const ParsedAttribute& a : attrs) FillEnableIf(c.enable_if, a);
        c.decl_index = next_const_order_++;
        const_container_[c.name] = iface.name;
        const_by_name_[c.name] = c;
        iface.consts.push_back(std::move(c));
        continue;
      }
      Method m = ParseMethod(attrs, iface.name);
      if (m.has_explicit_ordinal) {
        any_explicit = true;
      } else {
        m.ordinal = next_implicit++;
        any_implicit = true;
      }
      iface.version = std::max(iface.version, m.min_version);
      iface.methods.push_back(std::move(m));
    }
    Expect(TokenType::kRBrace, "'}'");
    Expect(TokenType::kSemi, "';' after interface body");
    if (any_explicit && any_implicit) {
      throw ParseError(
          "interface '" + iface.name +
              "': either every method has an explicit @ordinal or none do",
          Cur().line);
    }
    return iface;
  }

  // `top_attrs`: parsed by the caller (ParseInterface's body loop), same
  // "attrs first, then dispatch" reason ParseUnion/ParseEnum already take
  // one -- see ParseInterface's comment on its own '[' handling.
  // `iface_name`: (real-mojom-parity phase 6) only needed to name the
  // synthesized union a `result<T, E>` response desugars into -- see the
  // '=>' handling below.
  Method ParseMethod(const std::vector<ParsedAttribute>& top_attrs,
                      const std::string& iface_name) {
    Method m;
    for (const ParsedAttribute& a : top_attrs) {
      if (a.name == "Sync" && !a.has_value) {
        m.is_sync = true;
      } else if (a.name == "Sync") {
        throw ParseError("'[Sync]' doesn't take a value", a.line);
      } else if (a.name == "MinVersion" && a.has_value &&
                 a.value_kind == ParsedAttribute::ValueKind::kInt) {
        if (a.value < 0) {
          throw ParseError("'[MinVersion=N]': N must be non-negative",
                            a.line);
        }
        m.min_version = static_cast<uint32_t>(a.value);
      } else if (a.name == "MinVersion") {
        throw ParseError("'[MinVersion=N]' requires an integer value",
                          a.line);
      } else {
        FillEnableIf(m.enable_if, a);
      }
    }
    m.name = Expect(TokenType::kName, "method name").text;
    // (v23) Real mojom puts `@N` between the name and `(` (`Name@0(...)`).
    // This compiler historically accepted `Name(...)@N` after the closing
    // paren; both positions are legal, but not both at once.
    auto take_method_ordinal = [&]() {
      const Token& at_tok = Cur();
      Advance();
      const Token& num = Expect(TokenType::kNumber, "ordinal number");
      if (m.has_explicit_ordinal) {
        throw ParseError(
            "method '" + m.name + "': ordinal specified twice", at_tok.line);
      }
      m.ordinal = static_cast<uint32_t>(std::stoul(num.text));
      m.has_explicit_ordinal = true;
      if (m.ordinal >= 0xFFFFFFFEu) {
        throw ParseError(
            "method '" + m.name + "': ordinal " + std::to_string(m.ordinal) +
                " collides with a reserved control-message ordinal "
                "(0xFFFFFFFE and 0xFFFFFFFF are reserved for "
                "QueryVersion/RequireVersion)",
            at_tok.line);
      }
    };
    if (At(TokenType::kAt)) take_method_ordinal();
    Expect(TokenType::kLParen, "'('");
    if (!At(TokenType::kRParen)) {
      m.params = ParseParamList();
    }
    Expect(TokenType::kRParen, "')'");
    if (At(TokenType::kAt)) take_method_ordinal();
    if (At(TokenType::kArrow)) {
      Advance();
      m.has_response = true;
      if (AtKeyword("result")) {
        // `=> result<T, E>` -- wire is a synthesized two-arm union
        // (value/error); the generated C++ API is base::expected<T, E>
        // from Bindings (the base:: rung).
        Advance();
        Expect(TokenType::kLAngle, "'<'");
        TypeSpec value_type = ParseTypeSpec();
        Expect(TokenType::kComma, "',' between result<T, E>'s two types");
        TypeSpec error_type = ParseTypeSpec();
        Expect(TokenType::kRAngle, "'>'");
        m.is_result_response = true;
        m.result_success = value_type;
        m.result_error = error_type;
        m.response_params.push_back(
            {MakeResultUnion(iface_name, m.name, value_type, error_type),
             "result"});
      } else {
        Expect(TokenType::kLParen, "'(' after '=>'");
        if (!At(TokenType::kRParen)) {
          m.response_params = ParseParamList();
        }
        Expect(TokenType::kRParen, "')'");
      }
    }
    Expect(TokenType::kSemi, "';' after method declaration");
    if (m.is_sync && !m.has_response) {
      throw ParseError(
          "'[Sync]' method '" + m.name +
              "' must have a response (=> (...)) -- there's nothing to "
              "block for otherwise",
          Cur().line);
    }
    return m;
  }

  // (real-mojom-parity phase 6) Builds the synthesized two-arm union a
  // `result<T, E>` response desugars into (see ParseMethod's '=>'
  // handling), registers it in pending_result_unions_ for ParseBody to
  // splice into the module once parsing finishes, and returns the
  // kUnionRef TypeSpec the method's one response param should have.
  // Named "<Interface>_<Method>Result" -- interface-and-method-qualified
  // so two different methods (even same-named ones on different
  // interfaces) never collide within this file's one flat union
  // namespace. `value`/`error` follow union field rules (T/E that are
  // struct/union types contribute complete-type deps, checked at end of
  // ParseBody -- they may be declared later in the file).
  TypeSpec MakeResultUnion(const std::string& iface_name,
                           const std::string& method_name,
                           const TypeSpec& value_type,
                           const TypeSpec& error_type) {
    std::string name = iface_name + "_" + method_name + "Result";

    UnionField value_field;
    value_field.type = value_type;
    value_field.name = "value";
    value_field.tag = 0;
    UnionField error_field;
    error_field.type = error_type;
    error_field.name = "error";
    error_field.tag = 1;

    UnionDecl u;
    u.name = name;
    u.fields = {std::move(value_field), std::move(error_field)};
    u.decl_index = next_embed_order_++;
    pending_result_unions_.push_back(std::move(u));

    return TypeSpec{TypeKind::kUnionRef, name, nullptr, nullptr, false};
  }

  std::vector<Param> ParseParamList() {
    std::vector<Param> params;
    params.push_back(ParseParam());
    while (At(TokenType::kComma)) {
      Advance();
      params.push_back(ParseParam());
    }
    return params;
  }

  Param ParseParam() {
    Param p;
    if (At(TokenType::kLBracket)) {
      for (const ParsedAttribute& a : ParseAttributeList()) {
        if (a.name == "MinVersion" && a.has_value &&
            a.value_kind == ParsedAttribute::ValueKind::kInt) {
          if (a.value < 0) {
            throw ParseError("'[MinVersion=N]': N must be non-negative",
                              a.line);
          }
          p.min_version = static_cast<uint32_t>(a.value);
        } else if (a.name == "MinVersion") {
          throw ParseError("'[MinVersion=N]' requires an integer value",
                            a.line);
        } else {
          FillEnableIf(p.enable_if, a);
        }
      }
    }
    p.type = ParseTypeSpec();
    p.name = Expect(TokenType::kName, "parameter name").text;
    if (At(TokenType::kAt)) {
      Advance();
      const Token& num = Expect(TokenType::kNumber, "parameter ordinal");
      p.ordinal = static_cast<uint32_t>(std::stoul(num.text));
      p.has_explicit_ordinal = true;
    }
    return p;
  }

  StructDecl ParseStruct(const std::vector<ParsedAttribute>& top_attrs) {
    ExpectKeyword("struct");
    StructDecl s;
    for (const ParsedAttribute& a : top_attrs) {
      if (a.name == "Native" && !a.has_value) {
        s.is_native = true;
      } else if (a.name == "Native") {
        throw ParseError("'[Native]' doesn't take a value", a.line);
      } else {
        FillEnableIf(s.enable_if, a);
      }
    }
    s.name = Expect(TokenType::kName, "struct name").text;
    // (v23) `struct Foo;` -- real mojom's empty/native struct (usually
    // `[Native] struct Foo;`). No fields; codegen emits an empty struct
    // with just the wire header, unless `--typemap` replaces it.
    if (At(TokenType::kSemi)) {
      Advance();
      return s;
    }
    Expect(TokenType::kLBrace, "'{'");
    while (!At(TokenType::kRBrace)) {
      std::vector<ParsedAttribute> attrs;
      if (At(TokenType::kLBracket)) {
        attrs = ParseAttributeList();
      }
      if (AtKeyword("enum")) {
        s.enums.push_back(ParseEnum(attrs));
        continue;
      }
      if (AtKeyword("const")) {
        ConstDecl c = ParseConst();
        for (const ParsedAttribute& a : attrs) FillEnableIf(c.enable_if, a);
        c.decl_index = next_const_order_++;
        const_container_[c.name] = s.name;
        const_by_name_[c.name] = c;
        s.consts.push_back(std::move(c));
        continue;
      }
      StructField f;
      for (const ParsedAttribute& a : attrs) {
        if (a.name == "MinVersion" && a.has_value &&
            a.value_kind == ParsedAttribute::ValueKind::kInt) {
          if (a.value < 0) {
            throw ParseError("'[MinVersion=N]': N must be non-negative",
                              a.line);
          }
          f.min_version = static_cast<uint32_t>(a.value);
        } else if (a.name == "MinVersion") {
          throw ParseError("'[MinVersion=N]' requires an integer value",
                            a.line);
        } else {
          FillEnableIf(f.enable_if, a);
        }
      }
      f.type = ParseTypeSpec();
      f.name = Expect(TokenType::kName, "field name").text;
      if (At(TokenType::kAt)) {
        Advance();
        const Token& num = Expect(TokenType::kNumber, "field ordinal");
        f.ordinal = static_cast<uint32_t>(std::stoul(num.text));
        f.has_explicit_ordinal = true;
      }
      if (At(TokenType::kEquals)) {
        Advance();
        f.default_value = ParseDefaultValue(f.type);
      }
      Expect(TokenType::kSemi, "';' after field declaration");
      s.version = std::max(s.version, f.min_version);
      s.fields.push_back(std::move(f));
    }
    Expect(TokenType::kRBrace, "'}'");
    Expect(TokenType::kSemi, "';' after struct body");
    return s;
  }

  // `= literal` after a struct field's name (and, real-mojom-parity phase
  // 2, a ConstDecl's own value -- see ParseConst). Legal for non-nullable
  // bool/integer/float/double/string/enum fields.
  DefaultValue ParseDefaultValue(const TypeSpec& field_type) {
    // `uint32? rule_id_matched = 0` — the `?` is the absent state, the
    // `= 0` is the value used when the field is present.
    DefaultValue d;
    d.has_value = true;
    switch (field_type.kind) {
      case TypeKind::kBool: {
        if (AtKeyword("true")) {
          d.bool_value = true;
          Advance();
        } else if (AtKeyword("false")) {
          d.bool_value = false;
          Advance();
        } else if (At(TokenType::kName)) {
          const ConstDecl& c = ResolveConstReference(field_type);
          d.bool_value = c.value.bool_value;
          d.named_expr = c.name;
          d.named_expr_owner_namespace = OwnerOf(c.name);
          d.named_expr_owner_container = ConstContainerOf(c.name);
        } else {
          throw ParseError(
              "expected 'true', 'false', or a previously declared const "
              "name for a bool field default, got '" +
                  (Cur().text.empty() ? "<eof>" : Cur().text) + "'",
              Cur().line);
        }
        return d;
      }
      case TypeKind::kInt8:
      case TypeKind::kUint8:
      case TypeKind::kInt16:
      case TypeKind::kUint16:
      case TypeKind::kInt32:
      case TypeKind::kUint32:
      case TypeKind::kInt64:
      case TypeKind::kUint64: {
        if (At(TokenType::kName)) {
          const ConstDecl& c = ResolveConstReference(field_type);
          d.int_value = c.value.int_value;
          d.named_expr = c.name;
          d.named_expr_owner_namespace = OwnerOf(c.name);
          d.named_expr_owner_container = ConstContainerOf(c.name);
          return d;
        }
        auto [negative, digits] = ParseSignedNumber();
        d.int_value = ParseIntLiteral(negative, digits);
        return d;
      }
      case TypeKind::kFloat:
      case TypeKind::kDouble: {
        // (real-mojom-parity phase 8) Real mojom's special float/double
        // constant literals -- `float.INFINITY`/`double.INFINITY`,
        // `*.NEGATIVE_INFINITY`, `*.NAN` -- found via a real vendored
        // .mojom file (services/device/public/mojom/battery_status.mojom's
        // `double discharging_time = double.INFINITY;`) during phase 8's
        // real-file corpus work. Checked before the general NAME/const-
        // reference branch below, since "double"/"float" here are never
        // legal const names anyway (both are reserved scalar-type
        // keywords) -- this can't misfire on a real const reference.
        if (At(TokenType::kName) &&
            (Cur().text == "float" || Cur().text == "double") &&
            pos_ + 2 < t_.size() && t_[pos_ + 1].type == TokenType::kDot &&
            (t_[pos_ + 2].text == "INFINITY" ||
             t_[pos_ + 2].text == "NEGATIVE_INFINITY" ||
             t_[pos_ + 2].text == "NAN")) {
          const std::string& which = t_[pos_ + 2].text;
          if (which == "INFINITY") {
            d.float_special = DefaultValue::FloatSpecial::kInfinity;
            d.float_value = std::numeric_limits<double>::infinity();
          } else if (which == "NEGATIVE_INFINITY") {
            d.float_special = DefaultValue::FloatSpecial::kNegativeInfinity;
            d.float_value = -std::numeric_limits<double>::infinity();
          } else {
            d.float_special = DefaultValue::FloatSpecial::kNaN;
            d.float_value = std::numeric_limits<double>::quiet_NaN();
          }
          Advance();  // 'float'/'double'
          Advance();  // '.'
          Advance();  // INFINITY/NEGATIVE_INFINITY/NAN
          return d;
        }
        if (At(TokenType::kName)) {
          const ConstDecl& c = ResolveConstReference(field_type);
          d.float_value = c.value.float_value;
          d.float_special = c.value.float_special;
          d.named_expr = c.name;
          d.named_expr_owner_namespace = OwnerOf(c.name);
          d.named_expr_owner_container = ConstContainerOf(c.name);
          return d;
        }
        d.float_value = ParseFloatValue();
        return d;
      }
      case TypeKind::kString: {
        if (At(TokenType::kName)) {
          const ConstDecl& c = ResolveConstReference(field_type);
          d.string_value = c.value.string_value;
          d.named_expr = c.name;
          d.named_expr_owner_namespace = OwnerOf(c.name);
          d.named_expr_owner_container = ConstContainerOf(c.name);
          return d;
        }
        d.string_value =
            Expect(TokenType::kStringLiteral, "string field default").text;
        return d;
      }
      case TypeKind::kEnumRef: {
        const Token& value_name =
            Expect(TokenType::kName, "enum value name for field default");
        // `WebSandboxFlags.kNone`, or `network.mojom.WebSandboxFlags.kNone`.
        // The last segment is the enumerator.
        std::string enumerator = value_name.text;
        while (At(TokenType::kDot)) {
          Advance();
          enumerator =
              Expect(TokenType::kName, "enum value name after '.'").text;
        }
        const EnumDecl* enum_decl = EnumForDefault(field_type);
        if (!enum_decl) {
          throw ParseError(
              "enum '" + field_type.name +
                  "' must be declared (fully, in this file or one of its "
                  "imports) before its values can be used as a field "
                  "default",
              value_name.line);
        }
        const EnumDecl& e = *enum_decl;
        auto value_it =
            std::find_if(e.values.begin(), e.values.end(),
                         [&](const EnumValue& v) {
                           return v.name == enumerator;
                         });
        if (value_it == e.values.end()) {
          // An [EnableIf] enumerator is stripped before a later file can
          // name it (Sandbox.kPdfConversion). Keep the reference; the
          // const or field that uses it is dropped with the same flag.
          d.int_value = 0;
          d.named_expr = field_type.name + "::" + enumerator;
          d.named_expr_owner_namespace = field_type.owner_namespace;
          d.named_expr_owner_container = field_type.owner_container;
          return d;
        }
        d.int_value = value_it->value;
        d.named_expr = field_type.name + "::" + value_it->name;
        d.named_expr_owner_namespace = field_type.owner_namespace;
        d.named_expr_owner_container = field_type.owner_container;
        return d;
      }
      default:
        if (field_type.kind == TypeKind::kStructRef &&
            !declared_.structs.count(field_type.name) &&
            At(TokenType::kName)) {
          // EnableIf-stripped enum, referenced as a default
          // (`PrintScalingType x = kUnknownPrintScalingType`).
          std::string enumerator = Cur().text;
          Advance();
          while (At(TokenType::kDot)) {
            Advance();
            enumerator =
                Expect(TokenType::kName, "name after '.'").text;
          }
          d.named_expr = enumerator;
          return d;
        }
        throw ParseError(
            "field defaults are only supported for bool/integer/float/"
            "double/string/enum types (never struct/union/array/map/"
            "pending_*)",
            Cur().line);
    }
  }

  // Resolves a bare NAME used as a bool/integer field's default to a
  // previously-declared const of *exactly* the field's own TypeSpec::kind
  // -- no implicit widening/narrowing between e.g. int32 and int64, same
  // strictness real mojom applies. "Previously-declared" means textually
  // earlier in this file, or in one of its imports (see const_by_name_'s
  // comment for why this is order-dependent, unlike enum_values_).
  const ConstDecl& ResolveConstDefault(const TypeSpec& field_type,
                                        const Token& name_tok) {
    auto it = const_by_name_.find(name_tok.text);
    if (it == const_by_name_.end()) {
      throw ParseError(
          "'" + name_tok.text +
              "' must be a previously declared const (earlier in this "
              "file, or in one of its imports) to use as a field default",
          name_tok.line);
    }
    if (it->second.type.kind != field_type.kind) {
      throw ParseError("const '" + name_tok.text +
                            "' has a different type than this field -- a "
                            "field default must match its field's type "
                            "exactly",
                        name_tok.line);
    }
    return it->second;
  }

  // (real-mojom-parity phase 2) Resolves a bare NAME or qualified
  // `Container.NAME` const reference (self-consuming -- advances past
  // every token it reads) used as a field/const default value. The
  // qualified form still resolves against the same order-dependent
  // const_by_name_ ResolveConstDefault already uses -- a nested const is
  // no more forward-referenceable through its qualified name than an
  // unqualified one is (consts, unlike enums, were never made
  // order-independent -- see const_by_name_'s own comment).
  const ConstDecl& ResolveConstReference(const TypeSpec& field_type) {
    const Token& name_tok = Expect(TokenType::kName, "const name");
    std::vector<std::string> segs;
    segs.push_back(name_tok.text);
    int line = name_tok.line;
    while (At(TokenType::kDot)) {
      Advance();
      const Token& seg = Expect(TokenType::kName, "const name after '.'");
      segs.push_back(seg.text);
      line = seg.line;
    }
    if (segs.size() == 1) return ResolveConstDefault(field_type, name_tok);
    Token member{TokenType::kName, segs.back(), line};
    const ConstDecl& c = ResolveConstDefault(field_type, member);
    // `Container.NAME` must name that container. `blink.mojom.kFoo` is a
    // module-qualified const; the last segment is the const.
    if (segs.size() == 2) {
      std::string actual_container = ConstContainerOf(member.text);
      if (actual_container != segs[0]) {
        throw ParseError(
            "'" + segs[0] + "' has no nested const '" + member.text + "'" +
                (actual_container.empty()
                     ? " (it's a top-level const)"
                     : " (it belongs to '" + actual_container + "')"),
            member.line);
      }
    }
    return c;
  }

  // (real-mojom-parity phase 2) "" if `name` (a const already confirmed to
  // be in const_by_name_) is top-level, else the interface/struct it's
  // nested inside -- mirrors ContainerOf/OwnerOf's pattern.
  std::string ConstContainerOf(const std::string& name) const {
    auto it = const_container_.find(name);
    return it == const_container_.end() ? std::string() : it->second;
  }

  // `top_attrs`: see ParseInterface's comment on the same parameter.
  UnionDecl ParseUnion(const std::vector<ParsedAttribute>& top_attrs) {
    ExpectKeyword("union");
    UnionDecl u;
    for (const ParsedAttribute& a : top_attrs) {
      if (a.name == "Extensible" && !a.has_value) {
        u.is_extensible = true;
      } else if (a.name == "Extensible") {
        throw ParseError("'[Extensible]' doesn't take a value", a.line);
      } else {
        FillEnableIf(u.enable_if, a);
      }
    }
    u.name = Expect(TokenType::kName, "union name").text;
    Expect(TokenType::kLBrace, "'{'");
    bool any_explicit = false;
    bool any_implicit = false;
    uint32_t next_implicit = 0;
    while (!At(TokenType::kRBrace)) {
      UnionField f;
      if (At(TokenType::kLBracket)) {
        for (const ParsedAttribute& a : ParseAttributeList()) {
          if (a.name == "MinVersion" && a.has_value &&
              a.value_kind == ParsedAttribute::ValueKind::kInt) {
            if (a.value < 0) {
              throw ParseError("'[MinVersion=N]': N must be non-negative",
                                a.line);
            }
            f.min_version = static_cast<uint32_t>(a.value);
          } else if (a.name == "MinVersion") {
            throw ParseError("'[MinVersion=N]' requires an integer value",
                              a.line);
          } else if (a.name == "Default" && !a.has_value) {
            f.is_default = true;
          } else if (a.name == "Default") {
            throw ParseError("'[Default]' doesn't take a value", a.line);
          } else {
            FillEnableIf(f.enable_if, a);
          }
        }
      }
      f.type = ParseTypeSpec();
      f.name = Expect(TokenType::kName, "field name").text;
      if (At(TokenType::kAt)) {
        Advance();
        const Token& num = Expect(TokenType::kNumber, "tag number");
        f.tag = static_cast<uint32_t>(std::stoul(num.text));
        f.has_explicit_tag = true;
        any_explicit = true;
      } else {
        f.tag = next_implicit++;
        any_implicit = true;
      }
      Expect(TokenType::kSemi, "';' after field declaration");
      u.version = std::max(u.version, f.min_version);
      u.fields.push_back(std::move(f));
    }
    Expect(TokenType::kRBrace, "'}'");
    Expect(TokenType::kSemi, "';' after union body");
    if (any_explicit && any_implicit) {
      // Chromium mixes `@0` with untagged fields. Untagged fields take
      // the next ordinal that is not already used.
      std::unordered_set<uint32_t> used;
      for (const UnionField& f : u.fields) {
        if (f.has_explicit_tag) used.insert(f.tag);
      }
      uint32_t next = 0;
      for (UnionField& f : u.fields) {
        if (f.has_explicit_tag) continue;
        while (used.count(next)) ++next;
        f.tag = next;
        used.insert(next);
        ++next;
      }
    }
    if (u.fields.empty()) {
      throw ParseError("union '" + u.name + "': must have at least one field",
                        Cur().line);
    }
    int default_count = 0;
    for (const UnionField& f : u.fields) {
      if (f.is_default) ++default_count;
    }
    if (default_count > 1) {
      throw ParseError(
          "union '" + u.name + "': at most one field may be [Default]",
          Cur().line);
    }
    return u;
  }

  // By-value / optional / array<T,N> struct-or-union members need a
  // complete C++ type, so a cycle of those is uncompilable (no StructPtr
  // box). array<T> and map<K, T> may reference T while T is still
  // incomplete -- generated as WASMSafeSpace CageVector/CageMap -- so they
  // do not create a complete-type edge. Forward references (a field of a
  // type declared later in the file) are fine: the generator reorders.
  void CheckCompleteTypeAcyclic(const Module& mod) {
    std::unordered_set<std::string> local;
    auto add_struct = [&](const StructDecl& s) {
      local.insert(EmbedDeclKey(s.owner_container, s.name));
    };
    auto add_union = [&](const UnionDecl& u) {
      local.insert(EmbedDeclKey(u.owner_container, u.name));
    };
    for (const StructDecl& s : mod.structs) add_struct(s);
    for (const UnionDecl& u : mod.unions) add_union(u);
    for (const Interface& iface : mod.interfaces) {
      for (const StructDecl& s : iface.structs) add_struct(s);
      for (const UnionDecl& u : iface.unions) add_union(u);
    }

    std::unordered_map<std::string, std::vector<std::string>> outgoing;
    std::unordered_map<std::string, int> indeg;
    for (const std::string& n : local) indeg[n] = 0;

    auto add_owner = [&](const std::string& owner, const TypeSpec& type) {
      std::vector<std::string> deps;
      AppendCompleteTypeDeps(type, &deps);
      std::unordered_set<std::string> seen;
      for (const std::string& d : deps) {
        if (!local.count(d) || !seen.insert(d).second) continue;
        outgoing[d].push_back(owner);
        indeg[owner]++;
      }
    };
    auto add_struct_fields = [&](const StructDecl& s) {
      const std::string key = EmbedDeclKey(s.owner_container, s.name);
      for (const StructField& f : s.fields) add_owner(key, f.type);
    };
    auto add_union_fields = [&](const UnionDecl& u) {
      const std::string key = EmbedDeclKey(u.owner_container, u.name);
      for (const UnionField& f : u.fields) add_owner(key, f.type);
    };
    for (const StructDecl& s : mod.structs) add_struct_fields(s);
    for (const UnionDecl& u : mod.unions) add_union_fields(u);
    for (const Interface& iface : mod.interfaces) {
      for (const StructDecl& s : iface.structs) add_struct_fields(s);
      for (const UnionDecl& u : iface.unions) add_union_fields(u);
    }

    std::set<std::string> ready;
    for (const auto& [n, deg] : indeg) {
      if (deg == 0) ready.insert(n);
    }
    size_t seen = 0;
    while (!ready.empty()) {
      std::string n = *ready.begin();
      ready.erase(ready.begin());
      ++seen;
      for (const std::string& nxt : outgoing[n]) {
        if (--indeg[nxt] == 0) ready.insert(nxt);
      }
    }
    if (seen == local.size()) return;

    std::string cyc;
    for (const auto& [n, deg] : indeg) {
      if (deg > 0) {
        cyc = n;
        break;
      }
    }
    throw ParseError(
        "'" + cyc +
            "' embeds another struct/union by value in a cycle "
            "(direct member, optional, or array<T, N>) -- array<T> and "
            "map<K, T> may reference T while T is still incomplete "
            "(generated as WASMSafeSpace CageVector/CageMap)",
        Cur().line);
  }

  EnumDecl ParseEnum(const std::vector<ParsedAttribute>& top_attrs) {
    ExpectKeyword("enum");
    EnumDecl e;
    for (const ParsedAttribute& a : top_attrs) {
      if (a.name == "Extensible" && !a.has_value) {
        e.is_extensible = true;
      } else if (a.name == "Extensible") {
        throw ParseError("'[Extensible]' doesn't take a value", a.line);
      } else if (a.name == "Native" && !a.has_value) {
        e.is_native = true;
      } else if (a.name == "Native") {
        throw ParseError("'[Native]' doesn't take a value", a.line);
      } else {
        FillEnableIf(e.enable_if, a);
      }
    }
    e.name = Expect(TokenType::kName, "enum name").text;
    // (v23) `enum Foo;` -- real mojom's empty/native enum.
    if (At(TokenType::kSemi)) {
      Advance();
      return e;
    }
    Expect(TokenType::kLBrace, "'{'");
    int32_t next_value = 0;
    if (!At(TokenType::kRBrace)) {
      e.values.push_back(ParseEnumValue(e, &next_value));
      while (At(TokenType::kComma)) {
        Advance();
        if (At(TokenType::kRBrace)) break;  // trailing comma
        e.values.push_back(ParseEnumValue(e, &next_value));
      }
    }
    Expect(TokenType::kRBrace, "'}'");
    Expect(TokenType::kSemi, "';' after enum body");
    int default_count = 0;
    for (const EnumValue& v : e.values) {
      if (v.is_default) ++default_count;
    }
    if (default_count > 1) {
      throw ParseError(
          "enum '" + e.name + "': at most one value may be [Default]",
          Cur().line);
    }
    return e;
  }

  EnumValue ParseEnumValue(const EnumDecl& so_far, int32_t* next_value) {
    EnumValue v;
    if (At(TokenType::kLBracket)) {
      for (const ParsedAttribute& a : ParseAttributeList()) {
        if (a.name == "MinVersion" && a.has_value &&
            a.value_kind == ParsedAttribute::ValueKind::kInt) {
          if (a.value < 0) {
            throw ParseError("'[MinVersion=N]': N must be non-negative",
                              a.line);
          }
          v.min_version = static_cast<uint32_t>(a.value);
        } else if (a.name == "MinVersion") {
          throw ParseError("'[MinVersion=N]' requires an integer value",
                            a.line);
        } else if (a.name == "Default" && !a.has_value) {
          v.is_default = true;
        } else if (a.name == "Default") {
          throw ParseError("'[Default]' doesn't take a value", a.line);
        } else {
          FillEnableIf(v.enable_if, a);
        }
      }
    }
    v.name = Expect(TokenType::kName, "enum value name").text;
    if (At(TokenType::kEquals)) {
      Advance();
      if (At(TokenType::kName)) {
        // (v23) `kFoo = kBar` -- alias of an earlier value in this enum.
        std::string alias = Cur().text;
        int line = Cur().line;
        Advance();
        bool found = false;
        for (const EnumValue& prev : so_far.values) {
          if (prev.name == alias) {
            v.value = prev.value;
            found = true;
            break;
          }
        }
        if (!found) {
          throw ParseError(
              "enum value '" + v.name + "' aliases unknown value '" + alias +
                  "'",
              line);
        }
      } else {
        auto [negative, digits] = ParseSignedNumber();
        v.value = static_cast<int32_t>(ParseIntLiteral(negative, digits));
      }
    } else {
      v.value = *next_value;
    }
    *next_value = v.value + 1;
    return v;
  }

  // (real-mojom-parity phase 2) A const's type is parsed generically (via
  // ParseTypeSpec, so it can be a scalar keyword, `string`, or a
  // (possibly nested, possibly qualified) enum reference -- reusing all of
  // ParseTypeSpecInner's existing resolution) and then restricted to
  // IsLegalConstType's set (bool/integer/float/double/string/enum, never
  // nullable) -- parse permissively, then validate, same style as
  // CheckNotHandleBearing/CheckValidMapKeyType elsewhere in this file.
  // `value` is parsed the same way a struct field's default is (see
  // ParseDefaultValue) -- a const's value may itself reference an
  // earlier-declared const (bare or qualified `Container.NAME`) or, for an
  // enum-typed const, one of its enum's declared values.
  ConstDecl ParseConst() {
    ExpectKeyword("const");
    ConstDecl c;
    const Token& type_start = Cur();
    c.type = ParseTypeSpec();
    if (c.type.nullable || !IsLegalConstType(c.type.kind)) {
      throw ParseError(
          "const type must be bool/integer/float/double/string/enum "
          "(never nullable, struct/union/array/map/pending_*)",
          type_start.line);
    }
    c.name = Expect(TokenType::kName, "const name").text;
    Expect(TokenType::kEquals, "'=' in const declaration");
    c.value = ParseDefaultValue(c.type);
    Expect(TokenType::kSemi, "';' after const declaration");
    return c;
  }

  // (real-mojom-parity phase 7) `feature NAME { ... };` -- see
  // FeatureDecl's own comment in ast.h. The block's own attribute_list
  // (parsed by the caller, ParseBody -- same "attrs first, then
  // dispatch" reason struct/const's own block-level attrs never reach
  // ParseStruct/ParseConst either) is accepted and silently ignored, not
  // threaded through here. Real mojom's FeatureBody grammar only ever
  // allows `const` inside one -- confirmed against the fetched ast.py
  // during this project's own roadmap planning -- so anything else is a
  // real parse error naming exactly that, not a silent guess.
  FeatureDecl ParseFeature() {
    ExpectKeyword("feature");
    FeatureDecl f;
    f.name = Expect(TokenType::kName, "feature name").text;
    Expect(TokenType::kLBrace, "'{'");
    while (!At(TokenType::kRBrace)) {
      std::vector<ParsedAttribute> attrs;
      if (At(TokenType::kLBracket)) {
        attrs = ParseAttributeList();
      }
      if (!AtKeyword("const")) {
        throw ParseError(
            "feature '" + f.name +
                "': only 'const' declarations are allowed inside a "
                "'feature { ... }' body (matching real mojom's own "
                "FeatureBody grammar), got '" +
                (Cur().text.empty() ? "<eof>" : Cur().text) + "'",
            Cur().line);
      }
      ConstDecl c = ParseConst();
      for (const ParsedAttribute& a : attrs) FillEnableIf(c.enable_if, a);
      c.decl_index = next_const_order_++;
      const_container_[c.name] = f.name;
      const_by_name_[c.name] = c;
      f.consts.push_back(std::move(c));
    }
    Expect(TokenType::kRBrace, "'}'");
    Expect(TokenType::kSemi, "';' after feature body");
    return f;
  }

  // Parses one type_spec, including a trailing '?' if present (see
  // parser.h's grammar comment on nullable_type). The '?' check happens
  // here, uniformly, after ParseTypeSpecInner() -- so it applies whether
  // this call is parsing a struct field's type, an array's element, a
  // map's value, etc., and nests correctly for e.g. `array<string?>`.
  TypeSpec ParseTypeSpec() {
    TypeSpec t = ParseTypeSpecInner();
    if (At(TokenType::kQuestion)) {
      Advance();
      CheckNullableAllowed(t);
      t.nullable = true;
    }
    return t;
  }

  TypeSpec ParseTypeSpecInner() {
    const Token& name = Expect(TokenType::kName, "type name");
    if (name.text == "string") {
      return TypeSpec{TypeKind::kString, "", nullptr, nullptr, false};
    }
    if (name.text == "array") {
      Expect(TokenType::kLAngle, "'<'");
      TypeSpec element = ParseTypeSpec();
      // (v23) array elements may be handle-bearing, matching real mojom.
      // (real-mojom-parity phase 5) An optional `, N` -- a fixed-size
      // array. N must be a plain decimal literal (real mojom's
      // `INT_CONST_DEC`, never hex/float/a const reference) and strictly
      // positive -- a zero- or negative-length array has no sensible
      // std::array<T, N> spelling.
      int fixed_size = 0;
      if (At(TokenType::kComma)) {
        Advance();
        const Token& size_tok = Expect(TokenType::kNumber, "fixed array size");
        fixed_size = std::stoi(size_tok.text);
        if (fixed_size <= 0) {
          throw ParseError("fixed array size must be a positive integer",
                            size_tok.line);
        }
      }
      Expect(TokenType::kRAngle, "'>'");
      TypeSpec arr;
      arr.kind = TypeKind::kArray;
      arr.element = std::make_shared<TypeSpec>(std::move(element));
      arr.fixed_array_size = fixed_size;
      return arr;
    }
    if (name.text == "map" || name.text == "hash_map") {
      // (v23) `hash_map<K, V>` is real mojom's associative-array spelling
      // alongside `map<K, V>` -- both generate the same C++ unordered_map
      // (this compiler has no distinct hash-map representation).
      Expect(TokenType::kLAngle, "'<'");
      TypeSpec key = ParseTypeSpec();
      CheckValidMapKeyType(key);
      Expect(TokenType::kComma, "',' between map key and value types");
      TypeSpec value = ParseTypeSpec();
      Expect(TokenType::kRAngle, "'>'");
      TypeSpec m;
      m.kind = TypeKind::kMap;
      m.key = std::make_shared<TypeSpec>(std::move(key));
      m.element = std::make_shared<TypeSpec>(std::move(value));
      return m;
    }
    // `handle` (generic) or `handle<subtype>`. `platform` maps to
    // mojo::PlatformHandle (CadidumKernel wrapping Thunker's C
    // MojoPlatformHandle).
    if (name.text == "handle") {
      if (At(TokenType::kLAngle)) {
        Advance();
        const Token& subtype_tok =
            Expect(TokenType::kName, "handle subtype");
        Expect(TokenType::kRAngle, "'>'");
        if (subtype_tok.text == "message_pipe") {
          return TypeSpec{TypeKind::kHandleMessagePipe, "", nullptr, nullptr,
                           false};
        }
        if (subtype_tok.text == "data_pipe_consumer") {
          return TypeSpec{TypeKind::kHandleDataPipeConsumer, "", nullptr,
                           nullptr, false};
        }
        if (subtype_tok.text == "data_pipe_producer") {
          return TypeSpec{TypeKind::kHandleDataPipeProducer, "", nullptr,
                           nullptr, false};
        }
        if (subtype_tok.text == "shared_buffer") {
          return TypeSpec{TypeKind::kHandleSharedBuffer, "", nullptr,
                           nullptr, false};
        }
        if (subtype_tok.text == "platform" ||
            subtype_tok.text == "mach_send" ||
            subtype_tok.text == "mach_receive" ||
            subtype_tok.text == "mach_port") {
          return TypeSpec{TypeKind::kHandlePlatform, "", nullptr, nullptr,
                           false};
        }
        throw ParseError(
            "unsupported handle subtype '" + subtype_tok.text +
                "' -- this compiler supports 'message_pipe', "
                "'data_pipe_consumer', 'data_pipe_producer', "
                "'shared_buffer', 'platform', and 'mach_send'/"
                "'mach_receive'/'mach_port' (as handle<platform>)",
            subtype_tok.line);
      }
      return TypeSpec{TypeKind::kHandle, "", nullptr, nullptr, false};
    }
    // Chromium shorthand: `associated Foo` is pending_associated_remote<Foo>,
    // `associated Foo&` is pending_associated_receiver<Foo>.
    if (name.text == "associated") {
      const Token& iface_tok =
          Expect(TokenType::kName, "associated interface name");
      auto [qual, iface] = TakeQualifiedName(iface_tok);
      bool recv = false;
      if (At(TokenType::kAmpersand)) {
        Advance();
        recv = true;
      }
      TypeSpec t;
      t.kind = recv ? TypeKind::kPendingAssociatedReceiver
                    : TypeKind::kPendingAssociatedRemote;
      t.name = iface;
      t.owner_namespace = OwnerForQualified(qual, iface);
      return t;
    }
    auto pending_it = kPendingKeywords.find(name.text);
    if (pending_it != kPendingKeywords.end()) {
      Expect(TokenType::kLAngle, "'<'");
      const Token& iface_tok = Expect(TokenType::kName, "interface name");
      auto [qual, iface] = TakeQualifiedName(iface_tok);
      Expect(TokenType::kRAngle, "'>'");
      // (v15) An interface name is still accepted completely unvalidated,
      // same permissiveness as always (no declared-interfaces pre-scan --
      // unlike struct/union/enum, interfaces have never needed one, since
      // they're never ordering-checked). The only thing new is
      // owner_namespace: if this exact name happens to be something
      // already loaded from a prelude (owner_by_name_), it's qualified
      // with that file's namespace at codegen time; otherwise (declared in
      // this file, or not declared anywhere at all -- indistinguishable
      // here, same as before v15) it's left unqualified. A genuine
      // typo'd/unknown name still fails -- just later, as a real C++
      // compile error against the generated header.
      return TypeSpec{pending_it->second, iface, nullptr, nullptr, false,
                       OwnerForQualified(qual, iface)};
    }
    auto scalar_it = kScalarKeywords.find(name.text);
    if (scalar_it != kScalarKeywords.end()) {
      return TypeSpec{scalar_it->second, "", nullptr, nullptr, false};
    }
    // (v16) `NAME '.' NAME` -- originally just a qualified reference to a
    // nested enum declared inside the interface/struct named by the
    // first NAME. (real-mojom-parity phase 8) Generalized to
    // `NAME ('.' NAME)+` after a real vendored .mojom file
    // (services/device/public/mojom/geoposition.mojom's
    // `mojo_base.mojom.Time timestamp;` field) turned up real mojom's
    // other dotted-qualified-name use: a cross-file top-level type,
    // qualified by the declaring file's exact (possibly itself dotted)
    // `module` statement. Every segment after the first is collected
    // (there's no bound on how many real mojom allows -- a module
    // statement can itself be arbitrarily dotted, e.g. `module
    // mojo_base.mojom;`), then resolution tries, in order: (1) does the
    // *last* segment name something declared in another file whose raw
    // module statement exactly equals every segment before it, joined by
    // "." -- reusing the same owner_by_name_ map v15's plain cross-file
    // NAME resolution already populates, just with an explicit qualifier
    // check added; (2) only if there's exactly one dotted segment
    // (preserving the original v16 form exactly), the nested-enum-in-
    // this-file's-own-declared-container lookup. Both an unresolvable
    // qualifier and an unresolvable trailing name are real parse errors,
    // not a silent pass-through -- same philosophy the original v16 form
    // already had.
    if (At(TokenType::kDot)) {
      std::vector<Token> segs;
      while (At(TokenType::kDot)) {
        Advance();
        segs.push_back(Expect(TokenType::kName, "name after '.'"));
      }
      const Token& last = segs.back();
      std::string qualifier = name.text;
      for (size_t i = 0; i + 1 < segs.size(); ++i) {
        qualifier += "." + segs[i].text;
      }
      auto owner_it = owner_by_name_.find(last.text);
      const bool owner_ok = owner_it != owner_by_name_.end() &&
                             owner_it->second == qualifier;
      // `network.mojom.CoopAccessReportType` written in the file whose
      // own module is network.mojom. That name is not an import path.
      const bool self_ok =
          !module_name_.empty() && qualifier == module_name_ &&
          (owner_it == owner_by_name_.end() || owner_it->second.empty());
      if (owner_ok || self_ok) {
        const std::string ns = owner_ok ? qualifier : std::string();
        if (declared_.structs.count(last.text)) {
          return TypeSpec{TypeKind::kStructRef, last.text, nullptr, nullptr,
                           false, ns, StructContainerOf(last.text)};
        }
        if (declared_.unions.count(last.text)) {
          return TypeSpec{TypeKind::kUnionRef, last.text, nullptr, nullptr,
                           false, ns, UnionContainerOf(last.text)};
        }
        if (declared_.enums.count(last.text)) {
          return TypeSpec{TypeKind::kEnumRef, last.text, nullptr, nullptr,
                           false, ns, ContainerOf(last.text)};
        }
      }
      if (segs.size() == 1) {
        const std::unordered_set<std::string>* nested =
            ContainerEnums(name.text);
        if (nested && nested->count(last.text)) {
          return TypeSpec{TypeKind::kEnumRef, last.text, nullptr, nullptr,
                           false, OwnerOf(name.text), name.text};
        }
        const std::unordered_set<std::string>* nstructs =
            ContainerStructs(name.text);
        if (nstructs && nstructs->count(last.text)) {
          return TypeSpec{TypeKind::kStructRef, last.text, nullptr, nullptr,
                           false, OwnerOf(name.text), name.text};
        }
        const std::unordered_set<std::string>* nunions =
            ContainerUnions(name.text);
        if (nunions && nunions->count(last.text)) {
          return TypeSpec{TypeKind::kUnionRef, last.text, nullptr, nullptr,
                           false, OwnerOf(name.text), name.text};
        }
        return ExternalType(qualifier, last.text);
      }
      return ExternalType(qualifier, last.text);
    }
    if (declared_.structs.count(name.text)) {
      return TypeSpec{TypeKind::kStructRef, name.text, nullptr, nullptr,
                       false, OwnerOf(name.text), StructContainerOf(name.text)};
    }
    if (declared_.unions.count(name.text)) {
      return TypeSpec{TypeKind::kUnionRef, name.text, nullptr, nullptr,
                       false, OwnerOf(name.text), UnionContainerOf(name.text)};
    }
    if (declared_.enums.count(name.text)) {
      return TypeSpec{TypeKind::kEnumRef, name.text, nullptr, nullptr, false,
                       OwnerOf(name.text), ContainerOf(name.text)};
    }
    // Missing import, or a type stripped by [EnableIf] (use_blink,
    // is_chromeos, is_win). The name is still a real mojom type.
    return ExternalType("", name.text);
  }

  TypeSpec ExternalType(const std::string& qual, const std::string& type_name) {
    if (declared_.structs.count(type_name)) {
      const std::string ns =
          local_names_.count(type_name) ? std::string() : qual;
      return TypeSpec{TypeKind::kStructRef, type_name, nullptr, nullptr, false,
                       ns, StructContainerOf(type_name)};
    }
    if (declared_.unions.count(type_name)) {
      const std::string ns =
          local_names_.count(type_name) ? std::string() : qual;
      return TypeSpec{TypeKind::kUnionRef, type_name, nullptr, nullptr, false,
                       ns, UnionContainerOf(type_name)};
    }
    if (declared_.enums.count(type_name)) {
      const std::string ns =
          local_names_.count(type_name) ? std::string() : qual;
      return TypeSpec{TypeKind::kEnumRef, type_name, nullptr, nullptr, false,
                       ns, ContainerOf(type_name)};
    }
    return TypeSpec{TypeKind::kStructRef, type_name, nullptr, nullptr, false,
                     qual};
  }

  // `Name ('.' Name)*` with `first` already consumed. Qualifier is every
  // segment except the last, joined by '.'. Empty qualifier means a bare name.
  std::pair<std::string, std::string> TakeQualifiedName(const Token& first) {
    std::vector<std::string> segs;
    segs.push_back(first.text);
    while (At(TokenType::kDot)) {
      Advance();
      segs.push_back(Expect(TokenType::kName, "name after '.'").text);
    }
    if (segs.size() == 1) return {"", segs[0]};
    std::string qual = segs[0];
    for (size_t i = 1; i + 1 < segs.size(); ++i) qual += "." + segs[i];
    return {qual, segs.back()};
  }

  // A same-module qualifier (`network.mojom.Foo` inside module network.mojom)
  // stays unqualified in this file's header. A foreign qualifier is kept so
  // codegen can spell the other namespace.
  std::string OwnerForQualified(const std::string& qual,
                                const std::string& name) const {
    if (qual.empty() || qual == module_name_) return OwnerOf(name);
    return qual;
  }

  // "" if `name` was declared in the file being parsed right now, else the
  // raw module statement of whichever earlier-loaded file actually declared
  // it -- see owner_by_name_'s comment above.
  std::string OwnerOf(const std::string& name) const {
    if (local_names_.count(name)) return "";
    auto it = owner_by_name_.find(name);
    return it == owner_by_name_.end() ? std::string() : it->second;
  }

  const EnumDecl* EnumForDefault(const TypeSpec& field_type) const {
    if (field_type.owner_namespace.empty()) {
      auto local = local_enum_values_.find(field_type.name);
      if (local != local_enum_values_.end()) return &local->second;
    }
    auto it = enum_values_.find(field_type.name);
    return it == enum_values_.end() ? nullptr : &it->second;
  }

  // (v16) "" if `name` (an enum already confirmed to be in declared_.enums)
  // is top-level, else the interface/struct it's nested inside -- checks
  // this file's own pre-scan (declared_.enum_container) before the
  // prelude-seeded one (container_by_name_), mirroring OwnerOf.
  std::string ContainerOf(const std::string& name) const {
    auto it = declared_.enum_container.find(name);
    if (it != declared_.enum_container.end()) return it->second;
    auto pit = container_by_name_.find(name);
    return pit == container_by_name_.end() ? std::string() : pit->second;
  }

  // (v16) nullptr if `container` isn't a known interface/struct with any
  // nested enums (this file's own pre-scan, then prelude-seeded); else the
  // set of its nested enum names, for the qualified `Container.EnumName'
  // lookup above.
  const std::unordered_set<std::string>* ContainerEnums(
      const std::string& container) const {
    auto it = declared_.container_enums.find(container);
    if (it != declared_.container_enums.end()) return &it->second;
    auto pit = container_enums_.find(container);
    return pit == container_enums_.end() ? nullptr : &pit->second;
  }

  std::string StructContainerOf(const std::string& name) const {
    auto it = declared_.struct_container.find(name);
    return it == declared_.struct_container.end() ? std::string() : it->second;
  }
  std::string UnionContainerOf(const std::string& name) const {
    auto it = declared_.union_container.find(name);
    return it == declared_.union_container.end() ? std::string() : it->second;
  }
  const std::unordered_set<std::string>* ContainerStructs(
      const std::string& container) const {
    auto it = declared_.container_structs.find(container);
    return it == declared_.container_structs.end() ? nullptr : &it->second;
  }
  const std::unordered_set<std::string>* ContainerUnions(
      const std::string& container) const {
    auto it = declared_.container_unions.find(container);
    return it == declared_.container_unions.end() ? nullptr : &it->second;
  }

  // mojom gives "reference" types a meaningful null/absent state: string,
  // array<T>, map<K, V>, struct/union, and (v14) the four handle-bearing
  // pending_* kinds -- each of those already has a real invalid/default
  // state on the C++ side (PendingRemote<T>::is_valid(), etc.), so a
  // nullable one is represented as that same type, no std::optional
  // wrapper needed; see CppValueType and EmitTopWrite*/EmitTopReadParam
  // in cpp_generator.cc for the presence-flag wire framing this adds.
  // Enums and scalars are std::optional<T> (IntegrityAlgorithm?, uint32?).
  void CheckNullableAllowed(const TypeSpec& t) {
    switch (t.kind) {
      case TypeKind::kEnumRef:
      case TypeKind::kBool:
      case TypeKind::kInt8:
      case TypeKind::kUint8:
      case TypeKind::kInt16:
      case TypeKind::kUint16:
      case TypeKind::kInt32:
      case TypeKind::kUint32:
      case TypeKind::kInt64:
      case TypeKind::kUint64:
      case TypeKind::kFloat:
      case TypeKind::kDouble:
        return;
      case TypeKind::kString:
      case TypeKind::kArray:
      case TypeKind::kMap:
      case TypeKind::kStructRef:
      case TypeKind::kUnionRef:
      case TypeKind::kPendingRemote:
      case TypeKind::kPendingReceiver:
      case TypeKind::kPendingAssociatedRemote:
      case TypeKind::kPendingAssociatedReceiver:
      // (real-mojom-parity phase 4) Every handle type has the same
      // meaningful "absent" state (ScopedHandleBase::is_valid()) pending_*
      // already relies on for its own nullable framing -- see
      // cpp_generator.cc's EmitTopWritePrelude/EmitTopWritePayload/
      // EmitTopReadParam, which treat these identically to pending_*.
      case TypeKind::kHandle:
      case TypeKind::kHandleMessagePipe:
      case TypeKind::kHandleDataPipeConsumer:
      case TypeKind::kHandleDataPipeProducer:
      case TypeKind::kHandleSharedBuffer:
      case TypeKind::kHandlePlatform:
        return;
      default:
        throw ParseError(
            "'?' is only allowed on string/array/map/struct/union/enum/"
            "scalar/pending_*/handle types",
            Cur().line);
    }
  }

  // Map keys are bool/integer/string/enum, plus struct/union (Chromium
  // map<SchemefulSite, ...>). Never float/double, array/map, pending_*,
  // and never nullable (a key can't be absent).
  void CheckValidMapKeyType(const TypeSpec& key) {
    if (key.nullable) {
      throw ParseError("map key type cannot be nullable", Cur().line);
    }
    switch (key.kind) {
      case TypeKind::kString:
      case TypeKind::kEnumRef:
      case TypeKind::kStructRef:
      case TypeKind::kUnionRef:
      case TypeKind::kBool:
      case TypeKind::kInt8:
      case TypeKind::kUint8:
      case TypeKind::kInt16:
      case TypeKind::kUint16:
      case TypeKind::kInt32:
      case TypeKind::kUint32:
      case TypeKind::kInt64:
      case TypeKind::kUint64:
        return;
      default:
        throw ParseError(
            "map key type must be bool/integer/string/enum/struct/union "
            "(never float/double/array/map/pending_*)",
            Cur().line);
    }
  }
};

Parser::Parser(const std::vector<Token>& tokens)
    : impl_(std::make_unique<Impl>(tokens)) {}
Parser::~Parser() = default;
Parser::Parser(Parser&&) noexcept = default;
Parser& Parser::operator=(Parser&&) noexcept = default;

FileHeader Parser::ParseHeader() { return impl_->ParseHeader(); }

Module Parser::ParseBody(const Prelude* prelude) {
  return impl_->ParseBody(prelude);
}

Module Parse(const std::vector<Token>& tokens) {
  Parser p(tokens);
  FileHeader header = p.ParseHeader();
  Module mod = p.ParseBody(nullptr);
  mod.name = header.module_name;
  mod.imports = std::move(header.imports);
  return mod;
}

namespace {

bool ClauseMatches(const EnableIfClause& clause,
                   const std::unordered_set<std::string>& enabled,
                   bool negate) {
  if (clause.mode == EnableIfClause::Mode::kNone) return true;
  bool hit = false;
  if (clause.mode == EnableIfClause::Mode::kAny) {
    for (const std::string& f : clause.flags) {
      if (enabled.count(f)) {
        hit = true;
        break;
      }
    }
  } else {
    hit = !clause.flags.empty();
    for (const std::string& f : clause.flags) {
      if (!enabled.count(f)) {
        hit = false;
        break;
      }
    }
  }
  return negate ? !hit : hit;
}

template <typename T>
void EraseDisabled(std::vector<T>* v,
                   const std::unordered_set<std::string>& enabled) {
  v->erase(std::remove_if(v->begin(), v->end(),
                          [&](const T& x) {
                            return !IsDeclEnabled(x.enable_if, enabled);
                          }),
           v->end());
}

void FilterEnum(EnumDecl& e, const std::unordered_set<std::string>& enabled) {
  EraseDisabled(&e.values, enabled);
}

void FilterStruct(StructDecl& s,
                  const std::unordered_set<std::string>& enabled) {
  EraseDisabled(&s.fields, enabled);
  EraseDisabled(&s.enums, enabled);
  EraseDisabled(&s.consts, enabled);
  for (EnumDecl& e : s.enums) FilterEnum(e, enabled);
  s.version = 0;
  for (const StructField& f : s.fields) {
    s.version = std::max(s.version, f.min_version);
  }
}

void FilterUnion(UnionDecl& u,
                 const std::unordered_set<std::string>& enabled) {
  EraseDisabled(&u.fields, enabled);
  u.version = 0;
  for (const UnionField& f : u.fields) {
    u.version = std::max(u.version, f.min_version);
  }
}

void FilterInterface(Interface& iface,
                     const std::unordered_set<std::string>& enabled) {
  EraseDisabled(&iface.methods, enabled);
  EraseDisabled(&iface.enums, enabled);
  EraseDisabled(&iface.consts, enabled);
  EraseDisabled(&iface.structs, enabled);
  EraseDisabled(&iface.unions, enabled);
  for (EnumDecl& e : iface.enums) FilterEnum(e, enabled);
  for (StructDecl& s : iface.structs) FilterStruct(s, enabled);
  for (UnionDecl& u : iface.unions) FilterUnion(u, enabled);
  iface.unions.erase(std::remove_if(iface.unions.begin(), iface.unions.end(),
                                    [](const UnionDecl& u) {
                                      return u.fields.empty();
                                    }),
                     iface.unions.end());
  iface.version = 0;
  for (const Method& m : iface.methods) {
    iface.version = std::max(iface.version, m.min_version);
  }
}

}  // namespace

bool IsDeclEnabled(const EnableIf& spec,
                   const std::unordered_set<std::string>& enabled) {
  return ClauseMatches(spec.enable_if, enabled, false) &&
         ClauseMatches(spec.enable_if_not, enabled, true);
}

void ApplyEnableIf(Module& module,
                   const std::unordered_set<std::string>& enabled) {
  EraseDisabled(&module.interfaces, enabled);
  EraseDisabled(&module.structs, enabled);
  EraseDisabled(&module.unions, enabled);
  EraseDisabled(&module.enums, enabled);
  EraseDisabled(&module.consts, enabled);
  EraseDisabled(&module.features, enabled);
  for (Interface& iface : module.interfaces) {
    FilterInterface(iface, enabled);
  }
  for (StructDecl& s : module.structs) FilterStruct(s, enabled);
  for (UnionDecl& u : module.unions) FilterUnion(u, enabled);
  module.unions.erase(
      std::remove_if(module.unions.begin(), module.unions.end(),
                     [](const UnionDecl& u) { return u.fields.empty(); }),
      module.unions.end());
  for (EnumDecl& e : module.enums) FilterEnum(e, enabled);
  for (FeatureDecl& ft : module.features) {
    EraseDisabled(&ft.consts, enabled);
  }
}

}  // namespace voodoom
