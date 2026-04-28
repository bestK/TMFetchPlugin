#include "HttpClient.h"
#include <Windows.h>
#include <winhttp.h>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace http {

namespace {

std::wstring FormatLastError(const wchar_t* prefix) {
    DWORD err = GetLastError();
    wchar_t buf[256];
    swprintf(buf, 256, L"%s (err=%lu)", prefix, err);
    return buf;
}

struct WinHttpHandle {
    HINTERNET h{nullptr};
    WinHttpHandle() = default;
    explicit WinHttpHandle(HINTERNET hh) : h(hh) {}
    ~WinHttpHandle() { if (h) WinHttpCloseHandle(h); }
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;
    operator HINTERNET() const { return h; }
};

} // namespace

Response Fetch(const Request& req) {
    Response resp;

    URL_COMPONENTS uc{};
    uc.dwStructSize = sizeof(uc);
    wchar_t host[256] = {0};
    wchar_t urlpath[2048] = {0};
    uc.lpszHostName = host;       uc.dwHostNameLength = 256;
    uc.lpszUrlPath  = urlpath;    uc.dwUrlPathLength  = 2048;

    if (!WinHttpCrackUrl(req.url.c_str(), (DWORD)req.url.size(), 0, &uc)) {
        resp.error = FormatLastError(L"WinHttpCrackUrl failed");
        return resp;
    }

    WinHttpHandle session(WinHttpOpen(
        L"TMFetchPlugin/1.0",
        WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!session) {
        resp.error = FormatLastError(L"WinHttpOpen failed");
        return resp;
    }

    WinHttpSetTimeouts(session, req.timeout_ms, req.timeout_ms, req.timeout_ms, req.timeout_ms);

    WinHttpHandle conn(WinHttpConnect(session, host, uc.nPort, 0));
    if (!conn) {
        resp.error = FormatLastError(L"WinHttpConnect failed");
        return resp;
    }

    DWORD flags = (uc.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;

    WinHttpHandle hreq(WinHttpOpenRequest(
        conn,
        req.method.c_str(),
        urlpath,
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        flags));
    if (!hreq) {
        resp.error = FormatLastError(L"WinHttpOpenRequest failed");
        return resp;
    }

    // Headers
    std::wstring header_blob;
    for (auto& h : req.headers) {
        if (h.empty()) continue;
        header_blob += h;
        header_blob += L"\r\n";
    }
    LPCWSTR hdr_ptr = header_blob.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : header_blob.c_str();
    DWORD   hdr_len = header_blob.empty() ? 0 : (DWORD)-1;

    BOOL sent = WinHttpSendRequest(
        hreq,
        hdr_ptr, hdr_len,
        req.body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)req.body.data(),
        (DWORD)req.body.size(),
        (DWORD)req.body.size(),
        0);
    if (!sent) {
        resp.error = FormatLastError(L"WinHttpSendRequest failed");
        return resp;
    }

    if (!WinHttpReceiveResponse(hreq, nullptr)) {
        resp.error = FormatLastError(L"WinHttpReceiveResponse failed");
        return resp;
    }

    DWORD status = 0;
    DWORD szlen = sizeof(status);
    WinHttpQueryHeaders(hreq,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &status, &szlen, WINHTTP_NO_HEADER_INDEX);
    resp.status = (int)status;

    // Read body
    std::string body;
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hreq, &avail)) {
            resp.error = FormatLastError(L"WinHttpQueryDataAvailable failed");
            return resp;
        }
        if (avail == 0) break;
        std::vector<char> buf(avail);
        DWORD read = 0;
        if (!WinHttpReadData(hreq, buf.data(), avail, &read)) {
            resp.error = FormatLastError(L"WinHttpReadData failed");
            return resp;
        }
        body.append(buf.data(), read);
        if (read == 0) break;
    }

    resp.body = std::move(body);
    resp.ok = (resp.status >= 200 && resp.status < 400);
    if (!resp.ok && resp.error.empty()) {
        wchar_t buf[64]; swprintf(buf, 64, L"HTTP %d", resp.status);
        resp.error = buf;
    }
    return resp;
}

} // namespace http
