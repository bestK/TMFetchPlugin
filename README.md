# TMFetchPlugin

A generic HTTP fetcher plugin for [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor).
It periodically issues HTTP(S) requests, extracts a target field from the JSON
response using a **JSONPath**-style expression, and displays the value with a
**custom label** in the TrafficMonitor main window or taskbar item area.

Multiple display items are supported via INI configuration, each with its own
URL / headers / body / JSONPath / label / refresh interval.

## Features

-   HTTP / HTTPS via WinHTTP (no external dependencies required at runtime)
-   Per-item method (`GET`, `POST`, ...), headers, request body, timeout
-   JSONPath subset for extracting values:
    -   `$` root, `.field`, `['field']`, `["field"]`
    -   `[N]` array index (negative supported, e.g. `[-1]` = last)
    -   `..field` recursive descent (first match)
-   Custom display label, optional **prefix** and **suffix** (units, currency, ...)
-   Custom sample text (controls width on the taskbar)
-   Per-item refresh interval, runs on a background worker thread (does **not**
    block TrafficMonitor's UI thread)
-   Plain-text INI config; "Options" button opens it in the system editor

## ⚠️ Architecture matters

TrafficMonitor 官方发行版是 **32 位 (x86)**。
DLL 必须与宿主程序位数一致，否则加载时会报 `不是有效的 Win32 程序`。

快速检查 `TrafficMonitor.exe` 位数（PowerShell）：

```pwsh
$b = [IO.File]::ReadAllBytes("path\to\TrafficMonitor.exe")
$o = [BitConverter]::ToInt32($b, 60)
switch ([BitConverter]::ToUInt16($b, $o + 4)) {
    0x14c { "x86 (Win32)" }; 0x8664 { "x64" }
}
```

CMake 配置时如果生成的不是 32 位，会输出警告。

## Building

Requires Windows + a C++17 compiler (MSVC / Visual Studio recommended).
TrafficMonitor official builds ship as **x86 (Win32)**, so build the plugin in
`Win32` to match unless you run an x64 build of TrafficMonitor.

```pwsh
# x86 (matches official TrafficMonitor.exe)
cmake -A Win32 -B build
cmake --build build --config Release

# or x64
cmake -A x64 -B build64
cmake --build build64 --config Release
```

The CMake script auto-downloads the single-header
[`nlohmann/json`](https://github.com/nlohmann/json) (v3.11.3) on first configure.

The output DLL is `build/Release/TMFetchPlugin.dll`.

## Installation

1. Locate TrafficMonitor's plugin folder (typically `<TrafficMonitor>/plugins/`).
2. Copy `TMFetchPlugin.dll` into that folder.
3. Restart TrafficMonitor.
4. The plugin auto-creates a sample `TMFetchPlugin.ini` in the plugin config
   directory provided by TrafficMonitor (visible via _Plugin Management →
   Options_). Edit it and click _Options_ again to reload.

## Configuration

Config file: `TMFetchPlugin.ini` in TrafficMonitor's plugin config directory.

```ini
[Plugin]
Interval=10000        ; default refresh interval (ms)
ItemCount=2           ; number of [ItemN] sections

[Item0]
Name=BTC Price        ; shown in TrafficMonitor menus
Id=tmfetch_btc        ; unique id (letters+digits)
Label=BTC:            ; custom label drawn next to the value
URL=https://api.coingecko.com/api/v3/simple/price?ids=bitcoin&vs_currencies=usd
Method=GET
Headers=Accept: application/json
; or use Header1=, Header2=, ... up to Header16
JsonPath=$.bitcoin.usd
Prefix=$
Suffix=
Sample=$99999.99      ; sample text used to compute display width
Placeholder=--        ; shown while loading or on error
Interval=30000        ; per-item override (ms); 0 = use [Plugin] Interval
Timeout=8000          ; request timeout (ms)
Body=                 ; optional request body (UTF-8) for POST/PUT
```

A complete sample is in [`examples/TMFetchPlugin.ini`](examples/TMFetchPlugin.ini).

### JSONPath examples

| Path                  | Meaning                                        |
| --------------------- | ---------------------------------------------- |
| `$.bitcoin.usd`       | object key chain                               |
| `$['bitcoin']['usd']` | bracket form (use for keys with dots / spaces) |
| `$.data[0].price`     | first array element, then key                  |
| `$.data[-1].price`    | last array element                             |
| `$..stargazers_count` | recursive search; first match wins             |

If `JsonPath` is empty, the trimmed raw response body is used as the value.

## Notes / Caveats

-   The plugin uses Windows' system proxy (WinHTTP automatic proxy detection).
-   The "Options" button just opens `TMFetchPlugin.ini` in the user's default
    text editor; the plugin reloads its config immediately and again next time
    Options is invoked. Save and click Options once more to apply changes.
-   All network I/O happens on a dedicated worker thread; TrafficMonitor's UI
    is never blocked.
