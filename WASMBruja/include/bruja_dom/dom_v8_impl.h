// V8-backend counterpart to dom_impl.h -- the exact same real,
// production-shaped DOM implementation classes (extracted originally from
// tests/golden/dom_roundtrip.cc), built against examples/dom/dom.bruja's
// --backend=v8 generated header (dom_v8_gen.h) instead of the quickjs one
// (dom_gen.h). Every class here is a byte-for-byte copy of dom_impl.h's
// own, EXCEPT CustomEventImpl: its `any`-typed `detail` field is
// v8::Local<v8::Value> (already a real, independently-owned reference in
// this facade -- see WASMv8bindings/include/v8-local-handle.h's file
// comment) instead of a raw quickjs JSValue/JSContext* pair, so it needs
// no explicit dup/free bookkeeping and no isolate to hold onto -- see that
// class below. That every other one of dom_impl.h's ~30 concrete classes
// needed *zero* changes to compile against this backend's generated
// header is itself strong evidence the two backends produce identically-
// shaped pure-virtual interface classes (proven in
// tests/golden/dom_v8_roundtrip.cc). See dom_impl.h's own file comment for
// the shared "still-open limitations" (AppendChild not wiring `parent`
// automatically, etc.) -- unchanged here, since the underlying classes are
// unchanged.
#ifndef BRUJA_DOM_V8_IMPL_H_
#define BRUJA_DOM_V8_IMPL_H_

#include "dom_v8_gen.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace bruja_dom_generated {

namespace dom_impl_detail {

inline std::string ToUpperAscii(const std::string& s) {
  std::string out = s;
  for (char& c : out) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
  return out;
}

inline std::vector<Element*> ElementChildrenOf(const std::vector<Node*>& kids) {
  std::vector<Element*> out;
  for (Node* n : kids) {
    if (n->NodeType() == Node::ELEMENT_NODE) out.push_back(static_cast<Element*>(n));
  }
  return out;
}

// A minimal selector: `#id`, `.class`, a bare tag name, or `*`. No
// combinators, no comma-separated lists -- see this file's header comment.
inline bool MatchesSimpleSelector(Element* el, const std::string& selector) {
  if (selector.empty()) return false;
  if (selector == "*") return true;
  if (selector[0] == '#') return el->Id() == selector.substr(1);
  if (selector[0] == '.') return el->ClassList()->Contains(selector.substr(1));
  return el->TagName() == ToUpperAscii(selector);
}

inline void WalkElements(Node* root, const std::function<bool(Element*)>& visit) {
  for (Node* child : root->ChildNodes()) {
    if (child->NodeType() == Node::ELEMENT_NODE) {
      Element* el = static_cast<Element*>(child);
      if (!visit(el)) return;
      WalkElements(el, visit);
    } else {
      WalkElements(child, visit);
    }
  }
}

// WHATWG DOM "text content" algorithm for a non-CharacterData node: the
// concatenation of every Text node descendant's data, in tree order
// (Comment data is excluded -- dynamic_cast<Text*> fails for CommentImpl).
// Used by NodeImplT<Base>::TextContent() below for anything that isn't
// itself CharacterData (Element/DocumentFragment/Document).
inline void CollectTextContent(Node* node, std::string* out) {
  if (node == nullptr) return;
  if (auto* text = dynamic_cast<Text*>(node)) {
    *out += text->Data();
    return;
  }
  for (Node* child : node->ChildNodes()) CollectTextContent(child, out);
}

}  // namespace dom_impl_detail

using ChildNodeArg = std::variant<Node*, std::string>;

// Shared by ElementImplT/CharacterDataImplT/DocumentTypeImpl (the three
// `ChildNode` includers).
template <class Self>
void ChildNodeBefore(Self* self, const std::vector<ChildNodeArg>& nodes) {
  if (self->parent == nullptr) return;
  for (const ChildNodeArg& n : nodes) {
    if (std::holds_alternative<Node*>(n)) self->parent->InsertBefore(std::get<Node*>(n), self);
  }
}

// Simplified to a plain append rather than exact "insert after this node"
// -- see dom.bruja's header comment (no "next sibling" primitive to insert
// before).
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
  bool Toggle(const std::string& token, bool force) override {
    (void)force;
    if (Contains(token)) {
      Remove(std::vector<std::string>{token});
      return false;
    }
    tokens_.push_back(token);
    return true;
  }

  // Not part of the generated interface -- lets WASMBlinker's HTML parser
  // seed class list state directly from a parsed `class="a b"` attribute.
  void SetFromClassAttribute(const std::string& class_attr) { tokens_ = Split(class_attr); }

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

// A small non-templated interface every NodeImplT<Base> instantiation
// also implements, purely so code holding an abstract Node*/Element*
// (this library's own generated-interface pointers, e.g. whatever
// DocumentImpl::CreateElement just handed back) can still reach/set the
// `parent` field regardless of which concrete leaf type it actually is --
// `dynamic_cast<NodeImplBase*>(some_node)` works across the multiple-
// inheritance branch below since every leaf type is genuinely
// polymorphic. This is what lets code building a tree from a *known*
// concrete source (e.g. WASMBlinker mirroring its own parsed HTML) wire
// `parent` correctly through the abstract interface, without needing a
// per-leaf-type switch -- see this file's top header comment for why
// AppendChild itself still can't do this generically.
class NodeImplBase {
 public:
  virtual ~NodeImplBase() = default;
  virtual void SetParentPtr(Node* parent) = 0;
};

template <class Base>
class NodeImplT : public EventTargetImplT<Base>, public NodeImplBase {
 public:
  NodeImplT(uint16_t node_type, std::string node_name)
      : node_type_(node_type), node_name_(std::move(node_name)) {}

  void SetParentPtr(Node* p) override { parent = p; }

  uint16_t NodeType() override { return node_type_; }
  std::string NodeName() override { return node_name_; }
  Node* ParentNode() override { return parent; }
  Node* FirstChild() override { return children.empty() ? nullptr : children.front(); }
  Node* LastChild() override { return children.empty() ? nullptr : children.back(); }
  std::vector<Node*> ChildNodes() override { return children; }
  // WHATWG DOM: for anything that isn't itself CharacterData (Element,
  // DocumentFragment, Document here), the getter computes the
  // concatenation of Text descendants' data -- it does not read back a
  // stored field. text_content_ is only ever non-nullopt after an
  // explicit SetTextContent() (JS `el.textContent = "..."`), which this
  // simplified implementation honors as an override rather than also
  // replacing `children` with a single new Text node the way the real
  // setter does (same "test-only-grade" simplification as CloneNode
  // above). CharacterDataImplT (Text/Comment) overrides both accessors
  // to proxy Data()/SetData() instead -- see below.
  std::optional<std::string> TextContent() override {
    if (text_content_) return text_content_;
    std::string out;
    dom_impl_detail::CollectTextContent(this, &out);
    return out;
  }
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
  Node* InsertBefore(Node* new_node, Node* reference_node) override {
    auto it = reference_node == nullptr
                  ? children.end()
                  : std::find(children.begin(), children.end(), reference_node);
    children.insert(it, new_node);
    return new_node;
  }
  bool HasChildNodes() override { return !children.empty(); }

  // Test-only-grade: doesn't actually clone (returns `this`) -- proves the
  // optional-bool-param + Node-return-type wiring, not real clone
  // semantics. See this file's header comment.
  Node* CloneNode(bool deep) override {
    (void)deep;
    return this;
  }
  bool Contains(Node* other) override {
    if (other == nullptr) return false;
    return std::find(children.begin(), children.end(), other) != children.end();
  }

  // Public: not part of the generated interface. `parent` is wired by
  // whoever builds the tree (see this file's header comment on why
  // AppendChild can't do it generically).
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
  void SetClassName(const std::string& value) override {
    class_name_ = value;
    class_list_.SetFromClassAttribute(value);
  }

  std::optional<std::string> GetAttribute(const std::string& name) override {
    auto it = attributes_.find(name);
    if (it == attributes_.end()) return std::nullopt;
    return it->second;
  }
  void SetAttribute(const std::string& name, const std::string& value) override {
    attributes_[name] = value;
    if (name == "id") id_ = value;
    if (name == "class") SetClassName(value);
  }
  void RemoveAttribute(const std::string& name) override { attributes_.erase(name); }
  bool HasAttribute(const std::string& name) override { return attributes_.count(name) != 0; }
  std::vector<std::string> GetAttributeNames() override {
    std::vector<std::string> names;
    for (const auto& kv : attributes_) names.push_back(kv.first);
    return names;
  }

  std::vector<Element*> Children() override {
    return dom_impl_detail::ElementChildrenOf(this->children);
  }
  Element* FirstElementChild() override {
    auto kids = Children();
    return kids.empty() ? nullptr : kids.front();
  }
  Element* LastElementChild() override {
    auto kids = Children();
    return kids.empty() ? nullptr : kids.back();
  }
  uint32_t ChildElementCount() override { return static_cast<uint32_t>(Children().size()); }

  Element* QuerySelector(const std::string& selectors) override {
    Element* found = nullptr;
    dom_impl_detail::WalkElements(this, [&](Element* el) {
      if (dom_impl_detail::MatchesSimpleSelector(el, selectors)) {
        found = el;
        return false;
      }
      return true;
    });
    return found;
  }
  std::vector<Element*> QuerySelectorAll(const std::string& selectors) override {
    std::vector<Element*> out;
    dom_impl_detail::WalkElements(this, [&](Element* el) {
      if (dom_impl_detail::MatchesSimpleSelector(el, selectors)) out.push_back(el);
      return true;
    });
    return out;
  }

  void Remove() override {
    if (this->parent != nullptr) this->parent->RemoveChild(this);
  }
  void Before(const std::vector<ChildNodeArg>& nodes) override { ChildNodeBefore(this, nodes); }
  void After(const std::vector<ChildNodeArg>& nodes) override { ChildNodeAfter(this, nodes); }
  void ReplaceWith(const std::vector<ChildNodeArg>& nodes) override {
    ChildNodeReplaceWith(this, nodes);
  }

  // Stubbed, same as the golden test: would need a live sibling-list
  // primitive through the abstract `Node*` interface (see file header).
  Element* PreviousElementSibling() override { return nullptr; }
  Element* NextElementSibling() override { return nullptr; }

  DOMTokenList* ClassList() override { return &class_list_; }
  bool Matches(const std::string& selectors) override {
    return dom_impl_detail::MatchesSimpleSelector(this, selectors);
  }
  Element* Closest(const std::string& selectors) override {
    Element* cur = this;
    while (cur != nullptr) {
      if (dom_impl_detail::MatchesSimpleSelector(cur, selectors)) return cur;
      Node* p = cur->ParentNode();
      cur = (p != nullptr && p->NodeType() == Node::ELEMENT_NODE) ? static_cast<Element*>(p)
                                                                  : nullptr;
    }
    return nullptr;
  }

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

  // WHATWG DOM: CharacterData's textContent getter/setter is just its
  // data -- overrides NodeImplT<Base>::TextContent()'s separate
  // text_content_ field (which Text/Comment never populated, so
  // `textNode.textContent` was always empty regardless of `.data`).
  std::optional<std::string> TextContent() override { return data_; }
  void SetTextContent(const std::optional<std::string>& value) override {
    data_ = value.value_or("");
  }

  void Remove() override {
    if (this->parent != nullptr) this->parent->RemoveChild(this);
  }
  void Before(const std::vector<ChildNodeArg>& nodes) override { ChildNodeBefore(this, nodes); }
  void After(const std::vector<ChildNodeArg>& nodes) override { ChildNodeAfter(this, nodes); }
  void ReplaceWith(const std::vector<ChildNodeArg>& nodes) override {
    ChildNodeReplaceWith(this, nodes);
  }
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

  std::vector<Element*> Children() override {
    return dom_impl_detail::ElementChildrenOf(children);
  }
  Element* FirstElementChild() override {
    auto kids = Children();
    return kids.empty() ? nullptr : kids.front();
  }
  Element* LastElementChild() override {
    auto kids = Children();
    return kids.empty() ? nullptr : kids.back();
  }
  uint32_t ChildElementCount() override { return static_cast<uint32_t>(Children().size()); }
  Element* QuerySelector(const std::string& selectors) override {
    Element* found = nullptr;
    dom_impl_detail::WalkElements(this, [&](Element* el) {
      if (dom_impl_detail::MatchesSimpleSelector(el, selectors)) {
        found = el;
        return false;
      }
      return true;
    });
    return found;
  }
  std::vector<Element*> QuerySelectorAll(const std::string& selectors) override {
    std::vector<Element*> out;
    dom_impl_detail::WalkElements(this, [&](Element* el) {
      if (dom_impl_detail::MatchesSimpleSelector(el, selectors)) out.push_back(el);
      return true;
    });
    return out;
  }
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

// ---- Concrete leaves -------------------------------------------------

class HTMLDivElementImpl : public HTMLElementImplT<HTMLDivElement> {
 public:
  explicit HTMLDivElementImpl(std::string tag = "DIV")
      : HTMLElementImplT<HTMLDivElement>(tag, tag) {}
};

class HTMLAnchorElementImpl : public HTMLElementImplT<HTMLAnchorElement> {
 public:
  HTMLAnchorElementImpl() : HTMLElementImplT<HTMLAnchorElement>("A", "A") {}
  std::string Href() override { return href_; }
  void SetHref(const std::string& v) override { href_ = v; }
  std::string Target() override { return target_; }
  void SetTarget(const std::string& v) override { target_ = v; }

  // Href()/Target() are separate fields from the generic attributes_ map
  // ElementImplT::SetAttribute alone populates (same reason Id()/
  // ClassName() needed their own id/class special-casing there already)
  // -- without this, an `<a href="...">` parsed from HTML left .href
  // empty until a script explicitly set it, even though the html
  // attribute itself was right there in attributes_/GetAttribute("href").
  void SetAttribute(const std::string& name, const std::string& value) override {
    ElementImplT<HTMLAnchorElement>::SetAttribute(name, value);
    if (name == "href") href_ = value;
    if (name == "target") target_ = value;
  }

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

  // Not part of the generated interface (naturalWidth/naturalHeight/complete
  // are `readonly attribute` -- no SetX in HTMLImageElement.h). Called by
  // LocalFrameImpl once wasmskia::DecodeImage actually succeeds for this
  // element's src (see RebuildDomTree's image-decode pass), the same way a
  // real engine flips these once the image's real byte decode finishes.
  void SetNaturalDimensions(uint32_t w, uint32_t h) {
    natural_width_ = w;
    natural_height_ = h;
    complete_ = true;
  }

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

  // Same reason HTMLAnchorElementImpl overrides SetAttribute: value/type/
  // checked/disabled are separate fields the generic attributes_ map
  // alone doesn't populate. checked/disabled are real HTML boolean
  // attributes -- presence (any value, including "") means true; real
  // absence (never reaching SetAttribute for that name) stays false, an
  // explicit `checked=""`/bare `checked` both parse to the same
  // attributes_["checked"]="" either way upstream in html_parser.cc.
  void SetAttribute(const std::string& name, const std::string& value) override {
    ElementImplT<HTMLInputElement>::SetAttribute(name, value);
    if (name == "value") value_ = value;
    if (name == "type") type_ = value;
    if (name == "checked") checked_ = true;
    if (name == "disabled") disabled_ = true;
  }

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
  bool stopped() const { return stopped_; }

 private:
  std::string type_;
  EventTarget* target_;
  bool bubbles_, cancelable_;
  bool default_prevented_ = false;
  bool stopped_ = false;
};

class CustomEventImpl : public CustomEvent {
 public:
  // V8-backend version: `any` maps to v8::Local<v8::Value>, already a
  // real, independently-owned reference (see v8-local-handle.h's file
  // comment) -- no explicit dup/free bookkeeping needed, unlike the
  // quickjs-backend original this was adapted from.
  CustomEventImpl(std::string type, bool bubbles, bool cancelable, v8::Local<v8::Value> detail)
      : type_(std::move(type)), bubbles_(bubbles), cancelable_(cancelable), detail_(detail) {}
  CustomEventImpl(const CustomEventImpl&) = delete;
  CustomEventImpl& operator=(const CustomEventImpl&) = delete;

  std::string Type() override { return type_; }
  EventTarget* Target() override { return nullptr; }
  bool Bubbles() override { return bubbles_; }
  bool Cancelable() override { return cancelable_; }
  bool DefaultPrevented() override { return default_prevented_; }
  void PreventDefault() override { default_prevented_ = true; }
  void StopPropagation() override { stopped_ = true; }
  v8::Local<v8::Value> Detail() override { return detail_; }

 private:
  std::string type_;
  bool bubbles_, cancelable_;
  bool default_prevented_ = false;
  bool stopped_ = false;
  v8::Local<v8::Value> detail_;
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
      std::string pair = init.substr(pos, amp == std::string::npos ? std::string::npos : amp - pos);
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
  void WhenDefined(const std::string&) override {}

  std::vector<std::string> defined_;
};

// The explicit ask this file was extracted for: a real, working
// key/value store backing window.localStorage/window.sessionStorage.
// In-memory only -- no on-disk persistence, so localStorage does not
// actually survive a process restart yet (documented, not hidden; a
// natural future integration point is a real KV-store sibling repo, if
// one exists, or a simple flat-file dump on Clear()/SetItem()).
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

  // Not part of the generated interface: frees every node this Document
  // has ever created and clears its own tree/title state, so an embedder
  // that rebuilds the DOM from scratch on each navigation (e.g.
  // WASMBlinker's LocalFrameImpl) doesn't leak the previous page's nodes
  // -- CreateElement/CreateTextNode/CreateComment/CreateDocumentFragment
  // otherwise only ever grow their ownership vectors.
  void Reset() {
    children.clear();
    document_element = nullptr;
    body = nullptr;
    head = nullptr;
    title_.clear();
    created_.clear();
    created_text_.clear();
    created_comment_.clear();
    created_fragment_.clear();
  }

  std::string ReadyState() override { return ready_state; }
  Element* DocumentElement() override { return document_element; }
  HTMLElement* Body() override { return body; }
  void SetBody(HTMLElement* value) override { body = value; }
  HTMLElement* Head() override { return head; }
  std::string Title() override { return title_; }
  void SetTitle(const std::string& v) override { title_ = v; }

  // Covers every concrete HTMLXxxElement this file defines; anything else
  // (p/span/h1/ul/li/...) falls back to a generic div-shaped HTMLElement --
  // same "modeled on, not a literal port" tradeoff as the rest of this
  // family (there's no distinct HTMLParagraphElement/HTMLHeadingElement/...
  // in dom.bruja yet).
  Element* CreateElement(const std::string& tag_name,
                         const ElementCreationOptions& options) override {
    (void)options;
    std::string tag = dom_impl_detail::ToUpperAscii(tag_name);
    Element* el = MakeElementForTag(tag);
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
    Element* found = nullptr;
    dom_impl_detail::WalkElements(this, [&](Element* el) {
      if (el->Id() == id) {
        found = el;
        return false;
      }
      return true;
    });
    return found;
  }
  std::vector<Element*> GetElementsByTagName(const std::string& qualified_name) override {
    std::vector<Element*> out;
    std::string upper = dom_impl_detail::ToUpperAscii(qualified_name);
    dom_impl_detail::WalkElements(this, [&](Element* el) {
      if (upper == "*" || el->TagName() == upper) out.push_back(el);
      return true;
    });
    return out;
  }
  std::vector<Element*> GetElementsByClassName(const std::string& class_names) override {
    std::vector<Element*> out;
    std::vector<std::string> wanted;
    {
      std::string cur;
      for (char c : class_names) {
        if (c == ' ') {
          if (!cur.empty()) wanted.push_back(cur);
          cur.clear();
        } else {
          cur += c;
        }
      }
      if (!cur.empty()) wanted.push_back(cur);
    }
    dom_impl_detail::WalkElements(this, [&](Element* el) {
      bool all = !wanted.empty();
      for (const auto& w : wanted) {
        if (!el->ClassList()->Contains(w)) {
          all = false;
          break;
        }
      }
      if (all) out.push_back(el);
      return true;
    });
    return out;
  }

  std::vector<Element*> Children() override {
    return dom_impl_detail::ElementChildrenOf(children);
  }
  Element* FirstElementChild() override {
    auto kids = Children();
    return kids.empty() ? nullptr : kids.front();
  }
  Element* LastElementChild() override {
    auto kids = Children();
    return kids.empty() ? nullptr : kids.back();
  }
  uint32_t ChildElementCount() override { return static_cast<uint32_t>(Children().size()); }
  Element* QuerySelector(const std::string& selectors) override {
    Element* found = nullptr;
    dom_impl_detail::WalkElements(this, [&](Element* el) {
      if (dom_impl_detail::MatchesSimpleSelector(el, selectors)) {
        found = el;
        return false;
      }
      return true;
    });
    return found;
  }
  std::vector<Element*> QuerySelectorAll(const std::string& selectors) override {
    std::vector<Element*> out;
    dom_impl_detail::WalkElements(this, [&](Element* el) {
      if (dom_impl_detail::MatchesSimpleSelector(el, selectors)) out.push_back(el);
      return true;
    });
    return out;
  }

  std::string ready_state = "complete";
  Element* document_element = nullptr;
  HTMLElement* body = nullptr;
  HTMLElement* head = nullptr;

 private:
  static Element* MakeElementForTag(const std::string& tag) {
    if (tag == "A") return new HTMLAnchorElementImpl();
    if (tag == "IMG") return new HTMLImageElementImpl();
    if (tag == "INPUT") return new HTMLInputElementImpl();
    if (tag == "BUTTON") return new HTMLButtonElementImpl();
    if (tag == "FORM") return new HTMLFormElementImpl();
    if (tag == "SCRIPT") return new HTMLScriptElementImpl();
    if (tag == "SELECT") return new HTMLSelectElementImpl();
    if (tag == "TEXTAREA") return new HTMLTextAreaElementImpl();
    if (tag == "CANVAS") return new HTMLCanvasElementImpl();
    if (tag == "LABEL") return new HTMLLabelElementImpl();
    if (tag == "OPTION") return new HTMLOptionElementImpl();
    if (tag == "AUDIO") return new HTMLAudioElementImpl();
    if (tag == "VIDEO") return new HTMLVideoElementImpl();
    if (tag == "IFRAME") return new HTMLIFrameElementImpl();
    if (tag == "TEMPLATE") return new HTMLTemplateElementImpl();
    return new HTMLDivElementImpl(tag);
  }

  std::string title_;
  std::vector<std::unique_ptr<Element>> created_;
  std::vector<std::unique_ptr<Text>> created_text_;
  std::vector<std::unique_ptr<Comment>> created_comment_;
  std::vector<std::unique_ptr<DocumentFragment>> created_fragment_;
};

// Real most-derived-type dispatch for wrapping an Element* for JS --
// generated code (cpp_generator_v8.cc's EmitInterfaceReturn) calls this
// instead of the generic CreateElementBinding whenever the *static*
// return type is exactly `Element` (document.getElementById,
// querySelector, etc.), so e.g. an <img> now actually gets an
// HTMLImageElement-shaped wrapper (naturalWidth, src, ...) rather than a
// plain Element-shaped one with none of that. The dynamic_cast chain
// mirrors DocumentImpl::MakeElementForTag's tag->concrete-type mapping
// above exactly (same list, inverse direction) -- that mapping isn't
// derivable from the IDL itself (dom.bruja doesn't declare "HTMLImageElement
// is for tag IMG"), so this has to be hand-maintained the same way
// MakeElementForTag already is, not generated. Falls back to the generic
// Element wrapper for anything else (HTMLDivElementImpl's own tag-shaped
// wrapper, or any tag MakeElementForTag doesn't special-case, is still
// covered above; genuinely unknown Element subclasses fall through here).
inline v8::Local<v8::Object> WrapElementForJs(v8::Isolate* isolate, v8::Local<v8::Context> context,
                                              Element* el) {
  if (auto* p = dynamic_cast<HTMLAnchorElementImpl*>(el))
    return CreateHTMLAnchorElementBinding(isolate, context, p);
  if (auto* p = dynamic_cast<HTMLImageElementImpl*>(el))
    return CreateHTMLImageElementBinding(isolate, context, p);
  if (auto* p = dynamic_cast<HTMLInputElementImpl*>(el))
    return CreateHTMLInputElementBinding(isolate, context, p);
  if (auto* p = dynamic_cast<HTMLButtonElementImpl*>(el))
    return CreateHTMLButtonElementBinding(isolate, context, p);
  if (auto* p = dynamic_cast<HTMLFormElementImpl*>(el))
    return CreateHTMLFormElementBinding(isolate, context, p);
  if (auto* p = dynamic_cast<HTMLScriptElementImpl*>(el))
    return CreateHTMLScriptElementBinding(isolate, context, p);
  if (auto* p = dynamic_cast<HTMLSelectElementImpl*>(el))
    return CreateHTMLSelectElementBinding(isolate, context, p);
  if (auto* p = dynamic_cast<HTMLTextAreaElementImpl*>(el))
    return CreateHTMLTextAreaElementBinding(isolate, context, p);
  if (auto* p = dynamic_cast<HTMLCanvasElementImpl*>(el))
    return CreateHTMLCanvasElementBinding(isolate, context, p);
  if (auto* p = dynamic_cast<HTMLLabelElementImpl*>(el))
    return CreateHTMLLabelElementBinding(isolate, context, p);
  if (auto* p = dynamic_cast<HTMLOptionElementImpl*>(el))
    return CreateHTMLOptionElementBinding(isolate, context, p);
  if (auto* p = dynamic_cast<HTMLAudioElementImpl*>(el))
    return CreateHTMLAudioElementBinding(isolate, context, p);
  if (auto* p = dynamic_cast<HTMLVideoElementImpl*>(el))
    return CreateHTMLVideoElementBinding(isolate, context, p);
  if (auto* p = dynamic_cast<HTMLIFrameElementImpl*>(el))
    return CreateHTMLIFrameElementBinding(isolate, context, p);
  if (auto* p = dynamic_cast<HTMLTemplateElementImpl*>(el))
    return CreateHTMLTemplateElementBinding(isolate, context, p);
  if (auto* p = dynamic_cast<HTMLDivElementImpl*>(el))
    return CreateHTMLDivElementBinding(isolate, context, p);
  return CreateElementBinding(isolate, context, el);
}

// Reverse of WrapElementForJs: a JS value returned by getElementById/
// querySelector/etc. is now wrapped with whichever concrete per-tag
// template WrapElementForJs picked (HTMLImageElement, HTMLDivElement,
// ...), not always the generic Element one -- so anything that takes an
// Element argument *from* JS (e.g. getComputedStyle(el), see
// LocalFrameImpl::JsGetComputedStyle) needs to try every tag
// WrapElementForJs can produce, not just Element's own. Each
// v8::Object::Unwrap call is tag-checked (returns nullptr on a mismatch),
// so trying them in sequence is safe -- no blind reinterpret across the
// multiple-inheritance hierarchy the way accepting any wrappable tag
// with a single Unwrap<Element> call would risk.
inline Element* UnwrapElementFromJs(v8::Isolate* isolate, v8::Local<v8::Value> value) {
  (void)isolate;
  if (!value->IsObject()) return nullptr;
  v8::Local<v8::Object> obj = value.As<v8::Object>();

  EnsureV8HTMLAnchorElementTag();
  if (auto* p = v8::Object::Unwrap<HTMLAnchorElement>(
          obj, v8::CppHeapPointerTagRange(g_v8_HTMLAnchorElement_tag)))
    return p;
  EnsureV8HTMLImageElementTag();
  if (auto* p = v8::Object::Unwrap<HTMLImageElement>(
          obj, v8::CppHeapPointerTagRange(g_v8_HTMLImageElement_tag)))
    return p;
  EnsureV8HTMLInputElementTag();
  if (auto* p = v8::Object::Unwrap<HTMLInputElement>(
          obj, v8::CppHeapPointerTagRange(g_v8_HTMLInputElement_tag)))
    return p;
  EnsureV8HTMLButtonElementTag();
  if (auto* p = v8::Object::Unwrap<HTMLButtonElement>(
          obj, v8::CppHeapPointerTagRange(g_v8_HTMLButtonElement_tag)))
    return p;
  EnsureV8HTMLFormElementTag();
  if (auto* p = v8::Object::Unwrap<HTMLFormElement>(
          obj, v8::CppHeapPointerTagRange(g_v8_HTMLFormElement_tag)))
    return p;
  EnsureV8HTMLScriptElementTag();
  if (auto* p = v8::Object::Unwrap<HTMLScriptElement>(
          obj, v8::CppHeapPointerTagRange(g_v8_HTMLScriptElement_tag)))
    return p;
  EnsureV8HTMLSelectElementTag();
  if (auto* p = v8::Object::Unwrap<HTMLSelectElement>(
          obj, v8::CppHeapPointerTagRange(g_v8_HTMLSelectElement_tag)))
    return p;
  EnsureV8HTMLTextAreaElementTag();
  if (auto* p = v8::Object::Unwrap<HTMLTextAreaElement>(
          obj, v8::CppHeapPointerTagRange(g_v8_HTMLTextAreaElement_tag)))
    return p;
  EnsureV8HTMLCanvasElementTag();
  if (auto* p = v8::Object::Unwrap<HTMLCanvasElement>(
          obj, v8::CppHeapPointerTagRange(g_v8_HTMLCanvasElement_tag)))
    return p;
  EnsureV8HTMLLabelElementTag();
  if (auto* p = v8::Object::Unwrap<HTMLLabelElement>(
          obj, v8::CppHeapPointerTagRange(g_v8_HTMLLabelElement_tag)))
    return p;
  EnsureV8HTMLOptionElementTag();
  if (auto* p = v8::Object::Unwrap<HTMLOptionElement>(
          obj, v8::CppHeapPointerTagRange(g_v8_HTMLOptionElement_tag)))
    return p;
  EnsureV8HTMLAudioElementTag();
  if (auto* p = v8::Object::Unwrap<HTMLAudioElement>(
          obj, v8::CppHeapPointerTagRange(g_v8_HTMLAudioElement_tag)))
    return p;
  EnsureV8HTMLVideoElementTag();
  if (auto* p = v8::Object::Unwrap<HTMLVideoElement>(
          obj, v8::CppHeapPointerTagRange(g_v8_HTMLVideoElement_tag)))
    return p;
  EnsureV8HTMLIFrameElementTag();
  if (auto* p = v8::Object::Unwrap<HTMLIFrameElement>(
          obj, v8::CppHeapPointerTagRange(g_v8_HTMLIFrameElement_tag)))
    return p;
  EnsureV8HTMLTemplateElementTag();
  if (auto* p = v8::Object::Unwrap<HTMLTemplateElement>(
          obj, v8::CppHeapPointerTagRange(g_v8_HTMLTemplateElement_tag)))
    return p;
  EnsureV8HTMLDivElementTag();
  if (auto* p = v8::Object::Unwrap<HTMLDivElement>(
          obj, v8::CppHeapPointerTagRange(g_v8_HTMLDivElement_tag)))
    return p;
  EnsureV8ElementTag();
  if (auto* p =
          v8::Object::Unwrap<Element>(obj, v8::CppHeapPointerTagRange(g_v8_Element_tag)))
    return p;
  return nullptr;
}

class WindowImpl : public EventTargetImplT<Window> {
 public:
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

  // Not part of the generated interface: no real event loop backs
  // setTimeout/requestAnimationFrame here (see dom.bruja's header comment
  // on staying synchronous) -- an embedder that wants them to actually
  // fire drives this from its own pump loop.
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

}  // namespace bruja_dom_generated

#endif  // BRUJA_DOM_V8_IMPL_H_
