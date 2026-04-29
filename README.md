# TMFetchPlugin

[![Release](https://img.shields.io/github/v/release/bestK/TMFetchPlugin)](https://github.com/bestK/TMFetchPlugin/releases/latest)
[![Build](https://github.com/bestK/TMFetchPlugin/actions/workflows/release.yml/badge.svg)](https://github.com/bestK/TMFetchPlugin/actions/workflows/release.yml)
[![License](https://img.shields.io/badge/license-MIT-blue.svg)](#license)

A generic HTTP / JSON fetcher plugin for [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor).
Point it at any REST API, write a one-line **display template** mixing literal
text with `$.path.x` references, and the resolved value is rendered onto your
TrafficMonitor taskbar / main window on a configurable schedule.

```
电量:$.battery.level%  状态:$.status     ->     电量:76%  状态:OK
```

Multiple items are supported, each with its own URL / headers / body / template /
interval / timeout.

## Features

-   **Free-form display template.** Just write text and drop `$.path.x` references
    inline. The scanner finds them greedily; everything else is literal. `$$`
    emits a literal `$`. `${...}` braces form is also accepted.
-   **Two-line layout via `\n`.** A `\n` in the template (or a real newline) splits
    the rendered output into the TrafficMonitor _label_ (top row) and _value_
    (bottom row), matching the host's built-in two-row display.
-   **JSONPath subset:** `.key`, `['key']`, `[N]` (negatives ok, e.g. `[-1]`), and
    `..key` recursive descent.
-   **Per-item refresh** on a background worker thread. The TrafficMonitor UI
    thread is never blocked, even on slow / failing endpoints.
-   **In-app options dialog** with a live **Test** button: shows status, raw
    response, and the rendered template before you save.
-   **HTTP / HTTPS via WinHTTP** — no runtime dependencies, no MSVC redist.
-   **Per-item method, headers, body, timeout.**
-   **Hot-reload:** edits to existing items take effect immediately.

## Install

Grab the matching DLL from the [latest release](https://github.com/bestK/TMFetchPlugin/releases/latest):

| TrafficMonitor.exe                | Use this DLL            |
| --------------------------------- | ----------------------- |
| **32-bit / x86** (official build) | `TMFetchPlugin-x86.dll` |
| **64-bit / x64**                  | `TMFetchPlugin-x64.dll` |

Drop the file into TrafficMonitor's `plugins/` directory and restart
TrafficMonitor. Then enable the items you want via
_选项 → 插件管理 → TMFetchPlugin → 配置_.

> Bitness mismatch fails to load with `不是有效的 Win32 程序`. Quick check of
> your TrafficMonitor.exe:
>
> ```pwsh
> $b = [IO.File]::ReadAllBytes("path\to\TrafficMonitor.exe")
> $o = [BitConverter]::ToInt32($b, 60)
> switch ([BitConverter]::ToUInt16($b, $o + 4)) {
>     0x14c { "x86 (Win32)" }; 0x8664 { "x64" }
> }
> ```

## Build from source

Windows + C++17 compiler (MSVC recommended; MinGW also works).

```pwsh
# x86 (matches official TrafficMonitor.exe)
cmake -A Win32 -B build
cmake --build build --config Release

# x64
cmake -A x64 -B build64
cmake --build build64 --config Release
```

First configure auto-downloads the single-header
[`nlohmann/json`](https://github.com/nlohmann/json) v3.11.3.
Output DLL: `build/Release/TMFetchPlugin.dll`.

## Configure

The in-app options dialog is the recommended path: every field is editable, and
the **Test** button shows you the rendered output before you save.

Under the hood, settings live in `TMFetchPlugin.ini` inside TrafficMonitor's
plugin config directory. Hand-editing is also fine:

```ini
[Plugin]
Interval=10000        ; default refresh interval (ms)
ItemCount=2           ; number of [ItemN] sections

[Item0]
Name=BTC Price        ; shown in TrafficMonitor menus
Id=tmfetch_btc        ; unique id (letters / digits / underscore)
URL=https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=usd
Method=GET
Headers=Accept: application/json
; or split: Header1=Accept: application/json   Header2=User-Agent: TM
JsonPath=BTC: $$$.bitcoin.usd     ; literal "BTC: $" + $.bitcoin.usd
Interval=30000                    ; per-item override (ms); 0 = use [Plugin] Interval
Timeout=8000                      ; request timeout (ms)
Body=                             ; optional request body (UTF-8) for POST/PUT
```

Full example with multiple items: [`examples/TMFetchPlugin.ini`](examples/TMFetchPlugin.ini).

### Display template syntax

The `JsonPath` field is the **only** value-formatting field. Write whatever
text you want and inline `$.path.x` references; they're substituted with the
value from the JSON response.

| Template                                  | Rendered                              |
| ----------------------------------------- | ------------------------------------- |
| `BTC: $$$.bitcoin.usd`                    | `BTC: $65432.5`                       |
| `电量:$.battery.level%  状态:$.status`    | `电量:76%  状态:OK`                   |
| `电量:$.soc%\nF:$.front.psi R:$.rear.psi` | label `电量:76%`, value `F:230 R:240` |
| `★ $.stargazers_count`                    | `★ 4321`                              |
| `${$.bitcoin.usd}` _(legacy braces)_      | `65432.5`                             |
| _(empty)_                                 | raw response body                     |

Rules:

-   `$.path.foo` is matched greedily; it stops at the first character that can’t
    be part of a path (whitespace, punctuation other than `.`/`[`).
-   Inside brackets you can quote: `$.users[0]['display.name']`.
-   `..key` does a recursive descent and returns the first match.
-   `$$` emits a literal `$`. A bare `$` followed by something that isn't `.`,
    `[`, or `{` is treated as a literal too (so `$5 dollars` works fine).
-   A path that doesn't resolve renders as `--`.
-   `\n` (or a real Enter in the options dialog) is treated as the line break
    between the _label_ (top row, drawn by TrafficMonitor above the value) and
    the _value_ (bottom row). Only the **first** newline is special; further
    newlines just stay inside the value.

## Notes

-   WinHTTP uses Windows' system proxy automatically.
-   All network I/O happens on a dedicated worker thread — TrafficMonitor's UI
    thread is never blocked.
-   Items survive config reloads in-place: removing an item from the dialog
    hides it but doesn't tear down the live `IPluginItem*` (TrafficMonitor caches
    those pointers, so destroying them at runtime would crash the host).
    Reordering / removing items takes effect cleanly after a restart.

## License

MIT.
