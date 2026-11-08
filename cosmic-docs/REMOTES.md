# Wi-Fi remotes — v0.3 development build

LG TV Remote and Roku Remote are built-in apps under Apps; no SD-card app installation is required. They use the launcher's existing Wi-Fi settings and preserve the COSMIC theme. The Cardputer and the device must be on the same local network, with local-device traffic permitted by the router.

On first opening either remote, choose Discover devices or enter the device's local IPv4 address in Settings. Discovery collects multiple responses and lets you select an IP. If discovery is blocked, enter the address from your TV's network settings. Addresses and LG pairing data are saved in the `cosmic_remote` NVS namespace and survive firmware updates that preserve NVS. Changing the LG address clears its previous pairing key and wake MAC.

## LG webOS TV

Requires a webOS TV with network/mobile remote control enabled. Connect while the TV is awake. The remote starts the webOS hello → system-info → registration handshake. Accept the Allow prompt on the TV using your existing remote. The returned client key is saved for future sessions. If prompt pairing does not work, press S, change Pairing to PIN, open the remote, then press N to enter the code displayed by the TV. Forget pairing in Settings to force a fresh registration.

The app uses the IP9LGRemote webOS client, including TLS port 3001 with legacy port 3000 fallback, pointer-input socket button delivery, and limited remote-control permissions. LG TVs normally use self-signed certificates; this client does not validate them against a public CA. TLS connections run in their own task so reconnects do not redraw or block the screen.

Press W to send Wake-on-LAN if a TV MAC is saved or learned after pairing. Enable the TV's network standby / Mobile TV On / Turn on via Wi-Fi option as appropriate for your model. A powered-off TV may not listen to IP commands; wake support depends on its standby configuration. O sends power-off, not a toggle. TVs without webOS are not supported by this Wi-Fi app. Infrared, Bluetooth, service menus, and a motion pointer are not implemented here.

## Roku

Uses the Roku External Control Protocol (ECP), HTTP port 8060, with SSDP discovery. Enable Settings → System → Advanced system settings → Control by mobile apps → Enabled (wording varies by model/OS). Normal remote keypresses do not need Developer Mode. A 403 response is shown as a denied-control error; unreachable devices and other HTTP failures are reported separately.

Commands are queued and sent in a worker task, using short network timeouts. A successful status means the device accepted the HTTP request, not proof that the current app acted on it. Exit clears unsent queued commands; a request already in progress may finish. Volume, mute and power controls depend on the Roku model and its TV/CEC configuration. They are not available on every standalone streaming player.

## Controls

| Cardputer control | TV/Roku action |
|---|---|
| Arrow/navigation keys | Directional navigation |
| ENTER | OK / Select |
| H | Home |
| B or Backspace | Back on the TV |
| + or = / - | Volume up / down |
| M | Mute |
| P | Play/pause |
| R / F | Rewind / fast forward |
| I | Info |
| 0–9 | Digits (Roku uses literal input) |
| T | Type a short string into the focused TV text field |
| S | Device and Wi-Fi settings |
| N | Submit the LG pairing PIN |
| W | LG Wake-on-LAN |
| O | Power off |
| Backtick/ESC | Return to the desktop |

The LG connection is suspended when leaving its app. Music and Lua apps may need closing first to make room for LG's TLS connection. The USB controller bridge can remain active if memory permits. Remote screens repaint only when their status changes or you return from a dialog; there is no flashing activity animation.

## Validation and provenance

The ESP32 build and portable tests check compilation, remote address validation, Roku text escaping, LG command mappings, and the existing controller/Lua tests. No LG TV or Roku pairing, discovery, wake, or command delivery has been device-tested in this session. These apps must be tested against your particular TV/Roku firmware.

LG client source adapted from [raptyk/IP9LGRemote](https://github.com/raptyk/IP9LGRemote), commit `9ef7d134242bb08eed3da44bbc890cf2f863c564`, MIT © 2026 Patryk Strzelecki. The retained source notices and full license are in `lib/CosmicLG/LICENSE`. Adaptations: a launcher UI/NVS integration, reduced registration permissions and explicit startup-allocation checks.

Roku protocol reference: [mgierhart/Roku-ESP32-Remote](https://github.com/mgierhart/Roku-ESP32-Remote), commit `a82e15782dd74118a7769b413d36a38c76152590`; this integration implements its own UI, discovery and queued ECP transport, rather than copying its sketch. Protocol documentation: [Roku ECP](https://developer.roku.com/dev/docs/external-control-api).

WebSocket dependency: Links2004/arduinoWebSockets 2.6.1, LGPL-2.1; complete source and license included under `lib/WebSockets` in the source bundle. The source ZIP includes all linked library sources and existing upstream licenses.
