// Proves brujac's RmlUi JIT IDL (RmlElement / RmlDocument / RmlUi)
// against quickjs-ng. Binding proof only — after-Lime implements the same
// contract with Rml::Factory::RegisterElementInstancer and PinPageRoot
// (see WASMUniLoader/cpp/display/rml.cc).
#include "rml_gen.h"

#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>

namespace {

class ElementImpl : public rml_generated::RmlElement {
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
  rml_generated::RmlElement* GetChild(uint32_t index) override {
    return index < children_.size() ? children_[index] : nullptr;
  }
  rml_generated::RmlElement* GetElementById(const std::string& id) override {
    if (id_ == id) return this;
    for (auto* c : children_) {
      if (rml_generated::RmlElement* f = c->GetElementById(id)) return f;
    }
    return nullptr;
  }
  std::string TagName() override { return tag_; }
  std::string Id() override { return id_; }
  void SetId(const std::string& value) override { id_ = value; }
  std::string ClassName() override { return class_; }
  void SetClassName(const std::string& value) override { class_ = value; }
  rml_generated::RmlElement* ParentElement() override { return parent_; }
  uint32_t ChildCount() override { return static_cast<uint32_t>(children_.size()); }

  void AddChild(ElementImpl* child) {
    child->parent_ = this;
    children_.push_back(child);
  }

 private:
  std::string tag_, id_, class_;
  ElementImpl* parent_ = nullptr;
  std::vector<ElementImpl*> children_;
  std::map<std::string, std::string> props_, attrs_;
};

class DocumentImpl : public rml_generated::RmlDocument {
 public:
  DocumentImpl() : root_("body") {}

  void Show() override { shown_ = true; }
  void Update() override { updates_++; }
  bool Shown() const { return shown_; }
  int Updates() const { return updates_; }

  bool SetProperty(const std::string& name, const std::string& value) override {
    return root_.SetProperty(name, value);
  }
  std::string GetProperty(const std::string& name) override { return root_.GetProperty(name); }
  void RemoveProperty(const std::string& name) override { root_.RemoveProperty(name); }
  bool HasAttribute(const std::string& name) override { return root_.HasAttribute(name); }
  std::string GetAttribute(const std::string& name) override { return root_.GetAttribute(name); }
  void SetAttribute(const std::string& name, const std::string& value) override {
    root_.SetAttribute(name, value);
  }
  rml_generated::RmlElement* GetChild(uint32_t index) override { return root_.GetChild(index); }
  rml_generated::RmlElement* GetElementById(const std::string& id) override {
    return root_.GetElementById(id);
  }
  std::string TagName() override { return root_.TagName(); }
  std::string Id() override { return root_.Id(); }
  void SetId(const std::string& value) override { root_.SetId(value); }
  std::string ClassName() override { return root_.ClassName(); }
  void SetClassName(const std::string& value) override { root_.SetClassName(value); }
  rml_generated::RmlElement* ParentElement() override { return nullptr; }
  uint32_t ChildCount() override { return root_.ChildCount(); }

  ElementImpl* Root() { return &root_; }

 private:
  ElementImpl root_;
  bool shown_ = false;
  int updates_ = 0;
};

class RmlUiImpl : public rml_generated::RmlUi {
 public:
  void RegisterElement(const std::string& tagName) override {
    if (!tagName.empty()) tags_.insert(tagName);
  }
  void RegisterHtmlElements() override {
    const char* tags[] = {
        "div",  "nav",   "header", "section", "article", "main",  "aside",  "footer", "figure",
        "span", "p",     "h1",     "h2",      "h3",      "ul",    "ol",     "li",     "a",
        "img",  "table", "tr",     "td",      "th",      "thead", "tbody",  "button", "input",
    };
    for (const char* t : tags) tags_.insert(t);
  }
  void RegisterDomain(const std::string& domain) override {
    if (!domain.empty()) domains_.insert(domain);
  }
  void PinPageRoot(rml_generated::RmlElement* viewport) override {
    pinned_ = viewport;
    if (!viewport) return;
    rml_generated::RmlElement* page = viewport->GetElementById("page-root");
    if (!page) return;
    page->SetProperty("display", "block");
    page->SetProperty("width", "100%");
    page->SetProperty("background-color", "#ffffff");
  }

  rml_generated::RmlElement* Pinned() const { return pinned_; }
  bool HasTag(const std::string& tag) const { return tags_.count(tag) > 0; }
  bool HasDomain(const std::string& domain) const { return domains_.count(domain) > 0; }

 private:
  std::set<std::string> tags_;
  std::set<std::string> domains_;
  rml_generated::RmlElement* pinned_ = nullptr;
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

  RmlUiImpl ui;
  DocumentImpl doc;
  ElementImpl viewport("div");
  ElementImpl page("div");
  viewport.SetId("viewport");
  page.SetId("page-root");
  viewport.AddChild(&page);

  JSValue global = JS_GetGlobalObject(ctx);
  JS_SetPropertyStr(ctx, global, "rmlUi", rml_generated::CreateRmlUiBinding(ctx, &ui));
  JS_SetPropertyStr(ctx, global, "doc", rml_generated::CreateRmlDocumentBinding(ctx, &doc));
  JS_SetPropertyStr(ctx, global, "viewport", rml_generated::CreateRmlElementBinding(ctx, &viewport));
  JS_FreeValue(ctx, global);

  int failures = 0;

  if (!Eval(ctx, "rmlUi.registerHtmlElements();")) {
    std::fprintf(stderr, "FAIL: registerHtmlElements threw\n");
    ++failures;
  }
  if (!ui.HasTag("nav") || !ui.HasTag("header")) {
    std::fprintf(stderr, "FAIL: HTML5 tags not registered\n");
    ++failures;
  }
  if (!Eval(ctx, "rmlUi.registerElement('custom-widget');")) {
    std::fprintf(stderr, "FAIL: registerElement threw\n");
    ++failures;
  }
  if (!ui.HasTag("custom-widget")) {
    std::fprintf(stderr, "FAIL: custom tag not registered\n");
    ++failures;
  }

  if (!Eval(ctx, "rmlUi.registerDomain('html'); rmlUi.registerDomain('svg');")) {
    std::fprintf(stderr, "FAIL: registerDomain threw\n");
    ++failures;
  }
  if (!ui.HasDomain("html") || !ui.HasDomain("svg")) {
    std::fprintf(stderr, "FAIL: domains not registered\n");
    ++failures;
  }

  if (!Eval(ctx, "doc.show(); doc.update();")) {
    std::fprintf(stderr, "FAIL: document lifecycle threw\n");
    ++failures;
  }
  if (!doc.Shown() || doc.Updates() != 1) {
    std::fprintf(stderr, "FAIL: show/update not called\n");
    ++failures;
  }

  if (!Eval(ctx, "rmlUi.pinPageRoot(viewport);")) {
    std::fprintf(stderr, "FAIL: pinPageRoot threw\n");
    ++failures;
  }
  if (ui.Pinned() != &viewport) {
    std::fprintf(stderr, "FAIL: pinPageRoot did not receive viewport\n");
    ++failures;
  }
  if (page.GetProperty("background-color") != "#ffffff") {
    std::fprintf(stderr, "FAIL: page-root not pinned: %s\n", page.GetProperty("background-color").c_str());
    ++failures;
  }

  std::string id = EvalString(ctx, "viewport.getElementById('page-root').id");
  if (id != "page-root") {
    std::fprintf(stderr, "FAIL: getElementById = %s\n", id.c_str());
    ++failures;
  }

  JS_FreeContext(ctx);
  JS_FreeRuntime(rt);

  if (failures) {
    std::fprintf(stderr, "rml_roundtrip: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("rml_roundtrip: OK\n");
  return 0;
}
