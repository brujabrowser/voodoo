// Bruja occupancy. Source is voodoomc output (published chromium/src
// .mojom + sibling .voodoom). Engines loaded as-is:
//   content::RendererImpl.CreateFrame → pending_remote<blink.LocalFrame>
//   blink::LocalFrameImpl behind that remote (Navigate/LoadHTML/Eval/paint)
//   lime::FrameWindow CaptureFrame → WASMSkia + WASMRasta
// Not WASMv16 bruja_browser. Not Lime ContextProviderMain. Not Loki.

#include "content/renderer_impl.h"
#include "lime/frame_window.h"

#include "mojo/public/cpp/bindings/remote.h"
#include "renderer_interface_gen.h"
#include "whp/base/executor.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <cstdio>
#include <memory>
#include <string>

namespace {

constexpr int kChromeH = 36;
constexpr int kIdGo = 1001;
constexpr wchar_t kClass[] = L"BrujaBrowser";

constexpr char kAboutBruja[] =
    "<!doctype html><html><head><meta charset=\"utf-8\"><title>Bruja</title>"
    "<style>body{font-family:system-ui,sans-serif;margin:2em;background:#111;color:#eee}"
    "h1{font-weight:500}code{color:#8cf}</style></head>"
    "<body><h1>Bruja</h1>"
    "<p>Occupancy of published Chromium mojo. Frame is "
    "<code>blink.LocalFrame</code> (LocalFrameImpl). "
    "<code>content.Renderer.CreateFrame</code> include hop.</p>"
    "<p>User-Agent: Bruja/0</p>"
    "</body></html>";

void Pump(int n = 16) {
  for (int i = 0; i < n; ++i) whp::Executor::Current().RunUntilIdle();
}

template <typename Done>
bool PumpUntil(Done done, int max_idle = 128) {
  for (int i = 0; i < max_idle && !done(); ++i) {
    whp::Executor::Current().RunUntilIdle();
  }
  return done();
}

std::string WideToUtf8(const wchar_t* w) {
  if (!w || !*w) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
  if (n <= 1) return {};
  std::string s(static_cast<size_t>(n - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, w, -1, s.data(), n, nullptr, nullptr);
  return s;
}

std::wstring Utf8ToWide(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring w(static_cast<size_t>(n), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
  return w;
}

struct Browser {
  content::RendererImpl renderer{"unused", 0};
  mojo::Remote<blink::LocalFrame> frame;
  std::unique_ptr<lime::FrameWindow> view;
  HWND hwnd = nullptr;
  HWND url = nullptr;
  HWND go = nullptr;
  WNDPROC url_prev = nullptr;
  bool chrome_ready = false;
  bool navigating = false;

  void BindFrame() {
    renderer.CreateFrame([this](mojo::PendingRemote<blink::LocalFrame> pending) {
      frame.Bind(std::move(pending));
    });
  }

  void Layout() {
    RECT r{};
    GetClientRect(hwnd, &r);
    int w = r.right - r.left;
    int h = r.bottom - r.top;
    int btn = 72;
    if (url) MoveWindow(url, 8, 6, w - btn - 24, kChromeH - 12, TRUE);
    if (go) MoveWindow(go, w - btn - 8, 6, btn, kChromeH - 12, TRUE);
    if (view) {
      view->SetBounds(0, kChromeH, w, h > kChromeH ? h - kChromeH : 1);
    }
  }

  void EnsureView() {
    if (view || !hwnd || !frame) return;
    RECT r{};
    GetClientRect(hwnd, &r);
    lime::FrameWindowOptions opt;
    opt.parent = hwnd;
    opt.x = 0;
    opt.y = kChromeH;
    opt.width = r.right - r.left;
    opt.height = (r.bottom - r.top) > kChromeH ? (r.bottom - r.top) - kChromeH : 1;
    opt.child = true;
    opt.visible = true;
    view = std::make_unique<lime::FrameWindow>(frame.get(), nullptr, opt);
    view->Repaint();
  }

  void Go(const std::string& target) {
    if (navigating || !frame) return;
    navigating = true;
    if (view) view->NoteExternalRemoteOp(true);

    bool done = false;
    bool ok = false;
    std::string error;
    std::string title;
    auto finish = [&](bool o, std::string t, uint32_t, uint32_t, std::string e) {
      ok = o;
      title = std::move(t);
      error = std::move(e);
      done = true;
    };

    if (target == "about:bruja" || target == "about:blank" || target.empty()) {
      const char* html = target == "about:blank" ? "<!doctype html><title></title>" : kAboutBruja;
      frame->LoadHTML(html, std::move(finish));
    } else {
      frame->Navigate(target, std::move(finish));
    }

    if (!PumpUntil([&] { return done; })) {
      std::fprintf(stderr, "navigate timeout: %s\n", target.c_str());
    } else if (!ok) {
      std::fprintf(stderr, "navigate failed: %s\n", error.c_str());
    } else {
      std::printf("navigate ok title=%s\n", title.c_str());
      std::fflush(stdout);
    }

    EnsureView();
    if (view) {
      view->NoteExternalRemoteOp(false);
      view->Repaint();
    }
    navigating = false;
    std::string caption = title.empty() ? (target.empty() ? "Bruja" : target) : title;
    SetWindowTextW(hwnd, Utf8ToWide(caption).c_str());
    if (url) SetWindowTextW(url, Utf8ToWide(target.empty() ? "about:bruja" : target).c_str());
  }

  void GoFromBar() {
    wchar_t buf[4096];
    GetWindowTextW(url, buf, 4096);
    Go(WideToUtf8(buf));
  }
};

LRESULT CALLBACK UrlProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  auto* app = reinterpret_cast<Browser*>(GetWindowLongPtrW(GetParent(hwnd), GWLP_USERDATA));
  if (msg == WM_KEYDOWN && wparam == VK_RETURN && app) {
    app->GoFromBar();
    return 0;
  }
  if (!app || !app->url_prev) return DefWindowProcW(hwnd, msg, wparam, lparam);
  return CallWindowProcW(app->url_prev, hwnd, msg, wparam, lparam);
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
  auto* app = reinterpret_cast<Browser*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
  switch (msg) {
    case WM_COMMAND:
      if (app && app->chrome_ready && LOWORD(wparam) == kIdGo && HIWORD(wparam) == BN_CLICKED) {
        app->GoFromBar();
      }
      return 0;
    case WM_TIMER:
      Pump(4);
      return 0;
    case WM_SIZE:
      if (app) app->Layout();
      return 0;
    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;
    default:
      return DefWindowProcW(hwnd, msg, wparam, lparam);
  }
}

}  // namespace

int main(int argc, char** argv) {
  std::printf("%s\n", blink::LocalFrame::intern_key());
  std::printf("%s\n", content::Renderer::intern_key());
  std::printf("pending_remote<blink.LocalFrame>\n");
  std::printf("func content.Renderer.CreateFrame()->(pending_remote<blink.LocalFrame>)\n");
  std::printf("func blink.LocalFrame.LoadHTML(string)->(bool,string,uint32,uint32,string)\n");
  std::printf("func blink.LocalFrame.Navigate(string)->(bool,string,uint32,uint32,string)\n");
  std::fflush(stdout);

  auto app = std::make_unique<Browser>();
  app->BindFrame();
  if (!app->frame) {
    std::fprintf(stderr, "Renderer.CreateFrame produced no LocalFrame remote\n");
    return 1;
  }

  HINSTANCE inst = GetModuleHandleW(nullptr);
  WNDCLASSW wc{};
  wc.lpfnWndProc = WndProc;
  wc.hInstance = inst;
  wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
  wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH));
  wc.lpszClassName = kClass;
  RegisterClassW(&wc);

  app->hwnd = CreateWindowExW(0, kClass, L"Bruja",
                              WS_OVERLAPPEDWINDOW | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                              CW_USEDEFAULT, CW_USEDEFAULT, 1100, 800, nullptr, nullptr, inst,
                              nullptr);
  SetWindowLongPtrW(app->hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(app.get()));

  app->url = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"about:bruja",
                             WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | ES_LEFT, 8, 6, 900, 24,
                             app->hwnd, nullptr, inst, nullptr);
  app->go = CreateWindowExW(0, L"BUTTON", L"Go", WS_CHILD | WS_VISIBLE | BS_DEFPUSHBUTTON, 920, 6,
                            72, 24, app->hwnd, reinterpret_cast<HMENU>(static_cast<intptr_t>(kIdGo)),
                            inst, nullptr);
  app->url_prev = reinterpret_cast<WNDPROC>(
      SetWindowLongPtrW(app->url, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(UrlProc)));
  SendMessageW(app->url, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
  SendMessageW(app->go, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
  app->Layout();
  SetTimer(app->hwnd, 1, 16, nullptr);
  app->chrome_ready = true;

  std::string start = argc > 1 ? argv[1] : "about:bruja";
  SetWindowTextW(app->url, Utf8ToWide(start).c_str());
  app->Go(start);

  MSG msg;
  for (;;) {
    while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
      if (msg.message == WM_QUIT) return 0;
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
    }
    Pump(2);
    WaitMessage();
  }
}
