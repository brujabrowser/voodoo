// Proves brujac's HTML5-parity grammar/codegen -- interface inheritance,
// nullable/interface/sequence/enum/dictionary/callback types, optional/
// variadic params, interface mixins + `includes`, `constructor(...)`, and
// `Promise<T>` (see examples/dom/dom.bruja's header comment) -- against a
// real quickjs-ng context, the same way console/navigator/
// processes_roundtrip.cc prove v1/v2.
//
// Impl classes below use small `*ImplT<Base>` templates layering
// EventTarget -> Node -> Element -> HTMLElement -> HTMLMediaElement
// behavior onto whichever generated leaf class `Base` is, so each
// concrete leaf only has to add its own extra attributes. Node's
// appendChild deliberately does NOT wire up the child's reverse
// `parentNode` pointer automatically (that would require downcasting
// through the abstract `Node*` the interface hands back, which the
// interface itself doesn't support -- see cpp_generator.h's "declared
// type" note); parent pointers below are wired directly in test setup
// instead. The same limitation means a handful of mixin methods that
// would need to walk "the parent's children list" through an abstract
// `Node*` (QuerySelector(All), Matches, Closest,
// previous/nextElementSibling) are left as documented stubs -- proving
// the grammar/binding plumbing (right name, right signature, callable,
// right return type) without a full selector engine or sibling-list
// machinery, consistent with this test's existing simplifications
// (childNodes-as-snapshot, no real selector matching in createElement).
#include "dom_gen.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {
using namespace bruja_dom_generated;

std::vector<Element*> ElementChildrenOf(const std::vector<Node*>& kids) {
  std::vector<Element*> out;
  for (Node* n : kids) {
    if (n->NodeType() == Node::ELEMENT_NODE) out.push_back(static_cast<Element*>(n));
  }
  return out;
}

using ChildNodeArg = std::variant<Node*, std::string>;

// Shared by ElementImplT/CharacterDataImplT/DocumentTypeImpl (the three
// `ChildNode` includers) -- free function templates rather than another
// `*ImplT<Base>` layer since DocumentTypeImpl isn't part of that chain.
// The `DOMString` alternative is accepted (proves the union's fallback
// JS->C++ conversion) but not materialized into a real Text node -- see
// dom.bruja's header comment.
template <class Self>
void ChildNodeBefore(Self* self, const std::vector<ChildNodeArg>& nodes) {
  if (self->parent == nullptr) return;
  for (const ChildNodeArg& n : nodes) {
    if (std::holds_alternative<Node*>(n)) {
      self->parent->InsertBefore(std::get<Node*>(n), self);
    }
  }
}

// Simplified to a plain append rather than exact "insert after this
// node" -- see dom.bruja's header comment (no "next sibling" primitive
// to insert before).
template <class Self>
void ChildNodeAfter(Self* self, const std::vector<ChildNodeArg>& nodes) {
  if (self->parent == nullptr) return;
  for (const ChildNodeArg& n : nodes) {
    if (std::holds_alternative<Node*>(n)) self->parent->AppendChild(std::get<Node*>(n));
  }
}

template <class Self>
void ChildNodeReplaceWith(Self* self, const std::vector<ChildNodeArg>& nodes) {
  if (self->parent == nullptr) return;
  Node* parent = self->parent;
  ChildNodeBefore(self, nodes);
  parent->RemoveChild(self);
}

// A standalone interface (not part of the Node tree), so it has no
// dependency on the Node/Element template stack below and can be defined
// first -- ElementImplT needs it as a member.
class DOMTokenListImpl : public DOMTokenList {
 public:
  uint32_t Length() override { return static_cast<uint32_t>(tokens_.size()); }
  std::string Value() override { return Join(tokens_); }
  void SetValue(const std::string& v) override { tokens_ = Split(v); }
  std::optional<std::string> Item(uint32_t index) override {
    if (index >= tokens_.size()) return std::nullopt;
    return tokens_[index];
  }
  bool Contains(const std::string& token) override {
    return std::find(tokens_.begin(), tokens_.end(), token) != tokens_.end();
  }
  void Add(const std::vector<std::string>& toks) override {
    for (const std::string& t : toks) {
      if (!Contains(t)) tokens_.push_back(t);
    }
  }
  void Remove(const std::vector<std::string>& toks) override {
    for (const std::string& t : toks) {
      tokens_.erase(std::remove(tokens_.begin(), tokens_.end(), t), tokens_.end());
    }
  }
  // NOTE: real DOMTokenList.toggle's `force` is genuinely tri-state (absent
  // = toggle, present = set-to-force); this generator's `optional` always
  // supplies a concrete default, so an explicit `toggle(x, false)` can't be
  // told apart from an omitted force here -- always just plain-toggles.
  bool Toggle(const std::string& token, bool force) override {
    (void)force;
    if (Contains(token)) {
      Remove(std::vector<std::string>{token});
      return false;
    }
    tokens_.push_back(token);
    return true;
  }

 private:
  static std::string Join(const std::vector<std::string>& toks) {
    std::string out;
    for (size_t i = 0; i < toks.size(); ++i) {
      if (i > 0) out += " ";
      out += toks[i];
    }
    return out;
  }
  static std::vector<std::string> Split(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
      if (c == ' ') {
        if (!cur.empty()) {
          out.push_back(cur);
          cur.clear();
        }
      } else {
        cur += c;
      }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
  }

  std::vector<std::string> tokens_;
};

// ---- Shared impl layers -----------------------------------------------

template <class Base>
class EventTargetImplT : public Base {
 public:
  void AddEventListener(const std::string& type,
                         std::shared_ptr<EventListenerCallback> listener) override {
    listeners_.push_back({type, listener});
  }
  void RemoveEventListener(const std::string& type,
                            std::shared_ptr<EventListenerCallback> listener) override {
    listeners_.erase(
        std::remove_if(listeners_.begin(), listeners_.end(),
                        [&](const Entry& e) {
                          return e.type == type && e.listener->Matches(*listener);
                        }),
        listeners_.end());
  }
  bool DispatchEvent(Event* event) override {
    bool any = false;
    for (auto& e : listeners_) {
      if (e.type == event->Type()) {
        e.listener->Call(event);
        any = true;
      }
    }
    return any;
  }

 private:
  struct Entry {
    std::string type;
    std::shared_ptr<EventListenerCallback> listener;
  };
  std::vector<Entry> listeners_;
};

template <class Base>
class NodeImplT : public EventTargetImplT<Base> {
 public:
  NodeImplT(uint16_t node_type, std::string node_name)
      : node_type_(node_type), node_name_(std::move(node_name)) {}

  uint16_t NodeType() override { return node_type_; }
  std::string NodeName() override { return node_name_; }
  Node* ParentNode() override { return parent; }
  Node* FirstChild() override { return children.empty() ? nullptr : children.front(); }
  Node* LastChild() override { return children.empty() ? nullptr : children.back(); }
  std::vector<Node*> ChildNodes() override { return children; }
  std::optional<std::string> TextContent() override { return text_content_; }
  void SetTextContent(const std::optional<std::string>& value) override {
    text_content_ = value;
  }

  Node* AppendChild(Node* child) override {
    children.push_back(child);
    return child;
  }
  Node* RemoveChild(Node* child) override {
    children.erase(std::remove(children.begin(), children.end(), child), children.end());
    return child;
  }
  // Null referenceNode means "insert at the end" per real spec -- which
  // conveniently matches std::find returning end() when referenceNode
  // isn't actually among children (defensive fallback, not expected).
  Node* InsertBefore(Node* new_node, Node* reference_node) override {
    auto it = reference_node == nullptr ? children.end()
                                         : std::find(children.begin(), children.end(),
                                                      reference_node);
    children.insert(it, new_node);
    return new_node;
  }
  bool HasChildNodes() override { return !children.empty(); }

  // Test-only: doesn't actually clone (returns `this`, upcast to Node*) --
  // proves the optional-bool-param + Node-return-type wiring, not real
  // clone semantics.
  Node* CloneNode(bool deep) override {
    (void)deep;
    return this;
  }
  bool Contains(Node* other) override {
    if (other == nullptr) return false;
    return std::find(children.begin(), children.end(), other) != children.end();
  }

  // Public test hooks (not part of the generated interface).
  Node* parent = nullptr;
  std::vector<Node*> children;

 private:
  uint16_t node_type_;
  std::string node_name_;
  std::optional<std::string> text_content_;
};

template <class Base>
class ElementImplT : public NodeImplT<Base> {
 public:
  ElementImplT(std::string tag_name, std::string node_name)
      : NodeImplT<Base>(/*ELEMENT_NODE=*/1, std::move(node_name)),
        tag_name_(std::move(tag_name)) {}

  std::string TagName() override { return tag_name_; }
  std::string Id() override { return id_; }
  void SetId(const std::string& value) override { id_ = value; }
  std::string ClassName() override { return class_name_; }
  void SetClassName(const std::string& value) override { class_name_ = value; }

  std::optional<std::string> GetAttribute(const std::string& name) override {
    auto it = attributes_.find(name);
    if (it == attributes_.end()) return std::nullopt;
    return it->second;
  }
  void SetAttribute(const std::string& name, const std::string& value) override {
    attributes_[name] = value;
  }
  void RemoveAttribute(const std::string& name) override { attributes_.erase(name); }
  bool HasAttribute(const std::string& name) override { return attributes_.count(name) != 0; }
  std::vector<std::string> GetAttributeNames() override {
    std::vector<std::string> names;
    for (const auto& kv : attributes_) names.push_back(kv.first);
    return names;
  }

  // ParentNode (via `includes`) -- Children/FirstElementChild/
  // LastElementChild/ChildElementCount only need this node's own children,
  // so they're real; QuerySelector(All) would need a real selector engine,
  // stubbed (see file header comment).
  std::vector<Element*> Children() override { return ElementChildrenOf(this->children); }
  Element* FirstElementChild() override {
    auto kids = Children();
    return kids.empty() ? nullptr : kids.front();
  }
  Element* LastElementChild() override {
    auto kids = Children();
    return kids.empty() ? nullptr : kids.back();
  }
  uint32_t ChildElementCount() override {
    return static_cast<uint32_t>(Children().size());
  }
  Element* QuerySelector(const std::string&) override { return nullptr; }
  std::vector<Element*> QuerySelectorAll(const std::string&) override { return {}; }

  // ChildNode (via `includes`) -- real: Node::RemoveChild is a proper
  // interface method, callable through the abstract `parent` pointer.
  void Remove() override {
    if (this->parent != nullptr) this->parent->RemoveChild(this);
  }
  void Before(const std::vector<ChildNodeArg>& nodes) override { ChildNodeBefore(this, nodes); }
  void After(const std::vector<ChildNodeArg>& nodes) override { ChildNodeAfter(this, nodes); }
  void ReplaceWith(const std::vector<ChildNodeArg>& nodes) override {
    ChildNodeReplaceWith(this, nodes);
  }

  // NonDocumentTypeChildNode (via `includes`) -- stubbed (see file header).
  Element* PreviousElementSibling() override { return nullptr; }
  Element* NextElementSibling() override { return nullptr; }

  DOMTokenList* ClassList() override { return &class_list_; }
  bool Matches(const std::string&) override { return false; }
  Element* Closest(const std::string&) override { return nullptr; }

 private:
  std::string tag_name_;
  std::string id_;
  std::string class_name_;
  std::map<std::string, std::string> attributes_;
  DOMTokenListImpl class_list_;
};

template <class Base>
class HTMLElementImplT : public ElementImplT<Base> {
 public:
  using ElementImplT<Base>::ElementImplT;

  std::string Title() override { return title_; }
  void SetTitle(const std::string& value) override { title_ = value; }
  bool Hidden() override { return hidden_; }
  void SetHidden(bool value) override { hidden_ = value; }
  int32_t TabIndex() override { return tab_index_; }
  void SetTabIndex(int32_t value) override { tab_index_ = value; }
  void Click() override { ++click_count; }

  int click_count = 0;

 private:
  std::string title_;
  bool hidden_ = false;
  int32_t tab_index_ = 0;
};

template <class Base>
class HTMLMediaElementImplT : public HTMLElementImplT<Base> {
 public:
  using HTMLElementImplT<Base>::HTMLElementImplT;

  std::string Src() override { return src_; }
  void SetSrc(const std::string& v) override { src_ = v; }
  bool Autoplay() override { return autoplay_; }
  void SetAutoplay(bool v) override { autoplay_ = v; }
  bool Loop() override { return loop_; }
  void SetLoop(bool v) override { loop_ = v; }
  bool Muted() override { return muted_; }
  void SetMuted(bool v) override { muted_ = v; }
  bool Controls() override { return controls_; }
  void SetControls(bool v) override { controls_ = v; }
  bool Paused() override { return paused_; }
  void Play() override { paused_ = false; }
  void Pause() override { paused_ = true; }

 private:
  std::string src_;
  bool autoplay_ = false, loop_ = false, muted_ = false, controls_ = false;
  bool paused_ = true;
};

template <class Base>
class CharacterDataImplT : public NodeImplT<Base> {
 public:
  CharacterDataImplT(uint16_t node_type, std::string node_name)
      : NodeImplT<Base>(node_type, std::move(node_name)) {}

  std::string Data() override { return data_; }
  void SetData(const std::string& v) override { data_ = v; }
  uint32_t Length() override { return static_cast<uint32_t>(data_.size()); }

  // ChildNode (via `includes`) -- real, same reasoning as ElementImplT::Remove.
  void Remove() override {
    if (this->parent != nullptr) this->parent->RemoveChild(this);
  }
  void Before(const std::vector<ChildNodeArg>& nodes) override { ChildNodeBefore(this, nodes); }
  void After(const std::vector<ChildNodeArg>& nodes) override { ChildNodeAfter(this, nodes); }
  void ReplaceWith(const std::vector<ChildNodeArg>& nodes) override {
    ChildNodeReplaceWith(this, nodes);
  }
  // NonDocumentTypeChildNode (via `includes`) -- stubbed (see file header).
  Element* PreviousElementSibling() override { return nullptr; }
  Element* NextElementSibling() override { return nullptr; }

 private:
  std::string data_;
};

class TextImpl : public CharacterDataImplT<Text> {
 public:
  explicit TextImpl(const std::string& data)
      : CharacterDataImplT<Text>(/*TEXT_NODE=*/3, "#text") {
    SetData(data);
  }
};

class CommentImpl : public CharacterDataImplT<Comment> {
 public:
  explicit CommentImpl(const std::string& data)
      : CharacterDataImplT<Comment>(/*COMMENT_NODE=*/8, "#comment") {
    SetData(data);
  }
};

class DocumentFragmentImpl : public NodeImplT<DocumentFragment> {
 public:
  DocumentFragmentImpl()
      : NodeImplT<DocumentFragment>(/*DOCUMENT_FRAGMENT_NODE=*/11, "#document-fragment") {}

  // ParentNode (via `includes`) -- same shape as ElementImplT's.
  std::vector<Element*> Children() override { return ElementChildrenOf(children); }
  Element* FirstElementChild() override {
    auto kids = Children();
    return kids.empty() ? nullptr : kids.front();
  }
  Element* LastElementChild() override {
    auto kids = Children();
    return kids.empty() ? nullptr : kids.back();
  }
  uint32_t ChildElementCount() override { return static_cast<uint32_t>(Children().size()); }
  Element* QuerySelector(const std::string&) override { return nullptr; }
  std::vector<Element*> QuerySelectorAll(const std::string&) override { return {}; }
};

class DocumentTypeImpl : public NodeImplT<DocumentType> {
 public:
  DocumentTypeImpl(std::string name, std::string public_id, std::string system_id)
      : NodeImplT<DocumentType>(/*DOCUMENT_TYPE_NODE=*/10, name),
        name_(std::move(name)),
        public_id_(std::move(public_id)),
        system_id_(std::move(system_id)) {}

  std::string Name() override { return name_; }
  std::string PublicId() override { return public_id_; }
  std::string SystemId() override { return system_id_; }
  void Remove() override {
    if (parent != nullptr) parent->RemoveChild(this);
  }
  void Before(const std::vector<ChildNodeArg>& nodes) override { ChildNodeBefore(this, nodes); }
  void After(const std::vector<ChildNodeArg>& nodes) override { ChildNodeAfter(this, nodes); }
  void ReplaceWith(const std::vector<ChildNodeArg>& nodes) override {
    ChildNodeReplaceWith(this, nodes);
  }

 private:
  std::string name_, public_id_, system_id_;
};

// ---- Concrete leaves -----------------------------------------------------

class HTMLDivElementImpl : public HTMLElementImplT<HTMLDivElement> {
 public:
  HTMLDivElementImpl() : HTMLElementImplT<HTMLDivElement>("DIV", "DIV") {}
};

class HTMLAnchorElementImpl : public HTMLElementImplT<HTMLAnchorElement> {
 public:
  HTMLAnchorElementImpl() : HTMLElementImplT<HTMLAnchorElement>("A", "A") {}
  std::string Href() override { return href_; }
  void SetHref(const std::string& v) override { href_ = v; }
  std::string Target() override { return target_; }
  void SetTarget(const std::string& v) override { target_ = v; }

 private:
  std::string href_;
  std::string target_;
};

class HTMLImageElementImpl : public HTMLElementImplT<HTMLImageElement> {
 public:
  HTMLImageElementImpl() : HTMLElementImplT<HTMLImageElement>("IMG", "IMG") {}
  std::string Src() override { return src_; }
  void SetSrc(const std::string& v) override { src_ = v; }
  std::string Alt() override { return alt_; }
  void SetAlt(const std::string& v) override { alt_ = v; }
  uint32_t Width() override { return width_; }
  void SetWidth(uint32_t v) override { width_ = v; }
  uint32_t Height() override { return height_; }
  void SetHeight(uint32_t v) override { height_ = v; }
  uint32_t NaturalWidth() override { return natural_width_; }
  uint32_t NaturalHeight() override { return natural_height_; }
  bool Complete() override { return complete_; }

 private:
  std::string src_, alt_;
  uint32_t width_ = 0, height_ = 0;
  uint32_t natural_width_ = 0, natural_height_ = 0;
  bool complete_ = false;
};

class HTMLInputElementImpl : public HTMLElementImplT<HTMLInputElement> {
 public:
  HTMLInputElementImpl() : HTMLElementImplT<HTMLInputElement>("INPUT", "INPUT") {}
  std::string Value() override { return value_; }
  void SetValue(const std::string& v) override { value_ = v; }
  std::string Type() override { return type_; }
  void SetType(const std::string& v) override { type_ = v; }
  bool Checked() override { return checked_; }
  void SetChecked(bool v) override { checked_ = v; }
  bool Disabled() override { return disabled_; }
  void SetDisabled(bool v) override { disabled_ = v; }

 private:
  std::string value_;
  std::string type_ = "text";
  bool checked_ = false, disabled_ = false;
};

class HTMLButtonElementImpl : public HTMLElementImplT<HTMLButtonElement> {
 public:
  HTMLButtonElementImpl() : HTMLElementImplT<HTMLButtonElement>("BUTTON", "BUTTON") {}
  bool Disabled() override { return disabled_; }
  void SetDisabled(bool v) override { disabled_ = v; }
  std::string Type() override { return type_; }
  void SetType(const std::string& v) override { type_ = v; }

 private:
  bool disabled_ = false;
  std::string type_ = "submit";
};

class HTMLFormElementImpl : public HTMLElementImplT<HTMLFormElement> {
 public:
  HTMLFormElementImpl() : HTMLElementImplT<HTMLFormElement>("FORM", "FORM") {}
  std::string Action() override { return action_; }
  void SetAction(const std::string& v) override { action_ = v; }
  std::string Method() override { return method_; }
  void SetMethod(const std::string& v) override { method_ = v; }
  void Submit() override { ++submit_count; }

  int submit_count = 0;

 private:
  std::string action_;
  std::string method_ = "get";
};

class HTMLScriptElementImpl : public HTMLElementImplT<HTMLScriptElement> {
 public:
  HTMLScriptElementImpl() : HTMLElementImplT<HTMLScriptElement>("SCRIPT", "SCRIPT") {}
  std::string Src() override { return src_; }
  void SetSrc(const std::string& v) override { src_ = v; }
  bool Async() override { return async_; }
  void SetAsync(bool v) override { async_ = v; }
  bool Defer() override { return defer_; }
  void SetDefer(bool v) override { defer_ = v; }

 private:
  std::string src_;
  bool async_ = false, defer_ = false;
};

class HTMLSelectElementImpl : public HTMLElementImplT<HTMLSelectElement> {
 public:
  HTMLSelectElementImpl() : HTMLElementImplT<HTMLSelectElement>("SELECT", "SELECT") {}
  std::string Value() override { return value_; }
  void SetValue(const std::string& v) override { value_ = v; }
  int32_t Length() override { return length; }

  int32_t length = 0;

 private:
  std::string value_;
};

class HTMLTextAreaElementImpl : public HTMLElementImplT<HTMLTextAreaElement> {
 public:
  HTMLTextAreaElementImpl() : HTMLElementImplT<HTMLTextAreaElement>("TEXTAREA", "TEXTAREA") {}
  std::string Value() override { return value_; }
  void SetValue(const std::string& v) override { value_ = v; }
  int32_t Rows() override { return rows_; }
  void SetRows(int32_t v) override { rows_ = v; }
  int32_t Cols() override { return cols_; }
  void SetCols(int32_t v) override { cols_ = v; }

 private:
  std::string value_;
  int32_t rows_ = 2, cols_ = 20;
};

class HTMLCanvasElementImpl : public HTMLElementImplT<HTMLCanvasElement> {
 public:
  HTMLCanvasElementImpl() : HTMLElementImplT<HTMLCanvasElement>("CANVAS", "CANVAS") {}
  uint32_t Width() override { return width_; }
  void SetWidth(uint32_t v) override { width_ = v; }
  uint32_t Height() override { return height_; }
  void SetHeight(uint32_t v) override { height_ = v; }

 private:
  uint32_t width_ = 300, height_ = 150;
};

class HTMLLabelElementImpl : public HTMLElementImplT<HTMLLabelElement> {
 public:
  HTMLLabelElementImpl() : HTMLElementImplT<HTMLLabelElement>("LABEL", "LABEL") {}
  std::string HtmlFor() override { return html_for_; }
  void SetHtmlFor(const std::string& v) override { html_for_ = v; }

 private:
  std::string html_for_;
};

class HTMLOptionElementImpl : public HTMLElementImplT<HTMLOptionElement> {
 public:
  HTMLOptionElementImpl() : HTMLElementImplT<HTMLOptionElement>("OPTION", "OPTION") {}
  std::string Value() override { return value_; }
  void SetValue(const std::string& v) override { value_ = v; }
  std::string Text() override { return text_; }
  void SetText(const std::string& v) override { text_ = v; }
  bool Selected() override { return selected_; }
  void SetSelected(bool v) override { selected_ = v; }
  bool Disabled() override { return disabled_; }
  void SetDisabled(bool v) override { disabled_ = v; }

 private:
  std::string value_, text_;
  bool selected_ = false, disabled_ = false;
};

class HTMLAudioElementImpl : public HTMLMediaElementImplT<HTMLAudioElement> {
 public:
  HTMLAudioElementImpl() : HTMLMediaElementImplT<HTMLAudioElement>("AUDIO", "AUDIO") {}
};

class HTMLVideoElementImpl : public HTMLMediaElementImplT<HTMLVideoElement> {
 public:
  HTMLVideoElementImpl() : HTMLMediaElementImplT<HTMLVideoElement>("VIDEO", "VIDEO") {}
  uint32_t Width() override { return width_; }
  void SetWidth(uint32_t v) override { width_ = v; }
  uint32_t Height() override { return height_; }
  void SetHeight(uint32_t v) override { height_ = v; }

 private:
  uint32_t width_ = 0, height_ = 0;
};

class HTMLIFrameElementImpl : public HTMLElementImplT<HTMLIFrameElement> {
 public:
  HTMLIFrameElementImpl() : HTMLElementImplT<HTMLIFrameElement>("IFRAME", "IFRAME") {}
  std::string Src() override { return src_; }
  void SetSrc(const std::string& v) override { src_ = v; }
  Window* ContentWindow() override { return content_window; }

  // Test hook: wired to the shared `window_impl` in main().
  Window* content_window = nullptr;

 private:
  std::string src_;
};

class HTMLTemplateElementImpl : public HTMLElementImplT<HTMLTemplateElement> {
 public:
  HTMLTemplateElementImpl()
      : HTMLElementImplT<HTMLTemplateElement>("TEMPLATE", "TEMPLATE"), content_(&fragment_) {}
  DocumentFragment* Content() override { return content_; }

 private:
  DocumentFragmentImpl fragment_;
  DocumentFragment* content_;
};

class EventImpl : public Event {
 public:
  EventImpl(std::string type, EventTarget* target, bool bubbles, bool cancelable)
      : type_(std::move(type)), target_(target), bubbles_(bubbles), cancelable_(cancelable) {}

  std::string Type() override { return type_; }
  EventTarget* Target() override { return target_; }
  bool Bubbles() override { return bubbles_; }
  bool Cancelable() override { return cancelable_; }
  bool DefaultPrevented() override { return default_prevented_; }
  void PreventDefault() override { default_prevented_ = true; }
  void StopPropagation() override { stopped_ = true; }

 private:
  std::string type_;
  EventTarget* target_;
  bool bubbles_, cancelable_;
  bool default_prevented_ = false;
  bool stopped_ = false;
};

// `detail` (an `any`) arrives from the JS->C++ dictionary conversion
// already JS_DupValue'd -- same contract as a plain `any` parameter (see
// cpp_generator.h). Stored as-is (no extra dup on the way in) and freed
// exactly once here; returning it out through Detail() dups again since
// that's a *new* reference handed to the caller (matches the pattern
// EmitToJs's kAny case already uses for every other `any` return).
class CustomEventImpl : public CustomEvent {
 public:
  CustomEventImpl(JSContext* ctx, std::string type, bool bubbles, bool cancelable, JSValue detail)
      : ctx_(ctx),
        type_(std::move(type)),
        bubbles_(bubbles),
        cancelable_(cancelable),
        detail_(detail) {}
  ~CustomEventImpl() { JS_FreeValue(ctx_, detail_); }
  CustomEventImpl(const CustomEventImpl&) = delete;
  CustomEventImpl& operator=(const CustomEventImpl&) = delete;

  std::string Type() override { return type_; }
  EventTarget* Target() override { return nullptr; }
  bool Bubbles() override { return bubbles_; }
  bool Cancelable() override { return cancelable_; }
  bool DefaultPrevented() override { return default_prevented_; }
  void PreventDefault() override { default_prevented_ = true; }
  void StopPropagation() override { stopped_ = true; }
  JSValue Detail() override { return JS_DupValue(ctx_, detail_); }

 private:
  JSContext* ctx_;
  std::string type_;
  bool bubbles_, cancelable_;
  bool default_prevented_ = false;
  bool stopped_ = false;
  JSValue detail_;
};

class LocationImpl : public Location {
 public:
  std::string Href() override { return href_; }
  void SetHref(const std::string& v) override { href_ = v; }
  void Reload() override { ++reload_count; }

  int reload_count = 0;

 private:
  std::string href_ = "about:blank";
};

// Insertion-order-preserving key/value pairs (spec-accurate for
// URLSearchParams, unlike a std::map) -- init parsing is simple `k=v&k2=v2`
// splitting with no percent-decoding (see dom.bruja's header comment).
class URLSearchParamsImpl : public URLSearchParams {
 public:
  explicit URLSearchParamsImpl(const std::string& init) { ParseInit(init); }

  std::optional<std::string> Get(const std::string& name) override {
    for (const auto& kv : params_) {
      if (kv.first == name) return kv.second;
    }
    return std::nullopt;
  }
  void Set(const std::string& name, const std::string& value) override {
    for (auto& kv : params_) {
      if (kv.first == name) {
        kv.second = value;
        return;
      }
    }
    params_.emplace_back(name, value);
  }
  void Append(const std::string& name, const std::string& value) override {
    params_.emplace_back(name, value);
  }
  void Delete(const std::string& name) override {
    params_.erase(std::remove_if(params_.begin(), params_.end(),
                                  [&](const auto& kv) { return kv.first == name; }),
                  params_.end());
  }
  bool Has(const std::string& name) override {
    for (const auto& kv : params_) {
      if (kv.first == name) return true;
    }
    return false;
  }
  std::string ToString() override {
    std::string out;
    for (size_t i = 0; i < params_.size(); ++i) {
      if (i > 0) out += "&";
      out += params_[i].first + "=" + params_[i].second;
    }
    return out;
  }

 private:
  void ParseInit(const std::string& init) {
    size_t pos = 0;
    while (pos < init.size()) {
      size_t amp = init.find('&', pos);
      std::string pair =
          init.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
      size_t eq = pair.find('=');
      if (!pair.empty()) {
        if (eq == std::string::npos) {
          params_.emplace_back(pair, "");
        } else {
          params_.emplace_back(pair.substr(0, eq), pair.substr(eq + 1));
        }
      }
      if (amp == std::string::npos) break;
      pos = amp + 1;
    }
  }

  std::vector<std::pair<std::string, std::string>> params_;
};

// Stores components independently rather than deriving them from a real
// parse of `href` -- no real URL parser here (see dom.bruja's header
// comment).
class URLImpl : public URL {
 public:
  URLImpl(std::string href, const std::string& base)
      : href_(std::move(href)), search_params_(std::make_unique<URLSearchParamsImpl>("")) {
    (void)base;
  }

  std::string Href() override { return href_; }
  void SetHref(const std::string& v) override { href_ = v; }
  std::string Protocol() override { return protocol_; }
  void SetProtocol(const std::string& v) override { protocol_ = v; }
  std::string Host() override { return host_; }
  void SetHost(const std::string& v) override { host_ = v; }
  std::string Pathname() override { return pathname_; }
  void SetPathname(const std::string& v) override { pathname_ = v; }
  std::string Search() override { return search_; }
  void SetSearch(const std::string& v) override { search_ = v; }
  std::string Hash() override { return hash_; }
  void SetHash(const std::string& v) override { hash_ = v; }
  URLSearchParams* SearchParams() override { return search_params_.get(); }
  std::string ToString() override { return href_; }

 private:
  std::string href_;
  std::string protocol_, host_, pathname_, search_, hash_;
  std::unique_ptr<URLSearchParamsImpl> search_params_;
};

class CustomElementRegistryImpl : public CustomElementRegistry {
 public:
  void Define(const std::string& name) override { defined_.push_back(name); }
  // Synchronously "resolves" regardless of whether `name` was actually
  // defined -- see dom_gen.h's Promise<T> doc: no real async wait here,
  // this proves the Promise/JS_Call plumbing, not a real registry.
  void WhenDefined(const std::string&) override {}

  std::vector<std::string> defined_;
};

class StorageImpl : public Storage {
 public:
  uint32_t Length() override { return static_cast<uint32_t>(items_.size()); }
  std::optional<std::string> Key(uint32_t index) override {
    if (index >= items_.size()) return std::nullopt;
    auto it = items_.begin();
    std::advance(it, index);
    return it->first;
  }
  std::optional<std::string> GetItem(const std::string& key) override {
    auto it = items_.find(key);
    if (it == items_.end()) return std::nullopt;
    return it->second;
  }
  void SetItem(const std::string& key, const std::string& value) override { items_[key] = value; }
  void RemoveItem(const std::string& key) override { items_.erase(key); }
  void Clear() override { items_.clear(); }

 private:
  std::map<std::string, std::string> items_;
};

class MutationRecordImpl : public MutationRecord {
 public:
  MutationRecordImpl(std::string type, Node* target) : type_(std::move(type)), target_(target) {}
  std::string Type() override { return type_; }
  Node* Target() override { return target_; }

 private:
  std::string type_;
  Node* target_;
};

class MutationObserverImpl : public MutationObserver {
 public:
  explicit MutationObserverImpl(std::shared_ptr<MutationCallbackCallback> callback)
      : callback_(std::move(callback)) {}

  void Observe(Node* target, const MutationObserverInit& options) override {
    (void)options;
    observed_.push_back(target);
  }
  void Disconnect() override { observed_.clear(); }
  std::vector<MutationRecord*> TakeRecords() override { return {}; }

  // Test-only: there's no real mutation-tracking engine here (see file
  // header comment) -- synchronously deliver one record to the stored
  // callback so the JS->C++->JS round trip through `sequence<MutationRecord>`
  // can actually be exercised.
  void SimulateMutation(const std::string& type, Node* target) {
    MutationRecordImpl record(type, target);
    std::vector<MutationRecord*> records = {&record};
    callback_->Call(records, this);
  }

 private:
  std::shared_ptr<MutationCallbackCallback> callback_;
  std::vector<Node*> observed_;
};

class NavigatorImpl : public Navigator {
 public:
  std::string UserAgent() override { return "WASMBruja-DOM/1.0"; }
  bool OnLine() override { return true; }
};

class ConsoleImpl : public Console {
 public:
  void Log(const std::string& message) override { logs.push_back(message); }
  void Warn(const std::string& message) override { warns.push_back(message); }
  void Error(const std::string& message) override { errors.push_back(message); }

  std::vector<std::string> logs, warns, errors;
};

class DocumentImpl : public NodeImplT<Document> {
 public:
  DocumentImpl() : NodeImplT<Document>(/*DOCUMENT_NODE=*/9, "#document") {}

  std::string ReadyState() override { return ready_state; }
  Element* DocumentElement() override { return document_element; }
  HTMLElement* Body() override { return body; }
  void SetBody(HTMLElement* value) override { body = value; }
  HTMLElement* Head() override { return head; }
  std::string Title() override { return title_; }
  void SetTitle(const std::string& v) override { title_ = v; }

  Element* CreateElement(const std::string& tag_name,
                          const ElementCreationOptions& options) override {
    (void)options;
    Element* el;
    if (tag_name == "a") {
      el = new HTMLAnchorElementImpl();
    } else if (tag_name == "input") {
      el = new HTMLInputElementImpl();
    } else {
      el = new HTMLDivElementImpl();
    }
    created_.emplace_back(el);
    return el;
  }
  Text* CreateTextNode(const std::string& data) override {
    auto* t = new TextImpl(data);
    created_text_.emplace_back(t);
    return t;
  }
  Comment* CreateComment(const std::string& data) override {
    auto* c = new CommentImpl(data);
    created_comment_.emplace_back(c);
    return c;
  }
  DocumentFragment* CreateDocumentFragment() override {
    auto* f = new DocumentFragmentImpl();
    created_fragment_.emplace_back(f);
    return f;
  }

  Element* GetElementById(const std::string& id) override {
    if (document_element != nullptr && document_element->Id() == id) return document_element;
    return nullptr;
  }
  std::vector<Element*> GetElementsByTagName(const std::string&) override { return {}; }
  std::vector<Element*> GetElementsByClassName(const std::string&) override { return {}; }

  // ParentNode (via `includes`) -- same shape as ElementImplT's.
  std::vector<Element*> Children() override { return ElementChildrenOf(children); }
  Element* FirstElementChild() override {
    auto kids = Children();
    return kids.empty() ? nullptr : kids.front();
  }
  Element* LastElementChild() override {
    auto kids = Children();
    return kids.empty() ? nullptr : kids.back();
  }
  uint32_t ChildElementCount() override { return static_cast<uint32_t>(Children().size()); }
  Element* QuerySelector(const std::string&) override { return nullptr; }
  std::vector<Element*> QuerySelectorAll(const std::string&) override { return {}; }

  std::string ready_state = "complete";
  Element* document_element = nullptr;
  HTMLElement* body = nullptr;
  HTMLElement* head = nullptr;

 private:
  std::string title_;
  std::vector<std::unique_ptr<Element>> created_;
  std::vector<std::unique_ptr<Text>> created_text_;
  std::vector<std::unique_ptr<Comment>> created_comment_;
  std::vector<std::unique_ptr<DocumentFragment>> created_fragment_;
};

class WindowImpl : public EventTargetImplT<Window> {
 public:
  // `location`/`document`/`navigator`/`console` attributes generate pure
  // virtuals literally named `Location()`/`Document()`/`Navigator()`/
  // `Console()` (PascalCase of the attribute name) -- same name as their
  // own return type. Legal C++ (the return type resolves before the
  // declarator introduces the new member name -- see README.md's
  // "Binding shape" section) but every OTHER use of these type names
  // inside this class must be qualified, since by then the name refers to
  // the member function, not the type.
  WindowImpl(bruja_dom_generated::Document* doc, bruja_dom_generated::Location* location,
             CustomElementRegistry* custom_elements, Storage* local_storage,
             Storage* session_storage, bruja_dom_generated::Navigator* navigator,
             bruja_dom_generated::Console* console)
      : document_(doc),
        location_(location),
        custom_elements_(custom_elements),
        local_storage_(local_storage),
        session_storage_(session_storage),
        navigator_(navigator),
        console_(console) {}

  bruja_dom_generated::Document* Document() override { return document_; }
  bruja_dom_generated::Location* Location() override { return location_; }
  CustomElementRegistry* CustomElements() override { return custom_elements_; }
  Storage* LocalStorage() override { return local_storage_; }
  Storage* SessionStorage() override { return session_storage_; }
  bruja_dom_generated::Navigator* Navigator() override { return navigator_; }
  bruja_dom_generated::Console* Console() override { return console_; }
  int32_t InnerWidth() override { return inner_width_; }
  void SetInnerWidth(int32_t v) override { inner_width_ = v; }
  int32_t InnerHeight() override { return inner_height_; }
  void SetInnerHeight(int32_t v) override { inner_height_ = v; }
  void Alert(const std::string& message) override { last_alert = message; }

  int32_t SetTimeout(std::shared_ptr<TimerCallbackCallback> handler, int32_t timeout,
                      const std::vector<std::string>& arguments) override {
    (void)timeout;
    (void)arguments;
    int32_t id = next_timer_id_++;
    pending_[id] = handler;
    return id;
  }
  void ClearTimeout(int32_t id) override { pending_.erase(id); }

  int32_t RequestAnimationFrame(std::shared_ptr<FrameRequestCallbackCallback> callback) override {
    int32_t id = next_raf_id_++;
    raf_pending_[id] = callback;
    return id;
  }
  void CancelAnimationFrame(int32_t handle) override { raf_pending_.erase(handle); }

  // Test-only: synchronously fire a pending timer/animation frame (there's
  // no real event loop here -- see dom.bruja's header comment on staying
  // synchronous).
  void FireTimer(int32_t id) {
    auto it = pending_.find(id);
    if (it != pending_.end()) {
      it->second->Call();
      pending_.erase(it);
    }
  }
  void FireAnimationFrame(int32_t id, double timestamp) {
    auto it = raf_pending_.find(id);
    if (it != raf_pending_.end()) {
      it->second->Call(timestamp);
      raf_pending_.erase(it);
    }
  }

  std::string last_alert;

 private:
  bruja_dom_generated::Document* document_;
  bruja_dom_generated::Location* location_;
  CustomElementRegistry* custom_elements_;
  Storage* local_storage_;
  Storage* session_storage_;
  bruja_dom_generated::Navigator* navigator_;
  bruja_dom_generated::Console* console_;
  int32_t inner_width_ = 1024;
  int32_t inner_height_ = 768;
  int32_t next_timer_id_ = 1;
  int32_t next_raf_id_ = 1;
  std::map<int32_t, std::shared_ptr<TimerCallbackCallback>> pending_;
  std::map<int32_t, std::shared_ptr<FrameRequestCallbackCallback>> raf_pending_;
};

// ---- quickjs eval helpers (same shape as navigator_roundtrip.cc) --------

bool Eval(JSContext* ctx, const char* source) {
  JSValue result = JS_Eval(ctx, source, strlen(source), "<test>", JS_EVAL_TYPE_GLOBAL);
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
  JSValue result = JS_Eval(ctx, source, strlen(source), "<test>", JS_EVAL_TYPE_GLOBAL);
  const char* cstr = JS_ToCString(ctx, result);
  std::string out = cstr ? cstr : "";
  if (cstr) JS_FreeCString(ctx, cstr);
  JS_FreeValue(ctx, result);
  return out;
}

// Promise `.then()` callbacks run as microtasks, not inline -- pump the
// job queue so a synchronously-resolved Promise's continuation actually
// runs before we inspect its side effects.
void RunPendingJobs(JSRuntime* rt) {
  JSContext* job_ctx;
  for (;;) {
    int ret = JS_ExecutePendingJob(rt, &job_ctx);
    if (ret <= 0) break;
  }
}

}  // namespace

int main() {
  JSRuntime* rt = JS_NewRuntime();
  JSContext* ctx = JS_NewContext(rt);

  DocumentImpl document_impl;
  LocationImpl location_impl;
  CustomElementRegistryImpl custom_elements_impl;
  StorageImpl local_storage_impl;
  StorageImpl session_storage_impl;
  NavigatorImpl navigator_impl;
  ConsoleImpl console_impl;
  WindowImpl window_impl(&document_impl, &location_impl, &custom_elements_impl,
                          &local_storage_impl, &session_storage_impl, &navigator_impl,
                          &console_impl);
  HTMLDivElementImpl root_div;
  document_impl.document_element = &root_div;
  document_impl.body = &root_div;

  HTMLDivElementImpl detached_div;  // never appended anywhere -- parentNode should read null

  HTMLAnchorElementImpl test_anchor;
  // NodeImplT::AppendChild deliberately doesn't wire a child's reverse
  // `parent` pointer generically (see file header comment) -- pre-wire it
  // by hand here so `testAnchor.remove()` (ChildNode via `includes`) has
  // something real to detach from once JS appends it below.
  test_anchor.parent = &document_impl;
  HTMLInputElementImpl test_input;
  HTMLVideoElementImpl test_video;
  HTMLIFrameElementImpl test_iframe;
  test_iframe.content_window = &window_impl;
  HTMLTemplateElementImpl test_template;
  DocumentTypeImpl doctype_impl("html", "", "");
  EventImpl test_event("test", &window_impl, /*bubbles=*/true, /*cancelable=*/true);

  // For Node.insertBefore + ChildNode.before/after/replaceWith: pre-wired
  // the same way test_anchor is above.
  HTMLDivElementImpl child_node;
  child_node.parent = &document_impl;
  HTMLDivElementImpl before_node;
  HTMLDivElementImpl after_node;
  HTMLDivElementImpl replace_node;

  JSValue global = JS_GetGlobalObject(ctx);
  InstallTextConstructor(ctx, global,
                          [](JSContext*, const std::string& data) { return new TextImpl(data); });
  InstallCommentConstructor(
      ctx, global, [](JSContext*, const std::string& data) { return new CommentImpl(data); });
  InstallDocumentFragmentConstructor(ctx, global,
                                      [](JSContext*) { return new DocumentFragmentImpl(); });
  InstallEventConstructor(ctx, global,
                           [](JSContext*, const std::string& type, const EventInit& init) {
                             return new EventImpl(type, /*target=*/nullptr, init.bubbles,
                                                   init.cancelable);
                           });
  InstallCustomEventConstructor(
      ctx, global, [](JSContext* c, const std::string& type, const CustomEventInit& init) {
        return new CustomEventImpl(c, type, init.bubbles, init.cancelable, init.detail);
      });
  // Test-only: captures the most recently `new MutationObserver(...)`-
  // constructed impl so the test can drive it from C++ (SimulateMutation)
  // without a real mutation-tracking engine to trigger it organically.
  MutationObserverImpl* last_observer = nullptr;
  InstallMutationObserverConstructor(
      ctx, global,
      [&last_observer](JSContext*, std::shared_ptr<MutationCallbackCallback> callback) {
        auto* obs = new MutationObserverImpl(std::move(callback));
        last_observer = obs;
        return obs;
      });
  InstallURLSearchParamsConstructor(
      ctx, global, [](JSContext*, const std::string& init) { return new URLSearchParamsImpl(init); });
  InstallURLConstructor(ctx, global, [](JSContext*, const std::string& url, const std::string& base) {
    return new URLImpl(url, base);
  });

  JS_SetPropertyStr(ctx, global, "window", CreateWindowBinding(ctx, &window_impl));
  JS_SetPropertyStr(ctx, global, "document", CreateDocumentBinding(ctx, &document_impl));
  JS_SetPropertyStr(ctx, global, "detachedDiv", CreateHTMLDivElementBinding(ctx, &detached_div));
  JS_SetPropertyStr(ctx, global, "testAnchor", CreateHTMLAnchorElementBinding(ctx, &test_anchor));
  JS_SetPropertyStr(ctx, global, "testInput", CreateHTMLInputElementBinding(ctx, &test_input));
  JS_SetPropertyStr(ctx, global, "testVideo", CreateHTMLVideoElementBinding(ctx, &test_video));
  JS_SetPropertyStr(ctx, global, "testIframe", CreateHTMLIFrameElementBinding(ctx, &test_iframe));
  JS_SetPropertyStr(ctx, global, "testTemplate",
                     CreateHTMLTemplateElementBinding(ctx, &test_template));
  JS_SetPropertyStr(ctx, global, "customElements",
                     CreateCustomElementRegistryBinding(ctx, &custom_elements_impl));
  JS_SetPropertyStr(ctx, global, "testEvent", CreateEventBinding(ctx, &test_event));
  JS_SetPropertyStr(ctx, global, "doctype", CreateDocumentTypeBinding(ctx, &doctype_impl));
  JS_SetPropertyStr(ctx, global, "childNode", CreateHTMLDivElementBinding(ctx, &child_node));
  JS_SetPropertyStr(ctx, global, "beforeNode", CreateHTMLDivElementBinding(ctx, &before_node));
  JS_SetPropertyStr(ctx, global, "afterNode", CreateHTMLDivElementBinding(ctx, &after_node));
  JS_SetPropertyStr(ctx, global, "replaceNode", CreateHTMLDivElementBinding(ctx, &replace_node));
  JS_FreeValue(ctx, global);

  int failures = 0;
  auto check = [&](bool ok, const char* what) {
    if (!ok) {
      std::fprintf(stderr, "FAIL: %s\n", what);
      ++failures;
    }
  };

  // readonly enum attribute + inherited const flattening.
  check(EvalString(ctx, "document.readyState") == "complete", "document.readyState");
  check(EvalString(ctx, "String(document.ELEMENT_NODE)") == "1",
        "Node.ELEMENT_NODE const flattened onto Document");
  check(!Eval(ctx, "document.readyState = 'loading';"),
        "assigning readonly readyState should throw");

  // nullable interface attribute: null when unset, non-null via manual wiring.
  check(EvalString(ctx, "String(detachedDiv.parentNode)") == "null",
        "detached node's parentNode is null");
  check(EvalString(ctx, "document.documentElement.tagName") == "DIV",
        "document.documentElement");

  // createElement + attribute map + nullable DOMString return.
  check(Eval(ctx,
             "var el = document.createElement('a');\n"
             "el.setAttribute('class', 'foo');"),
        "createElement + setAttribute");
  check(EvalString(ctx, "el.tagName") == "A", "created element tagName");
  check(EvalString(ctx, "el.getAttribute('class')") == "foo", "getAttribute present");
  check(EvalString(ctx, "String(el.getAttribute('missing'))") == "null",
        "getAttribute missing returns null");
  check(EvalString(ctx, "el.hasAttribute('class')") == "true", "hasAttribute");
  check(EvalString(ctx, "el.getAttributeNames().indexOf('class') >= 0") == "true",
        "getAttributeNames (sequence<DOMString> return)");

  // appendChild: polymorphic Node-typed argument (el's JS wrapper's class id
  // is Element's, from createElement's declared return type -- appendChild
  // must still accept it as a Node).
  check(Eval(ctx, "document.appendChild(el);"), "document.appendChild(element)");
  check(EvalString(ctx, "document.hasChildNodes()") == "true", "hasChildNodes");
  check(EvalString(ctx, "document.childNodes.length") == "1",
        "childNodes (sequence<Node> return)");
  check(EvalString(ctx, "document.childNodes[0].nodeName") == "A",
        "childNodes element re-wrapped as declared type Node");
  check(EvalString(ctx, "document.contains(el)") == "true", "Node.contains");
  check(EvalString(ctx, "document.cloneNode().nodeName") == "#document",
        "Node.cloneNode (optional bool param, Node return)");

  // ParentNode via `includes`: Document/Element both got `children` etc.
  // copied in from the mixin by the resolver, not hand-written.
  check(EvalString(ctx, "document.children.length") == "1",
        "ParentNode.children flattened onto Document via includes");
  check(EvalString(ctx, "document.firstElementChild.tagName") == "A",
        "ParentNode.firstElementChild via includes");
  check(EvalString(ctx, "String(document.childElementCount)") == "1",
        "ParentNode.childElementCount via includes");

  // ChildNode via `includes`: testAnchor.remove() detaches it from its real
  // parent (Document) purely through the abstract Node* interface (parent
  // wiring itself was done directly in C++ setup above, not generically by
  // appendChild -- see file header comment).
  check(Eval(ctx, "document.appendChild(testAnchor);"), "append testAnchor for remove() test");
  check(EvalString(ctx, "document.childNodes.length") == "2",
        "two children now (el + testAnchor)");
  check(Eval(ctx, "testAnchor.remove();"), "ChildNode.remove via includes");
  check(EvalString(ctx, "document.childNodes.length") == "1",
        "remove() actually detached testAnchor, leaving el");

  // classList (DOMTokenList), a standalone interface referenced from Element.
  check(Eval(ctx, "el.classList.add('a', 'b', 'a');"), "DOMTokenList.add (variadic)");
  check(EvalString(ctx, "String(el.classList.length)") == "2",
        "DOMTokenList.length (duplicates not re-added)");
  check(EvalString(ctx, "el.classList.contains('b')") == "true", "DOMTokenList.contains");
  check(EvalString(ctx, "el.classList.value") == "a b", "DOMTokenList.value getter");
  check(Eval(ctx, "el.classList.remove('a');"), "DOMTokenList.remove (variadic)");
  check(EvalString(ctx, "String(el.classList.length)") == "1", "DOMTokenList.remove worked");
  check(EvalString(ctx, "el.classList.toggle('c')") == "true", "DOMTokenList.toggle add");
  check(EvalString(ctx, "el.classList.contains('c')") == "true", "toggle actually added 'c'");

  // events: addEventListener/dispatchEvent/removeEventListener round-trip
  // through the generated callback wrapper.
  check(Eval(ctx,
             "globalThis.fired = false;\n"
             "globalThis.handler = function(e) { globalThis.fired = (e.type === 'test'); };\n"
             "window.addEventListener('test', handler);"),
        "addEventListener");
  check(Eval(ctx, "window.dispatchEvent(testEvent);"), "dispatchEvent");
  check(EvalString(ctx, "fired") == "true", "listener observed the dispatched event");

  check(Eval(ctx,
             "globalThis.fired = false;\n"
             "window.removeEventListener('test', handler);\n"
             "window.dispatchEvent(testEvent);"),
        "removeEventListener + re-dispatch");
  check(EvalString(ctx, "fired") == "false", "removed listener should not fire");

  // setTimeout: optional-with-default + trailing variadic DOMString params.
  check(Eval(ctx,
             "globalThis.timerFired = false;\n"
             "globalThis.timerId = window.setTimeout(function() { "
             "globalThis.timerFired = true; }, 50, 'a', 'b');"),
        "setTimeout");
  check(EvalString(ctx, "typeof timerId") == "number", "setTimeout returns an id");
  window_impl.FireTimer(1);
  check(EvalString(ctx, "timerFired") == "true", "fired timer invoked the JS callback");

  // constructor(...): `new Text(...)`/`new Comment(...)`/`new DocumentFragment()`
  // backed by the factories installed above.
  check(Eval(ctx, "globalThis.t = new Text('hello');"), "new Text(...)");
  check(EvalString(ctx, "t.data") == "hello", "constructed Text's data");
  check(EvalString(ctx, "String(t.nodeType)") == "3", "constructed Text's nodeType");
  check(Eval(ctx, "globalThis.c = new Comment('note');"), "new Comment(...)");
  check(EvalString(ctx, "c.data") == "note", "constructed Comment's data");
  check(Eval(ctx, "globalThis.frag = new DocumentFragment();"), "new DocumentFragment()");
  check(Eval(ctx, "frag.appendChild(t);"), "appendChild onto a constructed DocumentFragment");
  check(EvalString(ctx, "frag.hasChildNodes()") == "true", "constructed fragment has the child");

  // Promise<T>: customElements.whenDefined resolves (synchronously under
  // the hood, see dom_gen.h) -- .then()'s callback only runs once the job
  // queue is pumped.
  check(Eval(ctx, "customElements.define('x-widget');"), "CustomElementRegistry.define");
  check(Eval(ctx,
             "globalThis.whenDefinedRan = false;\n"
             "customElements.whenDefined('x-widget').then(function() { "
             "globalThis.whenDefinedRan = true; });"),
        "CustomElementRegistry.whenDefined (Promise<void> return)");
  check(EvalString(ctx, "whenDefinedRan") == "false",
        "the .then() callback hasn't run yet (still a microtask)");
  RunPendingJobs(rt);
  check(EvalString(ctx, "whenDefinedRan") == "true",
        "pumping the job queue ran the resolved promise's .then() callback");

  // Concrete-leaf spot checks (breadth of the inheritance flattening).
  check(Eval(ctx, "testAnchor.href = 'https://example.test/';"), "HTMLAnchorElement.href set");
  check(EvalString(ctx, "testAnchor.href") == "https://example.test/",
        "HTMLAnchorElement.href get");
  check(Eval(ctx, "testInput.checked = true;"), "HTMLInputElement.checked set");
  check(EvalString(ctx, "testInput.checked") == "true", "HTMLInputElement.checked get");
  check(EvalString(ctx, "typeof testInput.click") == "function",
        "HTMLElement.click flattened onto HTMLInputElement");

  check(EvalString(ctx, "testVideo.paused") == "true", "HTMLMediaElement.paused initial state");
  check(Eval(ctx, "testVideo.play();"), "HTMLMediaElement.play (flattened onto HTMLVideoElement)");
  check(EvalString(ctx, "testVideo.paused") == "false", "play() cleared paused");
  check(Eval(ctx, "testVideo.width = 640;"), "HTMLVideoElement.width (own member)");
  check(EvalString(ctx, "String(testVideo.width)") == "640", "HTMLVideoElement.width get");

  check(EvalString(ctx, "testIframe.contentWindow === undefined") == "false",
        "HTMLIFrameElement.contentWindow is not undefined");
  check(EvalString(ctx, "typeof testIframe.contentWindow.alert") == "function",
        "contentWindow (nullable interface attr) wraps a real Window");

  check(EvalString(ctx, "testTemplate.content.nodeName") == "#document-fragment",
        "HTMLTemplateElement.content (own DocumentFragment)");

  check(EvalString(ctx, "doctype.name") == "html", "DocumentType.name");
  check(EvalString(ctx, "String(doctype.nodeType)") == "10",
        "DocumentType.nodeType (Node const, DOCUMENT_TYPE_NODE)");
  check(EvalString(ctx, "typeof doctype.remove") == "function",
        "ChildNode.remove flattened onto DocumentType via includes");

  // constructor(...) + dictionary: `new Event(type, EventInit)`.
  check(Eval(ctx, "globalThis.ev = new Event('x', {bubbles: true, cancelable: false});"),
        "new Event(type, EventInit)");
  check(EvalString(ctx, "ev.type") == "x", "constructed Event's type");
  check(EvalString(ctx, "ev.bubbles") == "true", "constructed Event's bubbles (from EventInit)");
  check(EvalString(ctx, "ev.cancelable") == "false", "constructed Event's cancelable");

  // Dictionary inheritance, the actual proof: CustomEventInit : EventInit --
  // `bubbles` (inherited) and `detail` (own, `any`) both read correctly off
  // the SAME JS object passed to `new CustomEvent(...)`.
  check(Eval(ctx, "globalThis.cev = new CustomEvent('y', {bubbles: true, detail: 42});"),
        "new CustomEvent(type, CustomEventInit)");
  check(EvalString(ctx, "cev.type") == "y", "constructed CustomEvent's type");
  check(EvalString(ctx, "cev.bubbles") == "true",
        "constructed CustomEvent's bubbles -- inherited from EventInit via dictionary inheritance");
  check(EvalString(ctx, "String(cev.detail)") == "42",
        "constructed CustomEvent's detail -- own field, any passthrough");

  // Storage (localStorage/sessionStorage).
  check(Eval(ctx, "window.localStorage.setItem('k', 'v');"), "Storage.setItem");
  check(EvalString(ctx, "window.localStorage.getItem('k')") == "v", "Storage.getItem");
  check(EvalString(ctx, "String(window.localStorage.length)") == "1", "Storage.length");
  check(EvalString(ctx, "String(window.localStorage.getItem('missing'))") == "null",
        "Storage.getItem missing key returns null");
  check(Eval(ctx, "window.localStorage.removeItem('k');"), "Storage.removeItem");
  check(EvalString(ctx, "String(window.localStorage.length)") == "0",
        "removeItem actually removed it");
  check(EvalString(ctx, "window.sessionStorage === window.localStorage") == "false",
        "localStorage and sessionStorage are distinct Storage objects");

  // MutationObserver: constructor(callback) + observe() + a sequence of
  // MutationRecord round-tripped through the stored callback.
  check(Eval(ctx,
             "globalThis.mutationType = '';\n"
             "globalThis.mutationCount = 0;\n"
             "globalThis.observer = new MutationObserver(function(mutations, obs) {\n"
             "  globalThis.mutationCount = mutations.length;\n"
             "  globalThis.mutationType = mutations[0].type;\n"
             "});\n"
             "observer.observe(document);"),
        "new MutationObserver(callback) + observe()");
  check(last_observer != nullptr, "factory captured the constructed MutationObserverImpl");
  if (last_observer != nullptr) last_observer->SimulateMutation("childList", &document_impl);
  check(EvalString(ctx, "String(mutationCount)") == "1",
        "callback received a sequence<MutationRecord> of length 1");
  check(EvalString(ctx, "mutationType") == "childList",
        "MutationRecord.type survived the C++->JS->C++ round trip");

  // requestAnimationFrame: first real use of `double` in dom.bruja.
  check(Eval(ctx,
             "globalThis.rafTimestamp = -1;\n"
             "globalThis.rafId = window.requestAnimationFrame(function(t) { "
             "globalThis.rafTimestamp = t; });"),
        "requestAnimationFrame");
  window_impl.FireAnimationFrame(1, 1.5);
  check(EvalString(ctx, "rafTimestamp") == "1.5",
        "FrameRequestCallback's double timestamp round-tripped correctly");

  // navigator/console on Window.
  check(EvalString(ctx, "window.navigator.userAgent") == "WASMBruja-DOM/1.0",
        "window.navigator.userAgent");
  check(EvalString(ctx, "window.navigator.onLine") == "true", "window.navigator.onLine");
  check(Eval(ctx, "window.console.log('hello from JS');"), "window.console.log");
  check(console_impl.logs.size() == 1 && console_impl.logs[0] == "hello from JS",
        "console.log reached the C++ ConsoleImpl");

  // Node.insertBefore + ChildNode.before/after/replaceWith -- the union-
  // type showcase. `document.childNodes` is [el] going in (el from the
  // earlier createElement/appendChild block, still attached).
  check(Eval(ctx, "document.appendChild(childNode);"), "append childNode");
  check(EvalString(ctx, "String(document.childNodes.length)") == "2", "childNode appended");

  check(Eval(ctx, "childNode.before(beforeNode, 'ignored text');"),
        "ChildNode.before -- union+variadic: one Node arg, one DOMString arg, neither throws");
  check(EvalString(ctx, "String(document.childNodes.length)") == "3",
        "before()'s Node argument was actually inserted");
  check(EvalString(ctx, "document.childNodes[1].nodeName") == "DIV",
        "insertBefore placed beforeNode immediately before childNode (real positional insert)");

  check(Eval(ctx, "childNode.after(afterNode);"), "ChildNode.after");
  check(EvalString(ctx, "String(document.childNodes.length)") == "4",
        "after()'s Node argument was inserted (simplified to append -- see dom.bruja header)");

  check(Eval(ctx, "childNode.replaceWith(replaceNode);"), "ChildNode.replaceWith");
  check(EvalString(ctx, "String(document.childNodes.length)") == "4",
        "replaceWith swaps one node for another -- count unchanged");
  check(EvalString(ctx, "document.contains(childNode)") == "false",
        "replaceWith actually removed the original node");
  check(EvalString(ctx, "document.contains(replaceNode)") == "true",
        "replaceWith actually inserted the replacement");

  // URLSearchParams + URL -- no new grammar, cheap breadth.
  check(Eval(ctx, "globalThis.usp = new URLSearchParams('a=1&b=2');"),
        "new URLSearchParams(init) -- simple k=v&k2=v2 parsing");
  check(EvalString(ctx, "usp.get('a')") == "1", "URLSearchParams.get, parsed from init");
  check(EvalString(ctx, "usp.has('b')") == "true", "URLSearchParams.has");
  check(Eval(ctx, "usp.set('a', '99');"), "URLSearchParams.set");
  check(EvalString(ctx, "usp.get('a')") == "99", "set() overwrote the existing value");
  check(Eval(ctx, "usp.append('c', '3');"), "URLSearchParams.append");
  check(EvalString(ctx, "usp.toString()") == "a=99&b=2&c=3", "URLSearchParams.toString");
  check(Eval(ctx, "usp.delete('b');"), "URLSearchParams.delete (method named after a JS keyword)");
  check(EvalString(ctx, "usp.has('b')") == "false", "delete() actually removed it");

  check(Eval(ctx, "globalThis.u = new URL('https://example.test/path');"), "new URL(url)");
  check(Eval(ctx, "u.protocol = 'https:'; u.host = 'example.test';"),
        "URL component setters (independently settable, no real parser -- see dom.bruja header)");
  check(EvalString(ctx, "u.protocol") == "https:", "URL.protocol getter");
  check(EvalString(ctx, "typeof u.searchParams.get") == "function",
        "URL.searchParams wired to a real URLSearchParams object");

  JS_FreeContext(ctx);
  JS_FreeRuntime(rt);

  if (failures > 0) {
    std::fprintf(stderr, "dom_roundtrip: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("dom_roundtrip: OK\n");
  return 0;
}
