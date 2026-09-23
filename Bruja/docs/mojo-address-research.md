# Extension runtime replies for dagger, voodoo, and mojo addresses

This note records how three address spaces for the same Chromium method were sent on the extension messaging channel, and what came back.

The table is `out/address-db.md`. It has 9062 methods. Each method was sent three times, once for each address. All 27186 replies are `DeliverMessage`.

## Three addresses, one method

`extensions.mojom.MessagePort.DispatchDisconnect` is one method and three numbers.

| space | how the number is made | value |
| --- | --- | --- |
| dagger | FNV-1 of the method name `DispatchDisconnect`, then `& 0x7fffffff` | 941764473 |
| voodoo | FNV-1a of the fully qualified ABI name, then `& 0x7fffffff` | 1481416133 |
| mojo | Chrome `ScrambleMethodOrdinals`: SHA-256 of `chrome/VERSION`, the short interface name, and the 1-based method index, first 4 bytes little-endian, `& 0x7fffffff` | 2592569 |

Dagger matches the ExtWrench dagger pack. `writeBuffer` hashes to `1208116324`. Voodoo matches go++ `hash/fnv` `New32a` applied to `voodoom::MethodKey`. Mojo matches the official desktop scramble. The salt used here is `chrome/VERSION` `156.0.8070.0`. A different Chrome version changes the mojo column.

None of the 9062 voodoo values equals its mojo value. Chrome does not hash the fully qualified name. The voodoo number is the name. The mojo number is the scrambled slot Chrome puts in `header.name` on an official build.

## Which bus a token is

WASMExtWrench `wew::bus::Classify` splits the token before anyone fires it.

| token | kind | owner |
| --- | --- | --- |
| wider than 32 bits, including SECRPC `91556947316803` | `kSRPC` | `chrome.runtime` |
| `1 .. 0x7fffffff` | `kMojo` | HolePunch `header.name` |

SECRPC is `PackASCII6("SECRPC")`. It does not fit a mojo message name. The three addresses above are all mojo-width. They ride inside the SRPC envelope as `args`. They are not the bus.

WASMMsgProxy states the same split. The envelope is `{ magic, host, method, args }`. `chrome.runtime.sendMessage(extensionId, envelope)` carries it. A mojo ordinal is a different wire format.

## The channel

Endpoint Verification (`callobklhcbilhphinckomhgkigmfocg`) is the extension that already speaks this envelope.

`background_service_worker.js` opens the second context:

`chrome.offscreen.createDocument({ url: "/offscreen.html", reasons: [DOM_SCRAPING], justification: "..." })`

`offscreen.html` is a document whose body says "Offscreen Document". It loads `offscreen_script.js`. That script opens `MessageServiceHost`, which registers `chrome.runtime.onMessage`. `onRequest` accepts the message when `magic` is `91556947316803` and `host` is `"*"` or that host's name.

The published mojom for the same channel is `extensions/common/mojom/message_port.mojom` and `OpenChannelToExtension` on `frame.mojom` and `service_worker_host.mojom`.

1. The opener calls `runtime.sendMessage`. The renderer calls `OpenChannelToExtension` with `ChannelType.kSendMessage`, a `MessagePort` (replies come back here), and a `MessagePortHost` (the opener posts here).
2. The browser looks for contexts that registered `runtime.onMessage`.
3. If the listener returns true, that is `MessagePortHost.ResponsePending`. The channel stays open.
4. `sendResponse` is `MessagePortHost.PostMessage`.
5. The opener's callback is `MessagePort.DeliverMessage`.
6. If no other context is listening, the browser calls `MessagePort.DispatchDisconnect`. The callback's `chrome.runtime.lastError` is `Could not establish connection. Receiving end does not exist.`

Chrome does not deliver `runtime.sendMessage` to the frame that sent it. The offscreen document exists so the host and the opener are different frames. A host that calls `sendMessage` itself loops. Voodoo's offscreen host posts to the service instead.

## What was measured

Both runs used headless Chrome, the DevTools port, and the component extension background `nkeimhogjdpnpccoofpliimaahmaaome` (Google Hangouts). Google Chrome ignores `--load-extension`, so the listener was installed on a component background that was already running. The envelope was:

```
{
  magic: 91556947316803,
  host: "*",
  method: "ordinal",
  args: [<address>],
  extension_id: <chrome.runtime.id>,
  channel: "offscreen"
}
```

### Same frame, no other end

The first table sent each mojo address from the same frame that would have had to answer it. There was no `onMessage` listener in another frame. All 9062 methods returned:

`Could not establish connection. Receiving end does not exist. reply=undefined`

The send left. `DispatchDisconnect` is the reply. Nothing in that extension called `ResponsePending`.

A guest that opened the DevTools port itself saw the same shape of failure one step earlier. `GET /json/list` and the websocket upgrade were written onto the guest's output, and the read on the socket timed out. The bytes moved. They did not come back on the caller's pipe. The list and the evaluate were done from the host after that.

### Listener in one frame, send from the other

The second table installs `onMessage` on the background. The listener returns true, then `sendResponse({ result: "ok", method, args, state: "DeliverMessage" })`. The send runs in an iframe of that same extension, which is the other frame.

23351 distinct addresses were sent (the union of the dagger, voodoo, and mojo columns). Every reply was `lastError=none` and `state=DeliverMessage`, with `args` equal to the address that was sent.

`DispatchDisconnect` on that run:

| space | address | reply |
| --- | --- | --- |
| dagger | 941764473 | `DeliverMessage`, args `[941764473]` |
| voodoo | 1481416133 | `DeliverMessage`, args `[1481416133]` |
| mojo | 2592569 | `DeliverMessage`, args `[2592569]` |

## What the replies are

The callback is the extension channel answering. It is not Chrome dispatching `header.name` 2592569 to `MessagePort.DispatchDisconnect`. The listener we installed echoes the envelope. Endpoint Verification's `MessageServiceHost` was not the process that produced these 27186 rows. Hangouts does not open `/offscreen.html`.

The first run shows the channel with no receiver: the browser closes it. The second run shows the channel with a receiver in another frame: `ResponsePending`, then `DeliverMessage`, and the address comes back in `args`. That is the difference between a fire and a reply.

The `result: "ok"` in those rows is the echo listener. Nothing runs after `sendResponse`. A host that owns methods does the work first, then the reply is the return value.

## Host dispatch

`ng/ext/host.js` is that host, installed by CDP on the Hangouts background `nkeimhogjdpnpccoofpliimaahmaaome`. The other frame sends with `host: "offscreen"`. The listener looks up the method, runs it, and `sendResponse` carries the return value. A promise returns true (`ResponsePending`) and replies when it settles. A named host with no method replies `No handler for 'offscreen:<method>'.`

`ng/state_machine.py` sent the three `DispatchDisconnect` addresses through `ordinal`, then asked for the log. Capture is `out/state-machine.txt`.

| send | reply |
| --- | --- |
| `hello` | `{result:{ready:true,host:"offscreen"}}` |
| `ordinal` 941764473 | `{result:{recorded:true,address:941764473,logSize:1}}` |
| `ordinal` 1481416133 | `{result:{recorded:true,address:1481416133,logSize:2}}` |
| `ordinal` 2592569 | `{result:{recorded:true,address:2592569,logSize:3}}` |
| `pending` 2592569 | `{result:{state:"ResponsePending",address:2592569}}` |
| `nope` | `{error:"No handler for 'offscreen:nope'."}` |
| `getLog` | three entries, dagger then voodoo then mojo, each `state:"recorded"` |

`getLog` is the later message. The three addresses are still in the log, so the handler's write survived the reply.

## The database

`C:\Users\grego\Bruja\out\address-db.db` is that host run across the portfolio. 9062 methods, 27186 sends (dagger, then voodoo, then mojo on each method). All 27186 replies are `recorded: true`. `log_size` runs from 1 through 27186 with no gaps. A closing `stat` returned `logSize: 27186`, and the last entry was still `state: "recorded"`.

23351 distinct addresses. The same short-name dagger is shared by more than one method, so a repeated address is written again and gets a new log size.

`DispatchDisconnect`:

| space | address | log size |
| --- | --- | --- |
| dagger | 941764473 | 15733 |
| voodoo | 1481416133 | 15734 |
| mojo | 2592569 | 15735 |

`out/address-db.md` is the same rows, with the log size in place of the echo body. The reply JSON lives in the `replies` table.

## Files

| path | what it is |
| --- | --- |
| `out/address-db.db` | sqlite: 9062 methods, 27186 host replies, log size 1..27186 |
| `out/address-db.md` | the same rows, log size per column |
| `ng/build_address_db.py` | installs the host and fills the database |
| `out/mojovm-portfolio.txt` | voodoo and mojo columns, 9062 methods, 1860 files |
| `ng/address-db.ps1` | the two-frame sweep |
| `ng/ext/host.js` | MessageServiceHost: handler runs, then the reply |
| `ng/state_machine.py` | installs that host and reads the log back |
| `out/state-machine.txt` | hello, three recordings, pending, missing method, getLog |
| `ng/address_rows.py` | dagger column from the portfolio |
| `WASMExtWrench` `wew::bus::Classify` | SRPC versus mojo |
| `WASMMsgProxy/js/offscreen.js` | the offscreen host shape |
| Endpoint Verification `offscreen_script.js` | `MessageServiceHost` for magic `91556947316803` |
