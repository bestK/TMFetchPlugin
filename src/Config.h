#pragma once
#include <string>
#include <vector>

struct ItemConfig {
    std::wstring section;       // e.g. "Item0"
    std::wstring name;          // display name in TM menus
    std::wstring id;            // unique ID (letters/digits)
    std::wstring url;           // request URL
    std::wstring method = L"GET";
    std::vector<std::wstring> headers; // each entry: "Key: Value"
    std::string  body;          // UTF-8 request body
    // Free-form template. Bare "$.path.x" tokens are auto-detected and
    // evaluated against the JSON response; everything else is literal.
    // Use "$$" to emit a literal '$'. Newlines (entered as Enter in the UI,
    // or "\n" in INI) are preserved.
    std::wstring jsonpath;
    int          interval_ms = 0;   // 0 = use global
    int          timeout_ms  = 8000;
};

struct PluginConfig {
    int interval_ms = 5000;     // default refresh interval
    std::vector<ItemConfig> items;
};

namespace config {

// Returns full path of the plugin's INI file inside the given config dir.
std::wstring IniPath(const std::wstring& configDir);

// Load config from INI. If the file does not exist, writes a default sample
// and returns it. Always returns a usable config (possibly empty items list).
PluginConfig Load(const std::wstring& iniPath);

// Write a sample/default config file.
void WriteSample(const std::wstring& iniPath);

// Save the in-memory config back to INI. Existing [ItemN] sections are cleared
// before being rewritten. Multiline fields (Body) are escaped (\n / \r / \\).
void Save(const std::wstring& iniPath, const PluginConfig& cfg);

} // namespace config
