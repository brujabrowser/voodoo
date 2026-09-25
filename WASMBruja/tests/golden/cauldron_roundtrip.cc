// Proves brujac's Cauldron IDL (RmlStyleSheet / RmlElement / Cauldron)
// against a real quickjs-ng context. This is the binding proof, not the
// RmlUi paint path -- after-Lime implements the same contract with
// Rml::StyleSheetSpecification::RegisterProperty and Rml::Element::SetProperty
// (see WASMUniLoader/cpp/display/cauldron.cc).
#include "cauldron_gen.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace {

class SheetImpl : public cauldron_generated::RmlStyleSheet {
 public:
  bool RegisterProperty(const cauldron_generated::PropertySpec& spec) override {
    if (spec.name.empty()) return false;
    names_.insert(spec.name);
    return true;
  }
  bool HasProperty(const std::string& name) override { return names_.count(name) > 0; }

 private:
  std::set<std::string> names_;
};

class ElementImpl : public cauldron_generated::RmlElement {
 public:
  explicit ElementImpl(std::string tag) : tag_(std::move(tag)) {}

  bool SetProperty(const std::string& name, const std::string& value) override {
    props_[name] = value;
    return true;
  }
  std::string GetProperty(const std::string& name) override {
    auto it = props_.find(name);
    return it == props_.end() ? std::string() : it->second;
  }
  void RemoveProperty(const std::string& name) override { props_.erase(name); }
  bool HasAttribute(const std::string& name) override { return attrs_.count(name) > 0; }
  std::string GetAttribute(const std::string& name) override {
    auto it = attrs_.find(name);
    return it == attrs_.end() ? std::string() : it->second;
  }
  void SetAttribute(const std::string& name, const std::string& value) override { attrs_[name] = value; }
  cauldron_generated::RmlElement* GetChild(uint32_t) override { return nullptr; }
  cauldron_generated::RmlElement* GetElementById(const std::string& id) override {
    return id_ == id ? this : nullptr;
  }
  std::string TagName() override { return tag_; }
  std::string Id() override { return id_; }
  void SetId(const std::string& value) override { id_ = value; }
  std::string ClassName() override { return class_; }
  void SetClassName(const std::string& value) override { class_ = value; }
  cauldron_generated::RmlElement* ParentElement() override { return nullptr; }
  uint32_t ChildCount() override { return 0; }

 private:
  std::string tag_, id_, class_;
  std::map<std::string, std::string> props_, attrs_;
};

class CauldronImpl : public cauldron_generated::Cauldron {
 public:
  cauldron_generated::RmlStyleSheet* StyleSheet() override { return &sheet_; }
  void RegisterWebProperties() override {
    const char* names[] = {
        "-uni-display",           "grid-template-columns", "grid-template-rows",
        "grid-auto-flow",         "grid-column",           "grid-row",
        "place-items",            "appearance",
    };
    for (const char* n : names) {
      cauldron_generated::PropertySpec spec;
      spec.name = n;
      spec.forcesLayout = true;
      spec.parser = "string";
      sheet_.RegisterProperty(spec);
    }
  }
  std::string TranslateCSS(const std::string& css) override {
    // Stub of the after-Lime translator: drop @charset (RmlUi's parser
    // aborts on `@charset ...; @layer`) and rewrite display:grid so
    // RegisterProperty('-uni-display') has something to read.
    std::string out = css;
    const char* charset = "@charset";
    size_t at = out.find(charset);
    if (at != std::string::npos) {
      size_t semi = out.find(';', at);
      if (semi != std::string::npos) out.erase(at, semi + 1 - at);
    }
    const char* from = "display:grid";
    const char* to = "display:flex;-uni-display:grid";
    size_t g = out.find(from);
    if (g != std::string::npos) out.replace(g, std::strlen(from), to);
    return out;
  }
  std::string TranslateXCSS(const std::string& xcss) override { return TranslateCSS(xcss); }
  std::string TranslateHTML(const std::string& html) override {
    std::string out = html;
    const char* from = "<header";
    size_t p = out.find(from);
    if (p != std::string::npos) out.replace(p, 7, "<div");
    p = out.find("</header>");
    if (p != std::string::npos) out.replace(p, 9, "</div>");
    p = out.find("<input");
    if (p != std::string::npos) {
      size_t gt = out.find('>', p);
      if (gt != std::string::npos && out[gt - 1] != '/') out.insert(gt, " /");
    }
    return out;
  }
  std::string TranslateXHTML(const std::string& xml) override { return TranslateHTML(xml); }
  std::string TranslateGML(const std::string& gml) override {
    // Binding proof: emit a well-formed RML snippet from intent GML.
    if (gml.find("h1(") != std::string::npos) return "<article id=\"page-root\"><h1>Hi</h1></article>";
    return "<div />";
  }
  std::string Cascade(const std::string& html, float, float) override {
    return TranslateHTML(TranslateCSS(html));
  }
  void ApplyPage(cauldron_generated::RmlElement* root) override {
    applied_ = root;
    if (root) root->SetProperty("display", "block");
  }
  cauldron_generated::RmlElement* applied() const { return applied_; }

 private:
  SheetImpl sheet_;
  cauldron_generated::RmlElement* applied_ = nullptr;
};

bool Eval(JSContext* ctx, const char* source) {
  JSValue result = JS_Eval(ctx, source, std::strlen(source), "<test>", JS_EVAL_TYPE_GLOBAL);
  if (JS_IsException(result)) {
    JSValue exc = JS_GetException(ctx);
    const char* msg = JS_ToCString(ctx, exc);
    std::fprintf(stderr, "eval error: %s\n", msg ? msg : "(unknown)");
    if (msg) JS_FreeCString(ctx, msg);
    JS_FreeValue(ctx, exc);
    JS_FreeValue(ctx, result);
    return false;
  }
  JS_FreeValue(ctx, result);
  return true;
}

std::string EvalString(JSContext* ctx, const char* source) {
  JSValue result = JS_Eval(ctx, source, std::strlen(source), "<test>", JS_EVAL_TYPE_GLOBAL);
  const char* cstr = JS_ToCString(ctx, result);
  std::string out = cstr ? cstr : "";
  if (cstr) JS_FreeCString(ctx, cstr);
  JS_FreeValue(ctx, result);
  return out;
}

}  // namespace

int main() {
  JSRuntime* rt = JS_NewRuntime();
  JSContext* ctx = JS_NewContext(rt);

  CauldronImpl pot;
  ElementImpl root("div");
  root.SetId("page-root");

  JSValue global = JS_GetGlobalObject(ctx);
  JS_SetPropertyStr(ctx, global, "cauldron",
                    cauldron_generated::CreateCauldronBinding(ctx, &pot));
  JS_SetPropertyStr(ctx, global, "root",
                    cauldron_generated::CreateRmlElementBinding(ctx, &root));
  JS_FreeValue(ctx, global);

  int failures = 0;

  if (!Eval(ctx, "cauldron.registerWebProperties();")) {
    std::fprintf(stderr, "FAIL: registerWebProperties threw\n");
    ++failures;
  }
  std::string has = EvalString(ctx, "String(cauldron.styleSheet.hasProperty('grid-template-columns'))");
  if (has != "true") {
    std::fprintf(stderr, "FAIL: grid-template-columns not registered: %s\n", has.c_str());
    ++failures;
  }
  std::string uni = EvalString(ctx, "String(cauldron.styleSheet.hasProperty('-uni-display'))");
  if (uni != "true") {
    std::fprintf(stderr, "FAIL: -uni-display not registered: %s\n", uni.c_str());
    ++failures;
  }

  std::string css = EvalString(
      ctx, "cauldron.translateCSS('@charset \"utf-8\";x{display:grid;width:60vw}')");
  if (css.find("charset") != std::string::npos) {
    std::fprintf(stderr, "FAIL: @charset survived: %s\n", css.c_str());
    ++failures;
  }
  if (css.find("display:flex") == std::string::npos || css.find("-uni-display:grid") == std::string::npos) {
    std::fprintf(stderr, "FAIL: display:grid not rewritten: %s\n", css.c_str());
    ++failures;
  }
  if (css.find("60vw") == std::string::npos) {
    std::fprintf(stderr, "FAIL: vw was neutralized: %s\n", css.c_str());
    ++failures;
  }

  std::string html = EvalString(
      ctx, "cauldron.translateHTML('<header id=\"h\">x</header><input type=hidden>')");
  if (html.find("<div") == std::string::npos || html.find("</div>") == std::string::npos) {
    std::fprintf(stderr, "FAIL: header not retagged: %s\n", html.c_str());
    ++failures;
  }
  if (html.find(" /") == std::string::npos && html.find("/>") == std::string::npos) {
    std::fprintf(stderr, "FAIL: void input not closed: %s\n", html.c_str());
    ++failures;
  }
  std::string xhtml = EvalString(ctx, "cauldron.translateXHTML('<nav>n</nav>')");
  if (xhtml.find("<nav>") == std::string::npos && xhtml.find("<div") == std::string::npos) {
    std::fprintf(stderr, "FAIL: translateXHTML empty: %s\n", xhtml.c_str());
    ++failures;
  }
  std::string gml = EvalString(ctx, "cauldron.translateGML('html(body(h1(\"Hi\")))')");
  if (gml.find("<h1>") == std::string::npos && gml.find("page-root") == std::string::npos) {
    std::fprintf(stderr, "FAIL: translateGML: %s\n", gml.c_str());
    ++failures;
  }
  std::string xcss = EvalString(ctx, "cauldron.translateXCSS('x{display:grid}')");
  if (xcss.find("display:flex") == std::string::npos) {
    std::fprintf(stderr, "FAIL: translateXCSS: %s\n", xcss.c_str());
    ++failures;
  }
  std::string cascaded = EvalString(
      ctx, "cauldron.cascade('@charset \"utf-8\";<header>x</header>', 800, 600)");
  if (cascaded.find("charset") != std::string::npos || cascaded.find("<header") != std::string::npos) {
    std::fprintf(stderr, "FAIL: cascade: %s\n", cascaded.c_str());
    ++failures;
  }

  if (!Eval(ctx, "cauldron.applyPage(root);")) {
    std::fprintf(stderr, "FAIL: applyPage threw\n");
    ++failures;
  }
  if (pot.applied() != &root) {
    std::fprintf(stderr, "FAIL: applyPage did not receive root\n");
    ++failures;
  }
  if (root.GetProperty("display") != "block") {
    std::fprintf(stderr, "FAIL: applyPage did not SetProperty display\n");
    ++failures;
  }

  std::string id = EvalString(ctx, "root.id");
  if (id != "page-root") {
    std::fprintf(stderr, "FAIL: root.id = %s\n", id.c_str());
    ++failures;
  }

  JS_FreeContext(ctx);
  JS_FreeRuntime(rt);

  if (failures) {
    std::fprintf(stderr, "cauldron_roundtrip: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("cauldron_roundtrip: OK\n");
  return 0;
}
