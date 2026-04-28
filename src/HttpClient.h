#pragma once
#include <string>
#include <vector>

namespace http {

struct Request {
    std::wstring url;
    std::wstring method = L"GET";          // GET / POST / ...
    std::vector<std::wstring> headers;     // each entry like L"Key: Value"
    std::string body;                      // request body (UTF-8)
    int timeout_ms = 8000;
};

struct Response {
    bool ok = false;
    int status = 0;
    std::string body;     // raw bytes (UTF-8 assumed)
    std::wstring error;
};

Response Fetch(const Request& req);

} // namespace http
