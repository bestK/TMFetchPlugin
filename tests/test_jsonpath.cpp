// Quick standalone test for the JSONPath subset.
// Build: g++ -std=c++17 -I include -I build/_deps/json src/JsonPath.cpp tests/test_jsonpath.cpp -o tests/test_jsonpath.exe
#include "../src/JsonPath.h"
#include <cassert>
#include <iostream>
#include <nlohmann/json.hpp>

using nlohmann::json;

static int s_failed = 0;

#define CHECK(cond, msg) do { \
    if (!(cond)) { std::cerr << "FAIL: " << msg << " at line " << __LINE__ << "\n"; ++s_failed; } \
} while(0)

int main() {
    json doc = json::parse(R"({
        "bitcoin": {"usd": 65432.5, "eur": 60000},
        "list": [ {"price": 10}, {"price": 20}, {"price": 30} ],
        "ip": "1.2.3.4",
        "deeply": {"nested": {"stargazers_count": 42}},
        "key.with.dot": "ok"
    })");

    auto eq = [&](const std::string& path, const std::string& expect) {
        const json* v = jpath::Evaluate(doc, path);
        CHECK(v, "path not found: " + path);
        if (v) {
            std::string got = jpath::ValueToString(*v);
            CHECK(got == expect, path + " => '" + got + "' expected '" + expect + "'");
        }
    };

    eq("$.bitcoin.usd", "65432.5");
    eq("$['bitcoin']['eur']", "60000");
    eq("$.list[0].price", "10");
    eq("$.list[-1].price", "30");
    eq("$.ip", "1.2.3.4");
    eq("$..stargazers_count", "42");
    eq("$['key.with.dot']", "ok");
    eq(".bitcoin.usd", "65432.5"); // missing $ should still work

    // Not-found returns null
    CHECK(jpath::Evaluate(doc, "$.nope") == nullptr, "missing key returns null");
    CHECK(jpath::Evaluate(doc, "$.list[99].price") == nullptr, "out-of-range returns null");

    // Template tests - braces form
    {
        std::string out = jpath::RenderTemplate(doc, "电量:${$.bitcoin.usd} 状态:${$.ip}");
        CHECK(out == "电量:65432.5 状态:1.2.3.4", "tpl braces 1: " + out);
    }
    {
        std::string out = jpath::RenderTemplate(doc, "${$.list[0].price}-${$.list[-1].price}");
        CHECK(out == "10-30", "tpl braces ranges: " + out);
    }
    {
        std::string out = jpath::RenderTemplate(doc, "[${$.missing}]", "N/A");
        CHECK(out == "[N/A]", "tpl braces missing: " + out);
    }
    {
        std::string out = jpath::RenderTemplate(doc, "$$${$.bitcoin.usd}");
        CHECK(out == "$65432.5", "tpl braces escape: " + out);
    }

    // Template tests - bare $.path form (the new primary syntax)
    {
        std::string out = jpath::RenderTemplate(doc, "aaa:$.bitcoin.usd\nbbb:$.ip");
        CHECK(out == "aaa:65432.5\nbbb:1.2.3.4", "bare 1: " + out);
    }
    {
        std::string out = jpath::RenderTemplate(doc, "电量:$.bitcoin.usd% 状态:$.ip!");
        // $.bitcoin.usd stops at '%'; $.ip stops at '!'
        CHECK(out == "电量:65432.5% 状态:1.2.3.4!", "bare with delim: " + out);
    }
    {
        std::string out = jpath::RenderTemplate(doc, "$5 dollars");
        CHECK(out == "$5 dollars", "bare $ literal: " + out);  // not a path
    }
    {
        std::string out = jpath::RenderTemplate(doc, "$$ -> literal $$");
        CHECK(out == "$ -> literal $", "bare escape: " + out);
    }
    {
        std::string out = jpath::RenderTemplate(doc, "first:$.list[0].price last:$.list[-1].price");
        CHECK(out == "first:10 last:30", "bare with index: " + out);
    }
    {
        std::string out = jpath::RenderTemplate(doc, "x=$.missing.field y", "??");
        CHECK(out == "x=?? y", "bare missing: " + out);
    }

    CHECK(jpath::IsTemplate("a${$.x}b") == true, "IsTemplate braces");
    CHECK(jpath::IsTemplate("a $.x.y b") == true, "IsTemplate bare");
    CHECK(jpath::IsTemplate("plain text") == false, "IsTemplate none");
    CHECK(jpath::IsTemplate("$5 dollars") == false, "IsTemplate $literal");

    if (s_failed == 0) std::cout << "All JSONPath tests passed.\n";
    return s_failed == 0 ? 0 : 1;
}
