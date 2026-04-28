#pragma once
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace jpath {

// Supported JSONPath subset:
//   $                root
//   .name            child by key
//   ['name']         child by key (single or double quotes supported)
//   ["name"]
//   [N]              array index (N may be negative for from-end)
//   ..name           recursive descent (returns first match in DFS order)
//
// Returns a pointer into the JSON tree, or nullptr if not found.
// If multiple matches exist (recursive descent), the first one in DFS order is returned.
const nlohmann::json* Evaluate(const nlohmann::json& root, const std::string& path);

// Convert a JSON value to a display string.
//  - strings: as-is
//  - numbers: shortest round-trip
//  - bool: "true"/"false"
//  - null: ""
//  - object/array: compact JSON dump
std::string ValueToString(const nlohmann::json& v);

// Render a template string, substituting every "${jsonpath}" occurrence with
// the value extracted from `root` via Evaluate(). Unknown / not-found paths
// are replaced with `missing`.
//
// Examples:
//   "电量:${$.x.a} 状态:${$.y.b}" -> "电量:80 状态:OK"
//
// `tpl` and the return value are UTF-8.
std::string RenderTemplate(const nlohmann::json& root,
                           const std::string& tpl,
                           const std::string& missing = "--");

// Returns true if `s` contains a "${...}" placeholder.
bool IsTemplate(const std::string& s);

} // namespace jpath
