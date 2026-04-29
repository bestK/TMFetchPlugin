#include "OptionsDialog.h"
#include "resource.h"
#include "HttpClient.h"
#include "JsonPath.h"
#include "StringUtil.h"
#include <Windows.h>
#include <CommCtrl.h>
#include <nlohmann/json.hpp>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "comctl32.lib")

namespace ui {

namespace {

using nlohmann::json;

struct EditCtx {
    ItemConfig* item;
};

struct MainCtx {
    PluginConfig* cfg;
    HINSTANCE hInst;
};

// ---------- helpers ----------
std::wstring GetText(HWND h) {
    int n = GetWindowTextLengthW(h);
    if (n <= 0) return L"";
    std::wstring s(n, L'\0');
    GetWindowTextW(h, s.data(), n + 1);
    return s;
}

std::wstring GetDlgText(HWND dlg, int id) { return GetText(GetDlgItem(dlg, id)); }

void SetDlgText(HWND dlg, int id, const std::wstring& s) {
    SetDlgItemTextW(dlg, id, s.c_str());
}

int GetDlgInt(HWND dlg, int id, int def = 0) {
    BOOL ok = FALSE;
    UINT v = GetDlgItemInt(dlg, id, &ok, FALSE);
    return ok ? (int)v : def;
}

void SetDlgInt(HWND dlg, int id, int v) {
    SetDlgItemInt(dlg, id, (UINT)v, FALSE);
}

// Multiline edit: split CRLF/LF into lines (trim each)
std::vector<std::wstring> TextToLines(const std::wstring& s) {
    std::vector<std::wstring> out;
    std::wstring line;
    for (wchar_t c : s) {
        if (c == L'\r') continue;
        if (c == L'\n') { line = strutil::Trim(line); if (!line.empty()) out.push_back(line); line.clear(); }
        else line.push_back(c);
    }
    line = strutil::Trim(line);
    if (!line.empty()) out.push_back(line);
    return out;
}

// Convert bare LF / CR / CRLF to CRLF so a Windows multi-line edit control
// renders line breaks (it ignores lone LF). Round-tripping through
// GetDlgItemText then config::UnescapeMultiline normalises back to LF.
std::wstring ToCrLf(const std::wstring& s) {
    std::wstring out;
    out.reserve(s.size() + 4);
    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t c = s[i];
        if (c == L'\r') {
            out += L"\r\n";
            if (i + 1 < s.size() && s[i+1] == L'\n') ++i; // skip LF of CRLF
        } else if (c == L'\n') {
            out += L"\r\n";
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::wstring LinesToText(const std::vector<std::wstring>& lines) {
    std::wstring s;
    for (size_t i = 0; i < lines.size(); ++i) {
        if (i) s += L"\r\n";
        s += lines[i];
    }
    return s;
}

// ---------- edit dialog ----------
INT_PTR CALLBACK EditProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    static EditCtx* ctx = nullptr;
    switch (msg) {
    case WM_INITDIALOG: {
        ctx = reinterpret_cast<EditCtx*>(lp);
        SetWindowTextW(hDlg, L"编辑项目");

        SetDlgText(hDlg, IDC_LBL_NAME,        L"名称:");
        SetDlgText(hDlg, IDC_LBL_ID,          L"唯一ID:");
        SetDlgText(hDlg, IDC_LBL_URL,         L"URL:");
        SetDlgText(hDlg, IDC_LBL_METHOD,      L"方法:");
        SetDlgText(hDlg, IDC_LBL_JSONPATH,
            L"显示模板（写普通文字 + $.path 引用，例：电量:$.battery.level%  状态:$.status）");
        SetDlgText(hDlg, IDC_LBL_HEADERS,     L"请求头 (每行 Key: Value)：");
        SetDlgText(hDlg, IDC_LBL_BODY,        L"Body:");
        SetDlgText(hDlg, IDC_LBL_INTERVAL_IT, L"刷新间隔(ms,0=默认):");
        SetDlgText(hDlg, IDC_LBL_TIMEOUT,     L"超时(ms):");
        SetDlgText(hDlg, IDC_TEST,            L"测试请求");
        SetDlgText(hDlg, IDOK,                L"确定");
        SetDlgText(hDlg, IDCANCEL,            L"取消");

        HWND cb = GetDlgItem(hDlg, IDC_METHOD);
        for (auto* m : { L"GET", L"POST", L"PUT", L"DELETE", L"HEAD", L"PATCH" }) {
            SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)m);
        }

        const ItemConfig& it = *ctx->item;
        SetDlgText(hDlg, IDC_NAME,        it.name);
        SetDlgText(hDlg, IDC_ID,          it.id);
        SetDlgText(hDlg, IDC_URL,         it.url);
        SetDlgText(hDlg, IDC_JSONPATH,    ToCrLf(it.jsonpath));
        SetDlgInt (hDlg, IDC_INTERVAL_ITEM, it.interval_ms);
        SetDlgInt (hDlg, IDC_TIMEOUT,       it.timeout_ms > 0 ? it.timeout_ms : 8000);

        std::wstring m = it.method.empty() ? std::wstring(L"GET") : it.method;
        int idx = (int)SendMessageW(cb, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)m.c_str());
        if (idx == CB_ERR) { SendMessageW(cb, CB_ADDSTRING, 0, (LPARAM)m.c_str()); idx = (int)SendMessageW(cb, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)m.c_str()); }
        SendMessageW(cb, CB_SETCURSEL, (WPARAM)idx, 0);

        SetDlgText(hDlg, IDC_HEADERS, LinesToText(it.headers));
        SetDlgText(hDlg, IDC_BODY,    strutil::Utf8ToWide(it.body));
        return TRUE;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        if (id == IDOK) {
            ItemConfig& it = *ctx->item;
            it.name        = strutil::Trim(GetDlgText(hDlg, IDC_NAME));
            it.id          = strutil::Trim(GetDlgText(hDlg, IDC_ID));
            it.url         = strutil::Trim(GetDlgText(hDlg, IDC_URL));
            it.jsonpath    = config::UnescapeMultiline(GetDlgText(hDlg, IDC_JSONPATH));
            it.method      = strutil::Trim(GetDlgText(hDlg, IDC_METHOD));
            if (it.method.empty()) it.method = L"GET";
            it.interval_ms = GetDlgInt(hDlg, IDC_INTERVAL_ITEM, 0);
            it.timeout_ms  = GetDlgInt(hDlg, IDC_TIMEOUT, 8000);
            it.headers     = TextToLines(GetDlgText(hDlg, IDC_HEADERS));
            it.body        = strutil::WideToUtf8(GetDlgText(hDlg, IDC_BODY));

            if (it.id.empty() || it.url.empty()) {
                MessageBoxW(hDlg, L"唯一ID 和 URL 不能为空", L"提示", MB_ICONWARNING | MB_OK);
                return TRUE;
            }
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        if (id == IDCANCEL) { EndDialog(hDlg, IDCANCEL); return TRUE; }
        if (id == IDC_TEST) {
            // Build a temporary ItemConfig from current fields and run a one-shot fetch.
            ItemConfig tmp;
            tmp.url      = strutil::Trim(GetDlgText(hDlg, IDC_URL));
            tmp.method   = strutil::Trim(GetDlgText(hDlg, IDC_METHOD));
            if (tmp.method.empty()) tmp.method = L"GET";
            tmp.headers  = TextToLines(GetDlgText(hDlg, IDC_HEADERS));
            tmp.body     = strutil::WideToUtf8(GetDlgText(hDlg, IDC_BODY));
            tmp.jsonpath = config::UnescapeMultiline(GetDlgText(hDlg, IDC_JSONPATH));
            tmp.timeout_ms = GetDlgInt(hDlg, IDC_TIMEOUT, 8000);

            if (tmp.url.empty()) {
                SetDlgText(hDlg, IDC_TEST_RESULT, L"URL 为空");
                return TRUE;
            }
            SetDlgText(hDlg, IDC_TEST_RESULT, L"请求中...");
            EnableWindow(GetDlgItem(hDlg, IDC_TEST), FALSE);

            // Run on background thread so the dialog stays responsive.
            HWND dlg = hDlg;
            std::thread([dlg, tmp]() {
                http::Request req;
                req.url = tmp.url;
                req.method = tmp.method;
                req.headers = tmp.headers;
                req.body = tmp.body;
                req.timeout_ms = tmp.timeout_ms > 0 ? tmp.timeout_ms : 8000;
                http::Response r = http::Fetch(req);

                std::wstring msg;
                if (!r.ok) {
                    msg = L"请求失败: " + r.error;
                    if (!r.body.empty()) msg += L"\r\n响应: " + strutil::Utf8ToWide(r.body.substr(0, 500));
                } else {
                    msg = L"HTTP " + std::to_wstring(r.status);
                    if (tmp.jsonpath.empty()) {
                        msg += L"\r\n--- 原始响应 (前 500 字节) ---\r\n";
                        msg += strutil::Utf8ToWide(r.body.substr(0, 500));
                    } else {
                        try {
                            json doc = json::parse(r.body, nullptr, true, true);
                            std::string p = strutil::WideToUtf8(tmp.jsonpath);
                            std::string rendered = jpath::RenderTemplate(doc, p, "--");
                            msg += L"\r\n[渲染结果]\r\n";
                            msg += strutil::Utf8ToWide(rendered);
                        } catch (const std::exception& e) {
                            msg += L"\r\nJSON 解析失败: " + strutil::Utf8ToWide(e.what());
                            if (!r.body.empty()) {
                                msg += L"\r\n响应预览:\r\n";
                                msg += strutil::Utf8ToWide(r.body.substr(0, 500));
                            }
                        }
                    }
                }
                // Marshal back to UI thread.
                std::wstring* heap = new std::wstring(std::move(msg));
                if (!PostMessageW(dlg, WM_APP + 1, 0, (LPARAM)heap)) delete heap;
            }).detach();
            return TRUE;
        }
        break;
    }
    case WM_APP + 1: {
        std::wstring* msg = reinterpret_cast<std::wstring*>(lp);
        if (msg) {
            SetDlgText(hDlg, IDC_TEST_RESULT, *msg);
            delete msg;
        }
        EnableWindow(GetDlgItem(hDlg, IDC_TEST), TRUE);
        return TRUE;
    }
    }
    return FALSE;
}

bool ShowEdit(HWND parent, HINSTANCE hInst, ItemConfig& it) {
    EditCtx ctx{ &it };
    INT_PTR r = DialogBoxParamW(hInst, MAKEINTRESOURCEW(IDD_EDIT_ITEM), parent,
                                EditProc, (LPARAM)&ctx);
    return r == IDOK;
}

// ---------- main dialog ----------
void RefreshList(HWND list, const PluginConfig& cfg, int selectIdx = -1) {
    ListView_DeleteAllItems(list);
    auto flatten = [](std::wstring s) {
        std::wstring out;
        out.reserve(s.size());
        for (wchar_t c : s) {
            if (c == L'\r') continue;
            out.push_back(c == L'\n' ? L' ' : c);
        }
        return out;
    };
    static thread_local std::vector<std::wstring> jpCache; // keep storage alive
    jpCache.clear();
    jpCache.reserve(cfg.items.size());
    for (size_t i = 0; i < cfg.items.size(); ++i) {
        const auto& it = cfg.items[i];
        jpCache.push_back(flatten(it.jsonpath));
        LVITEMW lv{};
        lv.mask = LVIF_TEXT;
        lv.iItem = (int)i;
        lv.pszText = (LPWSTR)it.name.c_str();
        ListView_InsertItem(list, &lv);
        ListView_SetItemText(list, (int)i, 1, (LPWSTR)it.url.c_str());
        ListView_SetItemText(list, (int)i, 2, (LPWSTR)jpCache[i].c_str());
    }
    if (selectIdx >= 0 && selectIdx < (int)cfg.items.size()) {
        ListView_SetItemState(list, selectIdx, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        ListView_EnsureVisible(list, selectIdx, FALSE);
    }
}

int GetSelected(HWND list) {
    return ListView_GetNextItem(list, -1, LVNI_SELECTED);
}

INT_PTR CALLBACK MainProc(HWND hDlg, UINT msg, WPARAM wp, LPARAM lp) {
    static MainCtx* ctx = nullptr;
    switch (msg) {
    case WM_INITDIALOG: {
        ctx = reinterpret_cast<MainCtx*>(lp);
        SetWindowTextW(hDlg, L"TMFetchPlugin 设置");
        SetDlgText(hDlg, IDC_LBL_INTERVAL, L"默认刷新间隔(ms):");
        SetDlgText(hDlg, IDC_LBL_ITEMS,    L"项目列表（双击编辑）");
        SetDlgText(hDlg, IDC_ADD,    L"新增");
        SetDlgText(hDlg, IDC_EDIT,   L"编辑");
        SetDlgText(hDlg, IDC_DELETE, L"删除");
        SetDlgText(hDlg, IDC_UP,     L"上移");
        SetDlgText(hDlg, IDC_DOWN,   L"下移");
        SetDlgText(hDlg, IDC_HINT,
            L"提示：编辑现有项目立即生效；新增 / 删除 / 重排序需要重启 TrafficMonitor 才能完全生效。");
        SetDlgText(hDlg, IDOK,       L"确定");
        SetDlgText(hDlg, IDCANCEL,   L"取消");

        SetDlgInt(hDlg, IDC_INTERVAL, ctx->cfg->interval_ms);

        HWND list = GetDlgItem(hDlg, IDC_LIST_ITEMS);
        ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES);
        struct Col { const wchar_t* t; int w; } cols[] = {
            { L"名称",      80 },
            { L"URL",      120 },
            { L"JsonPath", 130 },
        };
        for (int i = 0; i < 3; ++i) {
            LVCOLUMNW c{}; c.mask = LVCF_TEXT | LVCF_WIDTH;
            c.pszText = (LPWSTR)cols[i].t; c.cx = cols[i].w;
            ListView_InsertColumn(list, i, &c);
        }
        RefreshList(list, *ctx->cfg, ctx->cfg->items.empty() ? -1 : 0);
        return TRUE;
    }
    case WM_NOTIFY: {
        NMHDR* nm = (NMHDR*)lp;
        if (nm->idFrom == IDC_LIST_ITEMS && nm->code == NM_DBLCLK) {
            int sel = GetSelected(GetDlgItem(hDlg, IDC_LIST_ITEMS));
            if (sel >= 0) PostMessageW(hDlg, WM_COMMAND, MAKEWPARAM(IDC_EDIT, BN_CLICKED), 0);
        }
        break;
    }
    case WM_COMMAND: {
        WORD id = LOWORD(wp);
        HWND list = GetDlgItem(hDlg, IDC_LIST_ITEMS);
        PluginConfig& cfg = *ctx->cfg;

        if (id == IDC_ADD) {
            ItemConfig nw;
            nw.name = L"NewItem";
            nw.id   = L"new_item_" + std::to_wstring(cfg.items.size());
            nw.method = L"GET";
            nw.timeout_ms = 8000;
            if (ShowEdit(hDlg, ctx->hInst, nw)) {
                cfg.items.push_back(std::move(nw));
                RefreshList(list, cfg, (int)cfg.items.size() - 1);
            }
            return TRUE;
        }
        if (id == IDC_EDIT) {
            int sel = GetSelected(list);
            if (sel >= 0 && sel < (int)cfg.items.size()) {
                ItemConfig copy = cfg.items[sel];
                if (ShowEdit(hDlg, ctx->hInst, copy)) {
                    cfg.items[sel] = std::move(copy);
                    RefreshList(list, cfg, sel);
                }
            }
            return TRUE;
        }
        if (id == IDC_DELETE) {
            int sel = GetSelected(list);
            if (sel >= 0 && sel < (int)cfg.items.size()) {
                if (MessageBoxW(hDlg, L"确认删除该项目？", L"确认", MB_OKCANCEL | MB_ICONQUESTION) == IDOK) {
                    cfg.items.erase(cfg.items.begin() + sel);
                    int newSel = sel >= (int)cfg.items.size() ? (int)cfg.items.size() - 1 : sel;
                    RefreshList(list, cfg, newSel);
                }
            }
            return TRUE;
        }
        if (id == IDC_UP) {
            int sel = GetSelected(list);
            if (sel > 0) { std::swap(cfg.items[sel], cfg.items[sel-1]); RefreshList(list, cfg, sel-1); }
            return TRUE;
        }
        if (id == IDC_DOWN) {
            int sel = GetSelected(list);
            if (sel >= 0 && sel + 1 < (int)cfg.items.size()) {
                std::swap(cfg.items[sel], cfg.items[sel+1]);
                RefreshList(list, cfg, sel+1);
            }
            return TRUE;
        }
        if (id == IDOK) {
            cfg.interval_ms = GetDlgInt(hDlg, IDC_INTERVAL, 5000);
            if (cfg.interval_ms < 500) cfg.interval_ms = 500;
            EndDialog(hDlg, IDOK);
            return TRUE;
        }
        if (id == IDCANCEL) { EndDialog(hDlg, IDCANCEL); return TRUE; }
        break;
    }
    }
    return FALSE;
}

} // namespace

bool ShowOptions(HWND parent, HINSTANCE hInst, PluginConfig& cfg) {
    INITCOMMONCONTROLSEX icc{};
    icc.dwSize = sizeof(icc);
    icc.dwICC = ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&icc);

    MainCtx ctx{ &cfg, hInst };
    INT_PTR r = DialogBoxParamW(hInst, MAKEINTRESOURCEW(IDD_MAIN_OPTIONS),
                                parent, MainProc, (LPARAM)&ctx);
    return r == IDOK;
}

} // namespace ui
