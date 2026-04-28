#include "JsonPath.h"
#include <cstdlib>
#include <sstream>
#include <variant>

using nlohmann::json;

namespace jpath {

namespace {

struct Token {
    enum Kind { KEY, INDEX, RECURSIVE_KEY };
    Kind kind;
    std::string key;
    long long index{0};
};

bool ParseTokens(const std::string& path, std::vector<Token>& out) {
    size_t i = 0;
    const size_t n = path.size();

    // Optional leading '$'
    if (i < n && path[i] == '$') ++i;

    while (i < n) {
        if (path[i] == '.') {
            ++i;
            bool recursive = false;
            if (i < n && path[i] == '.') { recursive = true; ++i; }
            // read identifier
            size_t s = i;
            while (i < n && path[i] != '.' && path[i] != '[') ++i;
            if (s == i) return false;
            Token t;
            t.kind = recursive ? Token::RECURSIVE_KEY : Token::KEY;
            t.key = path.substr(s, i - s);
            out.push_back(std::move(t));
        } else if (path[i] == '[') {
            ++i;
            if (i >= n) return false;
            // string key in quotes, or numeric index
            if (path[i] == '\'' || path[i] == '"') {
                char q = path[i++];
                size_t s = i;
                while (i < n && path[i] != q) ++i;
                if (i >= n) return false;
                Token t; t.kind = Token::KEY; t.key = path.substr(s, i - s);
                out.push_back(std::move(t));
                ++i; // closing quote
                if (i >= n || path[i] != ']') return false;
                ++i;
            } else {
                size_t s = i;
                while (i < n && path[i] != ']') ++i;
                if (i >= n) return false;
                std::string num = path.substr(s, i - s);
                ++i; // ']'
                try {
                    Token t; t.kind = Token::INDEX;
                    t.index = std::stoll(num);
                    out.push_back(std::move(t));
                } catch (...) { return false; }
            }
        } else {
            // unexpected char; treat the rest as a key (lenient)
            size_t s = i;
            while (i < n && path[i] != '.' && path[i] != '[') ++i;
            Token t; t.kind = Token::KEY; t.key = path.substr(s, i - s);
            out.push_back(std::move(t));
        }
    }
    return true;
}

const json* ApplyKey(const json* cur, const std::string& key) {
    if (!cur) return nullptr;
    if (cur->is_object()) {
        auto it = cur->find(key);
        if (it == cur->end()) return nullptr;
        return &(*it);
    }
    return nullptr;
}

const json* ApplyIndex(const json* cur, long long idx) {
    if (!cur || !cur->is_array()) return nullptr;
    long long sz = (long long)cur->size();
    if (idx < 0) idx += sz;
    if (idx < 0 || idx >= sz) return nullptr;
    return &((*cur)[(size_t)idx]);
}

const json* RecursiveFind(const json* cur, const std::string& key) {
    if (!cur) return nullptr;
    if (cur->is_object()) {
        auto it = cur->find(key);
        if (it != cur->end()) return &(*it);
        for (auto& kv : cur->items()) {
            if (auto* hit = RecursiveFind(&kv.value(), key)) return hit;
        }
    } else if (cur->is_array()) {
        for (auto& v : *cur) {
            if (auto* hit = RecursiveFind(&v, key)) return hit;
        }
    }
    return nullptr;
}

} // namespace

const json* Evaluate(const json& root, const std::string& path) {
    std::vector<Token> tokens;
    if (!ParseTokens(path, tokens)) return nullptr;
    const json* cur = &root;
    for (auto& t : tokens) {
        if (!cur) return nullptr;
        switch (t.kind) {
        case Token::KEY:           cur = ApplyKey(cur, t.key); break;
        case Token::INDEX:         cur = ApplyIndex(cur, t.index); break;
        case Token::RECURSIVE_KEY: cur = RecursiveFind(cur, t.key); break;
        }
    }
    return cur;
}

namespace {

inline bool IsPathIdentByte(unsigned char c) {
    if (c >= '0' && c <= '9') return true;
    if (c >= 'A' && c <= 'Z') return true;
    if (c >= 'a' && c <= 'z') return true;
    if (c == '_' || c == '-') return true;
    if (c >= 0x80) return true;            // any non-ASCII (UTF-8 continuation)
    return false;
}

// s[start] expected to be '['. Returns position past the matching ']' or npos.
size_t ScanBracket(const std::string& s, size_t start) {
    size_t i = start + 1;
    while (i < s.size()) {
        char c = s[i];
        if (c == ']') return i + 1;
        if (c == '\'' || c == '"') {
            char q = c; ++i;
            while (i < s.size() && s[i] != q) ++i;
            if (i < s.size()) ++i;
        } else {
            ++i;
        }
    }
    return std::string::npos;
}

// Greedily scan a JSONPath starting at s[start]=='$'. Returns end position
// (exclusive) of the matched path; returns `start` if no valid path was found
// (caller will then treat the '$' as a literal character).
size_t TryScanPath(const std::string& s, size_t start) {
    if (start >= s.size() || s[start] != '$') return start;
    size_t i = start + 1;
    if (i >= s.size() || (s[i] != '.' && s[i] != '[')) return start;

    bool advanced = false;
    while (i < s.size()) {
        char c = s[i];
        if (c == '.') {
            size_t backup = i;
            ++i;
            if (i < s.size() && s[i] == '.') ++i; // recursive descent
            size_t id_start = i;
            while (i < s.size() && IsPathIdentByte((unsigned char)s[i])) ++i;
            if (i == id_start) { i = backup; break; }
            advanced = true;
        } else if (c == '[') {
            size_t end = ScanBracket(s, i);
            if (end == std::string::npos) break;
            i = end;
            advanced = true;
        } else {
            break;
        }
    }
    return advanced ? i : start;
}

} // namespace

bool IsTemplate(const std::string& s) {
    for (size_t i = 0; i + 1 < s.size(); ++i) {
        if (s[i] == '$') {
            char c = s[i + 1];
            if (c == '{' || c == '.' || c == '[') return true;
        }
    }
    return false;
}

std::string RenderTemplate(const json& root, const std::string& tpl, const std::string& missing) {
    std::string out;
    out.reserve(tpl.size());
    const size_t n = tpl.size();
    size_t i = 0;
    while (i < n) {
        // "$$" -> literal '$'
        if (tpl[i] == '$' && i + 1 < n && tpl[i + 1] == '$') {
            out.push_back('$'); i += 2; continue;
        }
        // "${...}" -> evaluate path inside braces
        if (tpl[i] == '$' && i + 1 < n && tpl[i + 1] == '{') {
            size_t end = tpl.find('}', i + 2);
            if (end == std::string::npos) { out.append(tpl, i, n - i); break; }
            std::string path = tpl.substr(i + 2, end - (i + 2));
            const json* node = Evaluate(root, path);
            out += node ? ValueToString(*node) : missing;
            i = end + 1;
            continue;
        }
        // Bare "$.x.y" or "$['x']" form
        if (tpl[i] == '$') {
            size_t end = TryScanPath(tpl, i);
            if (end > i) {
                std::string path = tpl.substr(i, end - i);
                const json* node = Evaluate(root, path);
                out += node ? ValueToString(*node) : missing;
                i = end;
                continue;
            }
        }
        out.push_back(tpl[i++]);
    }
    return out;
}

std::string ValueToString(const json& v) {
    if (v.is_null()) return "";
    if (v.is_string()) return v.get<std::string>();
    if (v.is_boolean()) return v.get<bool>() ? "true" : "false";
    if (v.is_number_integer())  return std::to_string(v.get<long long>());
    if (v.is_number_unsigned()) return std::to_string(v.get<unsigned long long>());
    if (v.is_number_float()) {
        std::ostringstream oss;
        oss.precision(10);
        oss << v.get<double>();
        return oss.str();
    }
    return v.dump();
}

} // namespace jpath
