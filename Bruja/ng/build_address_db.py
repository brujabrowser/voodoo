# Fill address-db.db from the host. Each address is ordinal.
# The reply is the handler return, including the log size after the write.
import json
import os
import socket
import sqlite3
import subprocess
import time

import state_machine as sm

PORT = 9346
PROFILE = r"C:\Users\grego\Bruja\out\mojojs-chrome-dbhost"
PORTFOLIO = r"C:\Users\grego\Bruja\out\mojovm-portfolio.txt"
DB = r"C:\Users\grego\Bruja\out\address-db.db"
MD = r"C:\Users\grego\Bruja\out\address-db.md"
CHUNK = 200

sm.PORT = PORT

PROBE = r"""
(async () => {
  const ordinals = ORDINAL_LIST;
  if (!globalThis.__frame || !globalThis.__frame.contentWindow) {
    globalThis.__frame = await new Promise((resolve, reject) => {
      const frame = document.createElement("iframe");
      frame.src = location.href;
      frame.onload = () => resolve(frame);
      setTimeout(() => reject(new Error("frame timeout")), 3000);
      document.documentElement.appendChild(frame);
    });
  }
  const rt = globalThis.__frame.contentWindow.chrome.runtime;
  const lines = ["EXT\t" + chrome.runtime.id];
  for (let k = 0; k < ordinals.length; k++) {
    const ord = ordinals[k];
    const env = {
      magic: 91556947316803,
      host: "offscreen",
      method: "ordinal",
      args: [ord],
      extension_id: chrome.runtime.id,
      channel: "offscreen"
    };
    const reply = await new Promise((resolve) => {
      let done = false;
      const finish = (s) => { if (!done) { done = true; resolve(s); } };
      setTimeout(() => finish("timeout"), 1500);
      try {
        rt.sendMessage(env, (h) => {
          let err = "";
          try { if (rt.lastError) err = String(rt.lastError.message || rt.lastError); } catch (e) {}
          let body = "";
          try { body = h === undefined ? "undefined" : JSON.stringify(h); } catch (e) { body = "unstringifiable"; }
          finish((err || "none") + " reply=" + body);
        });
      } catch (e) { finish("throw " + (e && e.message ? e.message : "error")); }
    });
    lines.push(String(k) + "\t" + String(ord) + "\t" + reply.replace(/\t/g, " ").replace(/\n/g, " "));
  }
  return lines.join("\n");
})()
"""


def fnv1(text):
    h = 2166136261
    for b in text.encode("utf-8"):
        h = (h * 16777619) & 0xFFFFFFFF
        h ^= b
    h &= 0x7FFFFFFF
    return h or 1


def load_methods():
    rows = []
    with open(PORTFOLIO, "r", encoding="utf-8") as f:
        for line in f:
            if not line or line.startswith("#"):
                continue
            parts = line.rstrip("\n").split("\t")
            if len(parts) < 3:
                continue
            name, voodoo, mojo = parts[0], int(parts[1]), int(parts[2])
            fqn = parts[3] if len(parts) > 3 else ""
            path = parts[4] if len(parts) > 4 else ""
            method = name.rsplit(".", 1)[-1]
            rows.append((name, method, fnv1(method), voodoo, mojo, fqn, path))
    return rows


def parse_reply(text):
    last_error = text
    body = ""
    if " reply=" in text:
        last_error, body = text.split(" reply=", 1)
        last_error = last_error.strip()
    recorded = 0
    log_size = None
    state = ""
    try:
        obj = json.loads(body) if body.startswith("{") else None
    except json.JSONDecodeError:
        obj = None
    if isinstance(obj, dict):
        result = obj.get("result") if isinstance(obj.get("result"), dict) else {}
        if result.get("recorded") is True:
            recorded = 1
            state = result.get("state") or "recorded"
        elif obj.get("error"):
            state = "error"
        log_size = result.get("logSize")
        if isinstance(log_size, bool) or not isinstance(log_size, int):
            log_size = None
    return last_error, recorded, log_size, state, body or text


def evaluate(sock, msg_id, expression, timeout):
    ran = sm.cdp(
        sock,
        msg_id,
        "Runtime.evaluate",
        {
            "expression": expression,
            "awaitPromise": True,
            "returnByValue": True,
            "timeout": timeout * 1000,
        },
        timeout=timeout + 5,
    )
    value = ((ran.get("result") or {}).get("result") or {}).get("value")
    if not isinstance(value, str):
        raise SystemExit("no value id %s %s" % (msg_id, json.dumps(ran)[:500]))
    return value


def main():
    methods = load_methods()
    if len(methods) != 9062:
        raise SystemExit("portfolio rows %d" % len(methods))
    sends = []
    for name, method, dagger, voodoo, mojo, fqn, path in methods:
        sends.append((name, "dagger", dagger))
        sends.append((name, "voodoo", voodoo))
        sends.append((name, "mojo", mojo))

    os.makedirs(PROFILE, exist_ok=True)
    proc = subprocess.Popen(
        [
            sm.chrome_path(),
            "--headless=new",
            "--remote-debugging-port=%d" % PORT,
            "--remote-allow-origins=*",
            "--enable-blink-features=MojoJS,MojoJSTest",
            "--user-data-dir=%s" % PROFILE,
            "--no-first-run",
            "--no-default-browser-check",
            "about:blank",
        ]
    )
    replies = []
    ext_id = ""
    stat = ""
    try:
        deadline = time.time() + 15
        while time.time() < deadline:
            try:
                with socket.create_connection((sm.ADDR, PORT), timeout=0.3):
                    break
            except OSError:
                time.sleep(0.2)
        else:
            raise SystemExit("devtools port %d did not open" % PORT)
        ws = sm.find_worker()
        print("worker", ws, flush=True)
        sock = sm.connect_ws(ws)
        sock.settimeout(8)
        sm.cdp(sock, 1, "Runtime.enable")
        host_js = open(sm.HOST_JS, encoding="utf-8").read()
        opened = sm.cdp(sock, 2, "Runtime.evaluate", {"expression": host_js, "returnByValue": True})
        host_value = ((opened.get("result") or {}).get("result") or {}).get("value")
        print("host", host_value, flush=True)
        if host_value != "host-open offscreen":
            raise SystemExit("host did not open")
        msg_id = 3
        for i in range(0, len(sends), CHUNK):
            chunk = sends[i:i + CHUNK]
            expr = PROBE.replace("ORDINAL_LIST", json.dumps([addr for _, _, addr in chunk]))
            value = evaluate(sock, msg_id, expr, 90)
            msg_id += 1
            for line in value.split("\n"):
                if line.startswith("EXT\t"):
                    ext_id = line.split("\t", 1)[1]
                    continue
                parts = line.split("\t", 2)
                if len(parts) != 3:
                    continue
                k = int(parts[0])
                addr = int(parts[1])
                name, space, expect = chunk[k]
                if addr != expect:
                    raise SystemExit("address mismatch %s %s" % (addr, expect))
                last_error, recorded, log_size, state, body = parse_reply(parts[2])
                replies.append((name, space, addr, last_error, recorded, log_size, state, body))
            print("chunk %d/%d" % (min(i + CHUNK, len(sends)), len(sends)), flush=True)
        stat = evaluate(
            sock,
            msg_id + 1,
            r"""
(async () => {
  const rt = globalThis.__frame.contentWindow.chrome.runtime;
  return await new Promise((resolve) => {
    rt.sendMessage({
      magic: 91556947316803,
      host: "offscreen",
      method: "stat",
      args: [],
      extension_id: chrome.runtime.id,
      channel: "offscreen"
    }, (h) => resolve(JSON.stringify(h)));
  });
})()
""",
            20,
        )
        print("stat", stat, flush=True)
        sock.close()
    finally:
        subprocess.run(
            ["taskkill.exe", "/PID", str(proc.pid), "/T", "/F"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

    if len(replies) != len(sends):
        raise SystemExit("replies %d sends %d" % (len(replies), len(sends)))
    recorded = sum(row[4] for row in replies)
    write_db(methods, replies, ext_id, stat, recorded)
    write_md(methods, replies, ext_id, stat, recorded)
    print("wrote", DB, flush=True)
    print("recorded", recorded, "of", len(replies), flush=True)


def trash(path):
    if not os.path.exists(path):
        return
    subprocess.run(
        [
            "powershell",
            "-NoProfile",
            "-Command",
            "Add-Type -AssemblyName Microsoft.VisualBasic; "
            "[Microsoft.VisualBasic.FileIO.FileSystem]::DeleteFile('%s', 'OnlyErrorDialogs', 'SendToRecycleBin')"
            % path,
        ],
        check=True,
    )


def write_db(methods, replies, ext_id, stat, recorded):
    trash(DB)
    con = sqlite3.connect(DB)
    con.executescript(
        """
        CREATE TABLE meta (key TEXT PRIMARY KEY, value TEXT NOT NULL);
        CREATE TABLE methods (
          name TEXT PRIMARY KEY,
          method TEXT NOT NULL,
          dagger INTEGER NOT NULL,
          voodoo INTEGER NOT NULL,
          mojo INTEGER NOT NULL,
          fqn TEXT NOT NULL,
          path TEXT NOT NULL
        );
        CREATE TABLE replies (
          name TEXT NOT NULL,
          space TEXT NOT NULL,
          address INTEGER NOT NULL,
          last_error TEXT NOT NULL,
          recorded INTEGER NOT NULL,
          log_size INTEGER,
          state TEXT NOT NULL,
          reply TEXT NOT NULL,
          PRIMARY KEY (name, space)
        );
        CREATE INDEX replies_address ON replies(address);
        CREATE INDEX replies_space ON replies(space);
        """
    )
    con.executemany(
        "INSERT INTO methods VALUES (?,?,?,?,?,?,?)",
        methods,
    )
    con.executemany(
        "INSERT INTO replies VALUES (?,?,?,?,?,?,?,?)",
        replies,
    )
    con.executemany(
        "INSERT INTO meta VALUES (?,?)",
        [
            ("magic", "91556947316803"),
            ("host", "offscreen"),
            ("extension", ext_id),
            ("methods", str(len(methods))),
            ("sends", str(len(replies))),
            ("recorded", str(recorded)),
            ("stat", stat),
            ("path", DB),
        ],
    )
    con.commit()
    con.close()


def write_md(methods, replies, ext_id, stat, recorded):
    by_key = {(row[0], row[1]): row for row in replies}
    lines = [
        "# address db",
        "",
        "The database file is `C:\\Users\\grego\\Bruja\\out\\address-db.db`.",
        "",
        "SECRPC `91556947316803` via `chrome.runtime.sendMessage` to host `offscreen` on extension `%s`."
        % ext_id,
        "Each address is method `ordinal`. The handler records it, then the reply carries `logSize` after that write.",
        "",
        "%d methods. %d sends. %d recorded. Closing stat `%s`."
        % (len(methods), len(replies), recorded, stat.replace("|", "/")),
        "",
        "dagger is FNV-1 of the method name. voodoo is FNV-1a of the fully qualified ABI name. mojo is the Chrome scramble ordinal.",
        "",
        "| name | dagger | voodoo | mojo | dagger log | voodoo log | mojo log |",
        "| --- | --- | --- | --- | --- | --- | --- |",
    ]
    for name, method, dagger, voodoo, mojo, fqn, path in methods:
        cells = []
        for space, addr in (("dagger", dagger), ("voodoo", voodoo), ("mojo", mojo)):
            row = by_key[(name, space)]
            log_size = row[5]
            cells.append("" if log_size is None else str(log_size))
        lines.append(
            "| %s | %d | %d | %d | %s | %s | %s |"
            % (name.replace("|", "/"), dagger, voodoo, mojo, cells[0], cells[1], cells[2])
        )
    with open(MD, "w", encoding="utf-8", newline="\n") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
