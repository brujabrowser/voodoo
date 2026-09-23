// MessageServiceHost for the offscreen side of the channel.
// A method runs first. sendResponse carries that return value.
// A promise method returns true (ResponsePending) and replies when it finishes.
(function () {
  if (globalThis.__host && globalThis.__host.opened) return "host-already";
  const SECRPC = 91556947316803;
  const log = [];
  const handlers = {};
  function register(name, fn) { handlers[name] = fn; }

  register("hello", function () {
    return { ready: true, host: "offscreen" };
  });
  register("ordinal", function (address) {
    log.push({ method: "ordinal", address: address, state: "recorded" });
    return { recorded: true, address: address, logSize: log.length };
  });
  register("getLog", function () {
    return { entries: log.slice() };
  });
  register("stat", function () {
    return { logSize: log.length, last: log.length ? log[log.length - 1] : null };
  });
  register("pending", function (address) {
    return Promise.resolve({ state: "ResponsePending", address: address });
  });

  const host = {
    name: "offscreen",
    opened: false,
    open: function () {
      if (host.opened) return;
      host.opened = true;
      chrome.runtime.onMessage.addListener(function (msg, sender, sendResponse) {
        if (!msg || Number(msg.magic) !== SECRPC) return false;
        if (!(msg.host === "*" || host.name === "*" || msg.host === host.name)) return false;
        if (sender && sender.id && sender.id !== chrome.runtime.id) return false;
        const fn = handlers[msg.method];
        if (fn) {
          try {
            const value = fn.apply(null, msg.args || []);
            if (value && typeof value.then === "function") {
              value.then(function (result) {
                try { sendResponse({ result: result }); } catch (e) {}
              }, function (err) {
                try { sendResponse({ error: String(err && err.message ? err.message : err) }); } catch (e) {}
              });
              return true;
            }
            sendResponse({ result: value });
            return false;
          } catch (err) {
            sendResponse({ error: String(err && err.message ? err.message : err) });
            return false;
          }
        }
        if (msg.host !== "*" && host.name !== "*" && msg.host === host.name) {
          sendResponse({ error: "No handler for '" + host.name + ":" + String(msg.method) + "'." });
          return false;
        }
        return false;
      });
    }
  };
  host.open();
  globalThis.__host = host;
  return "host-open offscreen";
})()
