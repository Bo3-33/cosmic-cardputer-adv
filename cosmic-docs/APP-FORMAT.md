# COSMIC App Package v1

Hosted apps are Lua source packages on the SD card. They run inside this launcher without rebooting. The app switcher retains up to three independent Lua states, subject to free memory. Only the foreground Lua app receives updates and input; other Lua apps pause. The USB controller bridge is a separate native service and keeps forwarding while you use Lua apps, menus, or the desktop.

An ESP32 firmware `.bin` remains a whole-device program. Installing or booting it uses the existing Firmware menu and leaves this launcher. Firmware binaries do not become hosted apps by renaming them.

## Directory and manifest

Copy an app directory to `/apps/<id>/` on the SD card:

```text
/apps/hello/app.json
/apps/hello/main.lua
```

```json
{
  "format": "cosmic.app",
  "version": 1,
  "api": 1,
  "id": "hello",
  "name": "Hello COSMIC",
  "entry": "main.lua"
}
```

The directory must match `id`: 1–32 lowercase ASCII letters, digits, or hyphens. The display name must be 1–24 bytes (use ASCII for the device font). `version` is the package format version, and `api` is the required launcher API version; both are integer `1`. `entry` is always `main.lua`. Optional `author` and `description` fields are informational. Other fields are ignored by v1.

`app.json` is limited to 2 KB and `main.lua` to 24 KB. Source must be UTF-8 text; binary Lua bytecode is rejected. The launcher scans up to 32 valid app packages each time Apps opens. Invalid packages are counted in the menu and skipped.

The distribution convention is `<id>.capp.zip`, containing the app directory. Extract it into `/apps/` using your computer or another file manager; v1 does not extract ZIP files itself. The included SDK validator checks the package envelope, not Lua syntax or app behavior:

```sh
python3 app-sdk/validate.py sdcard/apps/stopwatch --zip stopwatch.capp.zip
```

Copy `app-sdk/template` to a directory named `hello` before using its manifest, or change both its ID and directory name together.

## Lifecycle

`main.lua` returns a table of optional callback functions:

```lua
local count = 0
return {
  init = function() count = tonumber(storage.load()) or 0 end,
  key = function(key) if key == "enter" then count = count + 1 end end,
  update = function(dt) end,
  draw = function()
    ui.text(12, 16, "Hello COSMIC", 2)
    ui.text(12, 48, "Count: " .. tostring(count))
  end,
  suspend = function() storage.save(tostring(count)) end,
  resume = function() end,
}
```

`init()` runs once when loading. `resume()` runs when brought to the foreground. `key(name)` receives one key event at a time. `update(dt)` runs approximately 30 times per second with elapsed seconds capped at `0.1`; it stops while the app is paused. `draw()` runs on input, explicit `app.redraw()`, and when an app becomes foreground. Animated apps must call `app.redraw()` from `update(dt)` when their content changes. There is no timer-driven repaint for static apps. Keep drawing calls in `draw()`. The launcher clears the app content region before each frame. `suspend()` runs when switching away or closing; an already paused app can receive another suspend on close, so make it repeatable.

TAB opens the app switcher from an app or the desktop. It offers resume, close, close all Lua apps, desktop, and the bridge monitor when forwarding is active. The switcher shows each app's Lua allocation in KB. The backtick/ESC key returns to the desktop and retains the app state. Closing an app releases its Lua memory. Variables do not survive a reboot; explicitly save data you need later.

## API

Coordinates are relative to a **240 × 100** app content area. The launcher owns the top panel and bottom navigation bar. Coordinates and sizes are clipped to the content area. The device font renders printable ASCII.

| Function / field | Behavior |
|---|---|
| `ui.text(x, y, text, scale=1, fg=theme.text, bg=theme.background)` | One line, scales 1–3, cropped to fit. Control characters become spaces. |
| `ui.rect(x, y, width, height, color=theme.text, filled=true)` | Filled or outlined rectangle. |
| `ui.line(x1, y1, x2, y2, color=theme.text)` | Line between bounded endpoints. |
| `app.width`, `app.height` | `240`, `100`. |
| `app.millis()` | Device uptime in milliseconds; unlike `dt`, it includes time while paused. |
| `app.redraw()` | Requests a content redraw. |
| `storage.load()` | Returns the app's saved string, or `nil` when absent/unavailable. |
| `storage.save(text)` | Writes up to 1024 bytes; returns success. At most one successful save per second per app. |

`theme.background`, `theme.panel`, `theme.accent`, `theme.text`, and `theme.surface` are RGB565 integers derived from your COSMIC theme. Additional RGB565 colors can be passed directly.

Storage is confined to `/appdata/<id>/state.txt`. Save errors, an absent SD card, or the write rate limit return false. Writes are best effort and can be interrupted by power loss. No arbitrary filesystem, raw network, USB, firmware flashing, or system configuration API is exposed to Lua v1.

Key names are `left`, `right`, `up`, `down`, `enter`, `backspace`, or a single printable character. The physical comma, period, semicolon and slash keys serve as navigation under the launcher's existing keyboard mapping. TAB and backtick/ESC are reserved for the shell. Backspace edits text inside Lua apps.

## Runtime limits

The runtime is Lua 5.4.8 with 32-bit integers and floats. Each app has a hard 40 KB Lua allocation limit and approximately 12,000 VM instructions per callback/top-level execution. The loader also reserves free heap before starting another app. Three is the maximum, not a promise that every combination of apps and services fits.

Errors, allocation limits, and instruction overruns close the offending app and show a recoverable error screen. This is a constrained API for trusted personal apps, not a security guarantee for hostile code.

Available standard libraries are base, table, string, math and UTF-8. The following are intentionally absent: `io`, `os`, `package`, `debug`, coroutines, `load`, `loadfile`, `dofile`, `print`, `collectgarbage`, `pcall`, `xpcall`, metatable access, and string pattern/dump/repetition functions. Removing protected calls and metatable changes prevents apps from trapping instruction-limit errors or registering finalizers that could hang app teardown. Modules and assets are not loaded through `require` in v1; use one `main.lua` file.

Sample packages: Stopwatch demonstrates paused state and elapsed foreground time; Pocket Notes demonstrates text and app storage; Orbit demonstrates animation and navigation.
