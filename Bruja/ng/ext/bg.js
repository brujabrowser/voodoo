// Receiver for runtime.sendMessage. Returning true is ResponsePending.
// sendResponse is PostMessage, which the opener sees as DeliverMessage.
const SECRPC = 91556947316803;

chrome.runtime.onMessage.addListener((msg, sender, sendResponse) => {
  if (!msg || Number(msg.magic) !== SECRPC) return false;
  Promise.resolve().then(() => {
    sendResponse({
      result: "ok",
      method: msg.method,
      args: msg.args,
      state: "DeliverMessage"
    });
  });
  return true;
});
