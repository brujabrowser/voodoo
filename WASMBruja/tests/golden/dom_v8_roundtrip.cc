// Same proof as dom_roundtrip.cc, against brujac's V8 backend -- the real
// stress test for interface inheritance (5 levels deep), interface-typed
// values (nullable + polymorphic-accepting-descendant), callbacks
// (addEventListener/dispatchEvent, setTimeout, requestAnimationFrame,
// MutationObserver), dictionaries (including dictionary inheritance:
// CustomEventInit : EventInit), sequences (childNodes, getAttributeNames,
// takeRecords), unions (ChildNode.before/after/replaceWith), Promise<T>
// (CustomElementRegistry.whenDefined), constructors (Text/Comment/
// DocumentFragment/Event/CustomEvent/MutationObserver/URL/
// URLSearchParams), and mixins (`includes` -- ParentNode/ChildNode/
// NonDocumentTypeChildNode).
//
// Reuses dom_impl.h's real, production-shaped implementation classes
// UNCHANGED (see dom_v8_impl.h in this same directory -- a copy with only
// #include "dom_gen.h" swapped for #include "dom_v8_gen.h", and
// CustomEventImpl's `any`-typed `detail` field adapted from raw
// JSValue/JSContext* to v8::Local<v8::Value>/no isolate needed, since a
// Local<Value> is already a real independent reference in this facade --
// see that file's own comment). Every other one of dom_impl.h's ~30
// concrete classes compiled against the V8-backend header with zero
// changes, strong evidence the two backends produce identically-shaped
// pure-virtual interface classes.
//
// One real, intentional behavioral difference from dom_roundtrip.cc,
// forced by the facade rather than chosen: assigning to a readonly
// attribute (document.readyState) does NOT throw here (no
// Isolate::ThrowException() yet -- see cpp_generator_v8.cc's file
// comment) -- it's a real, silent no-op instead, so the assertion checks
// the value is unchanged rather than that the assignment failed.
#include "bruja_dom/dom_v8_impl.h"

#include <cstdio>
#include <string>

#include "v8.h"

namespace bruja_dom_generated {
namespace {

bool Eval(v8::Local<v8::Context> context, const char* source) {
  v8::Isolate* isolate = context->GetIsolate();
  v8::Local<v8::String> src = v8::String::NewFromUtf8(isolate, source).ToLocalChecked();
  v8::MaybeLocal<v8::Script> script = v8::Script::Compile(context, src);
  if (script.IsEmpty()) return false;
  return !script.ToLocalChecked()->Run(context).IsEmpty();
}

std::string EvalString(v8::Local<v8::Context> context, const char* source) {
  v8::Isolate* isolate = context->GetIsolate();
  v8::Local<v8::String> src = v8::String::NewFromUtf8(isolate, source).ToLocalChecked();
  v8::MaybeLocal<v8::Script> script = v8::Script::Compile(context, src);
  if (script.IsEmpty()) return "<compile error>";
  v8::Local<v8::Value> result;
  if (!script.ToLocalChecked()->Run(context).ToLocal(&result)) return "<eval error>";
  v8::String::Utf8Value utf8(isolate, result);
  return std::string(*utf8, static_cast<size_t>(utf8.length()));
}

// v8::Promise isn't ported into the facade yet -- pumping the real
// quickjs-ng job queue underneath is still how a resolved Promise's
// .then() reaction actually runs, same escape-hatch spirit as
// WASMv16/src/engine.cc's own RunPendingJobs.
void RunPendingJobs(JSRuntime* rt) {
  JSContext* job_ctx;
  int ret;
  int n = 0;
  while ((ret = JS_ExecutePendingJob(rt, &job_ctx)) > 0 && n++ < 10000) {
  }
}

}  // namespace
}  // namespace bruja_dom_generated

using namespace bruja_dom_generated;

int main() {
  v8::Isolate* isolate = v8::Isolate::New();
  int failures = 0;
  {
    v8::Isolate::Scope isolate_scope(isolate);
    v8::HandleScope handle_scope(isolate);
    v8::Local<v8::Context> context = v8::Context::New(isolate);
    v8::Context::Scope context_scope(context);
    JSRuntime* rt = isolate->runtime_for_wasmv8_internal();

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

    HTMLDivElementImpl detached_div;  // never appended -- parentNode should read null

    HTMLAnchorElementImpl test_anchor;
    test_anchor.parent = &document_impl;
    HTMLInputElementImpl test_input;
    HTMLVideoElementImpl test_video;
    HTMLIFrameElementImpl test_iframe;
    test_iframe.content_window = &window_impl;
    HTMLTemplateElementImpl test_template;
    DocumentTypeImpl doctype_impl("html", "", "");
    EventImpl test_event("test", &window_impl, /*bubbles=*/true, /*cancelable=*/true);

    HTMLDivElementImpl child_node;
    child_node.parent = &document_impl;
    HTMLDivElementImpl before_node;
    HTMLDivElementImpl after_node;
    HTMLDivElementImpl replace_node;

    v8::Local<v8::Object> global = context->Global();

    InstallV8TextConstructor(isolate, context, global,
                             [](v8::Isolate*, const std::string& data) {
                               return new TextImpl(data);
                             });
    InstallV8CommentConstructor(isolate, context, global,
                                [](v8::Isolate*, const std::string& data) {
                                  return new CommentImpl(data);
                                });
    InstallV8DocumentFragmentConstructor(isolate, context, global, [](v8::Isolate*) {
      return new DocumentFragmentImpl();
    });
    InstallV8EventConstructor(
        isolate, context, global,
        [](v8::Isolate*, const std::string& type, const EventInit& init) {
          return new EventImpl(type, /*target=*/nullptr, init.bubbles, init.cancelable);
        });
    InstallV8CustomEventConstructor(
        isolate, context, global,
        [](v8::Isolate*, const std::string& type, const CustomEventInit& init) {
          return new CustomEventImpl(type, init.bubbles, init.cancelable, init.detail);
        });
    MutationObserverImpl* last_observer = nullptr;
    InstallV8MutationObserverConstructor(
        isolate, context, global,
        [&last_observer](v8::Isolate*, std::shared_ptr<MutationCallbackCallback> callback) {
          auto* obs = new MutationObserverImpl(std::move(callback));
          last_observer = obs;
          return obs;
        });
    InstallV8URLSearchParamsConstructor(
        isolate, context, global,
        [](v8::Isolate*, const std::string& init) { return new URLSearchParamsImpl(init); });
    InstallV8URLConstructor(
        isolate, context, global,
        [](v8::Isolate*, const std::string& url, const std::string& base) {
          return new URLImpl(url, base);
        });

    global->Set(isolate, "window", CreateWindowBinding(isolate, context, &window_impl));
    global->Set(isolate, "document", CreateDocumentBinding(isolate, context, &document_impl));
    global->Set(isolate, "detachedDiv",
               CreateHTMLDivElementBinding(isolate, context, &detached_div));
    global->Set(isolate, "testAnchor",
               CreateHTMLAnchorElementBinding(isolate, context, &test_anchor));
    global->Set(isolate, "testInput",
               CreateHTMLInputElementBinding(isolate, context, &test_input));
    global->Set(isolate, "testVideo",
               CreateHTMLVideoElementBinding(isolate, context, &test_video));
    global->Set(isolate, "testIframe",
               CreateHTMLIFrameElementBinding(isolate, context, &test_iframe));
    global->Set(isolate, "testTemplate",
               CreateHTMLTemplateElementBinding(isolate, context, &test_template));
    global->Set(isolate, "customElements",
               CreateCustomElementRegistryBinding(isolate, context, &custom_elements_impl));
    global->Set(isolate, "testEvent", CreateEventBinding(isolate, context, &test_event));
    global->Set(isolate, "doctype", CreateDocumentTypeBinding(isolate, context, &doctype_impl));
    global->Set(isolate, "childNode", CreateHTMLDivElementBinding(isolate, context, &child_node));
    global->Set(isolate, "beforeNode",
               CreateHTMLDivElementBinding(isolate, context, &before_node));
    global->Set(isolate, "afterNode", CreateHTMLDivElementBinding(isolate, context, &after_node));
    global->Set(isolate, "replaceNode",
               CreateHTMLDivElementBinding(isolate, context, &replace_node));

    auto check = [&](bool ok, const char* what) {
      if (!ok) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++failures;
      }
    };

    // readonly enum attribute + inherited const flattening.
    check(EvalString(context, "document.readyState") == "complete", "document.readyState");
    check(EvalString(context, "String(document.ELEMENT_NODE)") == "1",
         "Node.ELEMENT_NODE const flattened onto Document");
    // Facade has no throw yet -- assigning to a readonly attribute is a
    // real, silent no-op (see this file's top comment), not a thrown
    // error; the important thing is the value genuinely didn't change.
    Eval(context, "document.readyState = 'loading';");
    check(EvalString(context, "document.readyState") == "complete",
         "assigning readonly readyState is a no-op, value unchanged");

    // nullable interface attribute: null when unset, non-null via manual wiring.
    check(EvalString(context, "String(detachedDiv.parentNode)") == "null",
         "detached node's parentNode is null");
    check(EvalString(context, "document.documentElement.tagName") == "DIV",
         "document.documentElement");

    // createElement + attribute map + nullable DOMString return.
    check(Eval(context,
              "var el = document.createElement('a');\n"
              "el.setAttribute('class', 'foo');"),
         "createElement + setAttribute");
    check(EvalString(context, "el.tagName") == "A", "created element tagName");
    check(EvalString(context, "el.getAttribute('class')") == "foo", "getAttribute present");
    check(EvalString(context, "String(el.getAttribute('missing'))") == "null",
         "getAttribute missing returns null");
    check(EvalString(context, "el.hasAttribute('class')") == "true", "hasAttribute");
    check(EvalString(context, "el.getAttributeNames().indexOf('class') >= 0") == "true",
         "getAttributeNames (sequence<DOMString> return)");

    // appendChild: polymorphic Node-typed argument.
    check(Eval(context, "document.appendChild(el);"), "document.appendChild(element)");
    check(EvalString(context, "document.hasChildNodes()") == "true", "hasChildNodes");
    check(EvalString(context, "document.childNodes.length") == "1",
         "childNodes (sequence<Node> return)");
    check(EvalString(context, "document.childNodes[0].nodeName") == "A",
         "childNodes element re-wrapped as declared type Node");
    check(EvalString(context, "document.contains(el)") == "true", "Node.contains");
    check(EvalString(context, "document.cloneNode().nodeName") == "#document",
         "Node.cloneNode (optional bool param, Node return)");

    // ParentNode via `includes`.
    check(EvalString(context, "document.children.length") == "1",
         "ParentNode.children flattened onto Document via includes");
    check(EvalString(context, "document.firstElementChild.tagName") == "A",
         "ParentNode.firstElementChild via includes");
    check(EvalString(context, "String(document.childElementCount)") == "1",
         "ParentNode.childElementCount via includes");

    // ChildNode via `includes`.
    check(Eval(context, "document.appendChild(testAnchor);"),
         "append testAnchor for remove() test");
    check(EvalString(context, "document.childNodes.length") == "2",
         "two children now (el + testAnchor)");
    check(Eval(context, "testAnchor.remove();"), "ChildNode.remove via includes");
    check(EvalString(context, "document.childNodes.length") == "1",
         "remove() actually detached testAnchor, leaving el");

    // classList (DOMTokenList), a standalone interface referenced from Element.
    // Real, verified dom_impl.h behavior (cross-checked directly against
    // the original quickjs backend with the exact same setup): the
    // earlier `el.setAttribute('class', 'foo')` genuinely seeds classList
    // via Element::SetClassName -- add()'s "duplicates not re-added"
    // dedup applies on top of that pre-existing 'foo' token, so the
    // starting point here is length 1 ("foo"), not 0. This is dom_impl.h's
    // own real behavior (it's a different, "improved" reimplementation
    // from dom_roundtrip.cc's own separate inline test classes -- see
    // dom_impl.h's file comment), not anything specific to this backend.
    check(Eval(context, "el.classList.add('a', 'b', 'a');"), "DOMTokenList.add (variadic)");
    check(EvalString(context, "String(el.classList.length)") == "3",
         "DOMTokenList.length ('foo' from setAttribute + 'a'+'b', duplicate 'a' not re-added)");
    check(EvalString(context, "el.classList.contains('b')") == "true", "DOMTokenList.contains");
    check(EvalString(context, "el.classList.value") == "foo a b", "DOMTokenList.value getter");
    check(Eval(context, "el.classList.remove('a');"), "DOMTokenList.remove (variadic)");
    check(EvalString(context, "String(el.classList.length)") == "2", "DOMTokenList.remove worked");
    check(EvalString(context, "el.classList.toggle('c')") == "true", "DOMTokenList.toggle add");
    check(EvalString(context, "el.classList.contains('c')") == "true", "toggle actually added 'c'");

    // events: addEventListener/dispatchEvent/removeEventListener.
    check(Eval(context,
              "globalThis.fired = false;\n"
              "globalThis.handler = function(e) { globalThis.fired = (e.type === 'test'); };\n"
              "window.addEventListener('test', handler);"),
         "addEventListener");
    check(Eval(context, "window.dispatchEvent(testEvent);"), "dispatchEvent");
    check(EvalString(context, "fired") == "true", "listener observed the dispatched event");

    check(Eval(context,
              "globalThis.fired = false;\n"
              "window.removeEventListener('test', handler);\n"
              "window.dispatchEvent(testEvent);"),
         "removeEventListener + re-dispatch");
    check(EvalString(context, "fired") == "false", "removed listener should not fire");

    // setTimeout: optional-with-default + trailing variadic DOMString params.
    check(Eval(context,
              "globalThis.timerFired = false;\n"
              "globalThis.timerId = window.setTimeout(function() { "
              "globalThis.timerFired = true; }, 50, 'a', 'b');"),
         "setTimeout");
    check(EvalString(context, "typeof timerId") == "number", "setTimeout returns an id");
    window_impl.FireTimer(1);
    check(EvalString(context, "timerFired") == "true", "fired timer invoked the JS callback");

    // constructor(...).
    check(Eval(context, "globalThis.t = new Text('hello');"), "new Text(...)");
    check(EvalString(context, "t.data") == "hello", "constructed Text's data");
    check(EvalString(context, "String(t.nodeType)") == "3", "constructed Text's nodeType");
    check(Eval(context, "globalThis.c = new Comment('note');"), "new Comment(...)");
    check(EvalString(context, "c.data") == "note", "constructed Comment's data");
    check(Eval(context, "globalThis.frag = new DocumentFragment();"), "new DocumentFragment()");
    check(Eval(context, "frag.appendChild(t);"), "appendChild onto a constructed DocumentFragment");
    check(EvalString(context, "frag.hasChildNodes()") == "true",
         "constructed fragment has the child");

    // Promise<T>.
    check(Eval(context, "customElements.define('x-widget');"), "CustomElementRegistry.define");
    check(Eval(context,
              "globalThis.whenDefinedRan = false;\n"
              "customElements.whenDefined('x-widget').then(function() { "
              "globalThis.whenDefinedRan = true; });"),
         "CustomElementRegistry.whenDefined (Promise<void> return)");
    check(EvalString(context, "whenDefinedRan") == "false",
         "the .then() callback hasn't run yet (still a microtask)");
    RunPendingJobs(rt);
    check(EvalString(context, "whenDefinedRan") == "true",
         "pumping the job queue ran the resolved promise's .then() callback");

    // Concrete-leaf spot checks.
    check(Eval(context, "testAnchor.href = 'https://example.test/';"),
         "HTMLAnchorElement.href set");
    check(EvalString(context, "testAnchor.href") == "https://example.test/",
         "HTMLAnchorElement.href get");
    check(Eval(context, "testInput.checked = true;"), "HTMLInputElement.checked set");
    check(EvalString(context, "testInput.checked") == "true", "HTMLInputElement.checked get");
    check(EvalString(context, "typeof testInput.click") == "function",
         "HTMLElement.click flattened onto HTMLInputElement");

    check(EvalString(context, "testVideo.paused") == "true",
         "HTMLMediaElement.paused initial state");
    check(Eval(context, "testVideo.play();"),
         "HTMLMediaElement.play (flattened onto HTMLVideoElement)");
    check(EvalString(context, "testVideo.paused") == "false", "play() cleared paused");
    check(Eval(context, "testVideo.width = 640;"), "HTMLVideoElement.width (own member)");
    check(EvalString(context, "String(testVideo.width)") == "640", "HTMLVideoElement.width get");

    check(EvalString(context, "testIframe.contentWindow === undefined") == "false",
         "HTMLIFrameElement.contentWindow is not undefined");
    check(EvalString(context, "typeof testIframe.contentWindow.alert") == "function",
         "contentWindow (nullable interface attr) wraps a real Window");

    check(EvalString(context, "testTemplate.content.nodeName") == "#document-fragment",
         "HTMLTemplateElement.content (own DocumentFragment)");

    check(EvalString(context, "doctype.name") == "html", "DocumentType.name");
    check(EvalString(context, "String(doctype.nodeType)") == "10",
         "DocumentType.nodeType (Node const, DOCUMENT_TYPE_NODE)");
    check(EvalString(context, "typeof doctype.remove") == "function",
         "ChildNode.remove flattened onto DocumentType via includes");

    // constructor(...) + dictionary.
    check(Eval(context, "globalThis.ev = new Event('x', {bubbles: true, cancelable: false});"),
         "new Event(type, EventInit)");
    check(EvalString(context, "ev.type") == "x", "constructed Event's type");
    check(EvalString(context, "ev.bubbles") == "true",
         "constructed Event's bubbles (from EventInit)");
    check(EvalString(context, "ev.cancelable") == "false", "constructed Event's cancelable");

    // Dictionary inheritance.
    check(Eval(context, "globalThis.cev = new CustomEvent('y', {bubbles: true, detail: 42});"),
         "new CustomEvent(type, CustomEventInit)");
    check(EvalString(context, "cev.type") == "y", "constructed CustomEvent's type");
    check(EvalString(context, "cev.bubbles") == "true",
         "constructed CustomEvent's bubbles -- inherited from EventInit via dictionary "
         "inheritance");
    check(EvalString(context, "String(cev.detail)") == "42",
         "constructed CustomEvent's detail -- own field, any passthrough");

    // Storage.
    check(Eval(context, "window.localStorage.setItem('k', 'v');"), "Storage.setItem");
    check(EvalString(context, "window.localStorage.getItem('k')") == "v", "Storage.getItem");
    check(EvalString(context, "String(window.localStorage.length)") == "1", "Storage.length");
    check(EvalString(context, "String(window.localStorage.getItem('missing'))") == "null",
         "Storage.getItem missing key returns null");
    check(Eval(context, "window.localStorage.removeItem('k');"), "Storage.removeItem");
    check(EvalString(context, "String(window.localStorage.length)") == "0",
         "removeItem actually removed it");

    // MutationObserver: constructor(callback) + observe()/disconnect()/
    // takeRecords() (sequence<MutationRecord> return, empty here -- see
    // dom_impl.h's real MutationObserverImpl, which -- unlike
    // dom_roundtrip.cc's own test-only copy -- doesn't simulate a mutation
    // engine; MutationCallback's own JS-callback plumbing is already
    // proven above via addEventListener/setTimeout/requestAnimationFrame).
    check(Eval(context,
              "globalThis.observer = new MutationObserver(function(mutations, obs) {});\n"
              "observer.observe(document);"),
         "new MutationObserver(callback) + observe()");
    check(last_observer != nullptr, "factory captured the constructed MutationObserverImpl");
    check(EvalString(context, "Array.isArray(observer.takeRecords())") == "true",
         "takeRecords (sequence<MutationRecord> return, real materialized JS Array)");
    check(Eval(context, "observer.disconnect();"), "MutationObserver.disconnect");

    // requestAnimationFrame: double.
    check(Eval(context,
              "globalThis.rafTimestamp = -1;\n"
              "globalThis.rafId = window.requestAnimationFrame(function(t) { "
              "globalThis.rafTimestamp = t; });"),
         "requestAnimationFrame");
    window_impl.FireAnimationFrame(1, 1.5);
    check(EvalString(context, "rafTimestamp") == "1.5",
         "FrameRequestCallback's double timestamp round-tripped correctly");

    // navigator/console on Window.
    check(EvalString(context, "window.navigator.userAgent") == "WASMBruja-DOM/1.0",
         "window.navigator.userAgent");
    check(EvalString(context, "window.navigator.onLine") == "true", "window.navigator.onLine");
    check(Eval(context, "window.console.log('hello from JS');"), "window.console.log");
    check(console_impl.logs.size() == 1 && console_impl.logs[0] == "hello from JS",
         "console.log reached the C++ ConsoleImpl");

    // Node.insertBefore + ChildNode.before/after/replaceWith -- the union-
    // type showcase.
    check(Eval(context, "document.appendChild(childNode);"), "append childNode");
    check(EvalString(context, "String(document.childNodes.length)") == "2", "childNode appended");

    check(Eval(context, "childNode.before(beforeNode, 'ignored text');"),
         "ChildNode.before -- union+variadic: one Node arg, one DOMString arg, neither throws");
    check(EvalString(context, "String(document.childNodes.length)") == "3",
         "before()'s Node argument was actually inserted");
    check(EvalString(context, "document.childNodes[1].nodeName") == "DIV",
         "insertBefore placed beforeNode immediately before childNode");

    check(Eval(context, "childNode.after(afterNode);"), "ChildNode.after");
    check(EvalString(context, "String(document.childNodes.length)") == "4",
         "after()'s Node argument was inserted");

    check(Eval(context, "childNode.replaceWith(replaceNode);"), "ChildNode.replaceWith");
    check(EvalString(context, "String(document.childNodes.length)") == "4",
         "replaceWith swaps one node for another -- count unchanged");
    check(EvalString(context, "document.contains(childNode)") == "false",
         "replaceWith actually removed the original node");
    check(EvalString(context, "document.contains(replaceNode)") == "true",
         "replaceWith actually inserted the replacement");

    // URLSearchParams + URL.
    check(Eval(context, "globalThis.usp = new URLSearchParams('a=1&b=2');"),
         "new URLSearchParams(init)");
    check(EvalString(context, "usp.get('a')") == "1", "URLSearchParams.get, parsed from init");
    check(EvalString(context, "usp.has('b')") == "true", "URLSearchParams.has");
    check(Eval(context, "usp.set('a', '99');"), "URLSearchParams.set");
    check(EvalString(context, "usp.get('a')") == "99", "set() overwrote the existing value");
    check(Eval(context, "usp.append('c', '3');"), "URLSearchParams.append");
    check(EvalString(context, "usp.toString()") == "a=99&b=2&c=3", "URLSearchParams.toString");
    check(Eval(context, "usp.delete('b');"), "URLSearchParams.delete");
    check(EvalString(context, "usp.has('b')") == "false", "delete() actually removed it");

    check(Eval(context, "globalThis.u = new URL('https://example.test/path');"), "new URL(url)");
    check(Eval(context, "u.protocol = 'https:'; u.host = 'example.test';"),
         "URL component setters");
    check(EvalString(context, "u.protocol") == "https:", "URL.protocol getter");
    check(EvalString(context, "typeof u.searchParams.get") == "function",
         "URL.searchParams wired to a real URLSearchParams object");

    // Teardown: every interface's per-isolate ObjectTemplate (and
    // constructor template, for the constructible ones) must be cleared
    // before Dispose() -- see cpp_generator_v8.cc's EmitTeardownFunction.
    TeardownWindowV8Binding(isolate);
    TeardownDocumentV8Binding(isolate);
    TeardownHTMLDivElementV8Binding(isolate);
    TeardownHTMLAnchorElementV8Binding(isolate);
    TeardownHTMLInputElementV8Binding(isolate);
    TeardownHTMLVideoElementV8Binding(isolate);
    TeardownHTMLIFrameElementV8Binding(isolate);
    TeardownHTMLTemplateElementV8Binding(isolate);
    TeardownCustomElementRegistryV8Binding(isolate);
    TeardownEventV8Binding(isolate);
    TeardownDocumentTypeV8Binding(isolate);
    TeardownDOMTokenListV8Binding(isolate);
    TeardownTextV8Binding(isolate);
    TeardownCommentV8Binding(isolate);
    TeardownDocumentFragmentV8Binding(isolate);
    TeardownCustomEventV8Binding(isolate);
    TeardownMutationObserverV8Binding(isolate);
    TeardownMutationRecordV8Binding(isolate);
    TeardownURLSearchParamsV8Binding(isolate);
    TeardownURLV8Binding(isolate);
    TeardownStorageV8Binding(isolate);
    TeardownLocationV8Binding(isolate);
    TeardownNavigatorV8Binding(isolate);
    TeardownConsoleV8Binding(isolate);
    TeardownEventTargetV8Binding(isolate);
    TeardownNodeV8Binding(isolate);
    TeardownElementV8Binding(isolate);
    TeardownHTMLElementV8Binding(isolate);
    TeardownCharacterDataV8Binding(isolate);
    TeardownHTMLMediaElementV8Binding(isolate);
    TeardownHTMLButtonElementV8Binding(isolate);
    TeardownHTMLFormElementV8Binding(isolate);
    TeardownHTMLScriptElementV8Binding(isolate);
    TeardownHTMLSelectElementV8Binding(isolate);
    TeardownHTMLTextAreaElementV8Binding(isolate);
    TeardownHTMLCanvasElementV8Binding(isolate);
    TeardownHTMLLabelElementV8Binding(isolate);
    TeardownHTMLOptionElementV8Binding(isolate);
    TeardownHTMLAudioElementV8Binding(isolate);
    TeardownHTMLVideoElementV8Binding(isolate);
    TeardownHTMLImageElementV8Binding(isolate);
  }
  isolate->Dispose();

  if (failures > 0) {
    std::fprintf(stderr, "dom_v8_roundtrip: %d failure(s)\n", failures);
    return 1;
  }
  std::printf("dom_v8_roundtrip: OK\n");
  return 0;
}
