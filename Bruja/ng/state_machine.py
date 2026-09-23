# Install MessageServiceHost on the Hangouts background, then send from the other frame.
# hello, ordinal, getLog, a missing method, and the promise path.
import json
import os
import socket
import subprocess
import time
import urllib.request

ADDR = "127.0.0.1"
PORT = 9345
PROFILE = r"C:\Users\grego\Bruja\out\mojojs-chrome-host3"
HOST_JS = r"C:\Users\grego\Bruja\ng\ext\host.js"
OUT = r"C:\Users\grego\Bruja\out\state-machine.txt"

DRIVER = r"""
(async () => {
  const frame = await new Promise((resolve, reject) => {
    const node = document.createElement("iframe");
    node.src = location.href;
    node.onload = () => resolve(node);
    setTimeout(() => reject(new Error("frame timeout")), 3000);
    document.documentElement.appendChild(node);
  });
  const rt = frame.contentWindow.chrome.runtime;
  function call(method, args) {
    const env = {
      magic: 91556947316803,
      host: "offscreen",
      method: method,
      args: args || [],
      extension_id: chrome.runtime.id,
      channel: "offscreen"
    };
    return new Promise((resolve) => {
      let done = false;
      const finish = (s) => { if (!done) { done = true; resolve(s); } };
      setTimeout(() => finish("timeout"), 2000);
      try {
        rt.sendMessage(env, (h) => {
          let err = "";
          try { if (rt.lastError) err = String(rt.lastError.message || rt.lastError); } catch (e) {}
          let body = "";
          try { body = h === undefined ? "undefined" : JSON.stringify(h); } catch (e) { body = "unstringifiable"; }
          finish("lastError=" + (err || "none") + " reply=" + body);
        });
      } catch (e) { finish("throw " + (e && e.message ? e.message : "error")); }
    });
  }
  const rows = ["extension=" + chrome.runtime.id];
  rows.push("hello " + await call("hello", []));
  rows.push("dagger " + await call("ordinal", [941764473]));
  rows.push("voodoo " + await call("ordinal", [1481416133]));
  rows.push("mojo " + await call("ordinal", [2592569]));
  rows.push("pending " + await call("pending", [2592569]));
  rows.push("missing " + await call("nope", []));
  rows.push("getLog " + await call("getLog", []));
  return rows.join("\n");
})()
"""


def chrome_path():
    candidates = [
        os.path.join(os.environ.get("ProgramFiles", r"C:\Program Files"), r"Google\Chrome\Application\chrome.exe"),
        os.path.join(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"), r"Google\Chrome\Application\chrome.exe"),
        os.path.join(os.environ.get("LOCALAPPDATA", ""), r"Google\Chrome\Application\chrome.exe"),
    ]
    for path in candidates:
        if path and os.path.isfile(path):
            return path
    raise SystemExit("chrome.exe not found")


def recvn(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise RuntimeError("socket closed")
        buf += chunk
    return buf


def send_text(sock, text):
    data = text.encode("utf-8")
    mask = os.urandom(4)
    masked = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
    ln = len(data)
    hdr = bytearray([0x81])
    if ln < 126:
        hdr.append(0x80 | ln)
    elif ln < 65536:
        hdr.append(0x80 | 126)
        hdr += ln.to_bytes(2, "big")
    else:
        hdr.append(0x80 | 127)
        hdr += ln.to_bytes(8, "big")
    sock.sendall(bytes(hdr) + mask + masked)


def recv_text(sock):
    parts = []
    while True:
        b0, b1 = recvn(sock, 2)
        opcode = b0 & 0x0F
        ln = b1 & 0x7F
        if ln == 126:
            ln = int.from_bytes(recvn(sock, 2), "big")
        elif ln == 127:
            ln = int.from_bytes(recvn(sock, 8), "big")
        if b1 & 0x80:
            mask = recvn(sock, 4)
            data = bytes(b ^ mask[i % 4] for i, b in enumerate(recvn(sock, ln)))
        else:
            data = recvn(sock, ln)
        if opcode == 0x8:
            raise RuntimeError("websocket closed")
        if opcode == 0x9:
            # pong
            mask = os.urandom(4)
            masked = bytes(b ^ mask[i % 4] for i, b in enumerate(data))
            hdr = bytearray([0x8A, 0x80 | len(data)])
            sock.sendall(bytes(hdr) + mask + masked)
            continue
        if opcode in (0x1, 0x0):
            parts.append(data)
            if b0 & 0x80:
                return b"".join(parts).decode("utf-8", "replace")
            continue
        if opcode == 0xA:
            continue


def connect_ws(url):
    rest = url[len("ws://"):]
    hostport, path = rest.split("/", 1)
    host, port = hostport.split(":")
    path = "/" + path
    sock = socket.create_connection((host, int(port)), timeout=8)
    key = "dGhlIHNhbXBsZSBub25jZQ=="
    req = (
        f"GET {path} HTTP/1.1\r\n"
        f"Host: {hostport}\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n"
    )
    sock.sendall(req.encode("ascii"))
    buf = b""
    while b"\r\n\r\n" not in buf:
        chunk = sock.recv(4096)
        if not chunk:
            raise RuntimeError("websocket handshake closed")
        buf += chunk
    head = buf.split(b"\r\n\r\n", 1)[0].decode("ascii", "replace")
    if "101" not in head.split("\r\n", 1)[0]:
        raise RuntimeError("websocket handshake failed: " + head.split("\r\n", 1)[0])
    return sock


def cdp(sock, msg_id, method, params=None, timeout=20):
    body = {"id": msg_id, "method": method}
    if params is not None:
        body["params"] = params
    send_text(sock, json.dumps(body))
    sock.settimeout(timeout)
    while True:
        text = recv_text(sock)
        obj = json.loads(text)
        if obj.get("id") == msg_id:
            return obj


def find_worker():
    for _ in range(25):
        try:
            with urllib.request.urlopen(f"http://{ADDR}:{PORT}/json/list", timeout=2) as resp:
                targets = json.loads(resp.read().decode("utf-8"))
        except Exception:
            time.sleep(0.2)
            continue
        hangouts = None
        background = None
        for t in targets:
            url = t.get("url") or ""
            ws = t.get("webSocketDebuggerUrl") or ""
            if not ws:
                continue
            if "nkeimhogjdpnpccoofpliimaahmaaome" in url:
                hangouts = ws
            if t.get("type") == "background_page" and background is None:
                background = ws
        if hangouts or background:
            return hangouts or background
        time.sleep(0.2)
    raise SystemExit("extension background target missing")


def main():
    os.makedirs(PROFILE, exist_ok=True)
    chrome = chrome_path()
    proc = subprocess.Popen(
        [
            chrome,
            "--headless=new",
            f"--remote-debugging-port={PORT}",
            "--remote-allow-origins=*",
            "--enable-blink-features=MojoJS,MojoJSTest",
            f"--user-data-dir={PROFILE}",
            "--no-first-run",
            "--no-default-browser-check",
            "about:blank",
        ]
    )
    try:
        deadline = time.time() + 15
        while time.time() < deadline:
            try:
                with socket.create_connection((ADDR, PORT), timeout=0.3):
                    break
            except OSError:
                time.sleep(0.2)
        else:
            raise SystemExit(f"devtools port {PORT} did not open")
        ws = find_worker()
        print("worker", ws, flush=True)
        sock = connect_ws(ws)
        sock.settimeout(8)
        enabled = cdp(sock, 1, "Runtime.enable")
        print("enable", "ok" if "result" in enabled else enabled, flush=True)
        host_js = open(HOST_JS, encoding="utf-8").read()
        opened = cdp(sock, 2, "Runtime.evaluate", {"expression": host_js, "returnByValue": True})
        print("host", json.dumps(opened.get("result", opened)), flush=True)
        ran = cdp(
            sock,
            3,
            "Runtime.evaluate",
            {
                "expression": DRIVER,
                "awaitPromise": True,
                "returnByValue": True,
                "timeout": 20000,
            },
            timeout=25,
        )
        value = (((ran.get("result") or {}).get("result") or {}).get("value")) or ""
        print("capture", flush=True)
        print(value if value else json.dumps(ran), flush=True)
        with open(OUT, "w", encoding="utf-8") as f:
            f.write(value if isinstance(value, str) else json.dumps(value))
        sock.close()
    finally:
        subprocess.run(["taskkill.exe", "/PID", str(proc.pid), "/T", "/F"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


if __name__ == "__main__":
    main()
