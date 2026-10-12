#include "SFECore/transport.hpp"
#include <algorithm>
#include <chrono>
#include <sstream>
#include <thread>

namespace sf {
Response::Response() : status(200) {}

Transport::Transport() : active(nullptr), session(nullptr), cancelled(false) {}

std::string Response::header(std::string k) const {
    auto i = headers.find(lower(k));
    return i == headers.end() || i->second.empty() ? "" : i->second[0];
}

Transport::Active::~Active() {
    std::lock_guard lock(t.active_mutex);
    if (t.active == h) {
        WinHttpCloseHandle(h);
        t.active = nullptr;
    }
}

Transport::~Transport() {
    for (auto &[key, connection] : connections)
        WinHttpCloseHandle(connection);
    if (session)
        WinHttpCloseHandle(session);
}

void Transport::check() const {
    if (cancelled)
        throw Error("任务已停止，缓存可在下次导出时继续使用", -1, false, true);
}

void Transport::cancel() {
    cancelled = true;
    std::lock_guard lock(active_mutex);
    if (active) {
        WinHttpCloseHandle(active);
        active = nullptr;
    }
}

void Transport::pause() {
    int delay = random_delay();
    for (int elapsed = 0; elapsed < delay; elapsed += 10) {
        check();
        std::this_thread::sleep_for(std::chrono::milliseconds(std::min(10, delay - elapsed)));
    }
    check();
}

Response Transport::send(std::string url, const std::string &method,
                         const std::map<std::string, std::string> &headers, const std::string &body,
                         size_t limit, bool signed_request) {
    check();
    if (url.starts_with("//"))
        url = "https:" + url;
    if (url.starts_with("http://"))
        url = "https://" + url.substr(7);
    auto wurl = wide(url);
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = DWORD(-1);
    if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS)
        throw Error("只接受 SF 官方 HTTPS 地址");
    std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    auto hname = lower(narrow(host));
    if (hname != "sfacg.com" && !hname.ends_with(".sfacg.com"))
        throw Error("地址不属于 SF 官方域名");
    if (fixture)
        return fixture(url, method, headers, body);
    if (!session) {
        session = WinHttpOpen(L"SFExporter/0.9.4", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                              WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session)
            throw Error("网络初始化失败", 0, true);
        WinHttpSetTimeouts(session, 25000, 25000, 25000, 25000);
    }
    auto key = std::make_pair(host, parts.nPort);
    auto &connection = connections[key];
    if (!connection)
        connection = WinHttpConnect(session, host.c_str(), parts.nPort, 0);
    if (!connection)
        throw Error("连接服务器失败", 0, true);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength)
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    if (path.empty())
        path = L"/";
    HINTERNET request =
        WinHttpOpenRequest(connection, wide(method).c_str(), path.c_str(), nullptr, WINHTTP_NO_REFERER,
                           WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (!request)
        throw Error("创建网络请求失败", 0, true);
    {
        std::lock_guard lock(active_mutex);
        active = request;
    }
    Active guard{*this, request};
    check();
    DWORD disabled = WINHTTP_DISABLE_COOKIES | (signed_request ? WINHTTP_DISABLE_REDIRECTS : 0);
    WinHttpSetOption(request, WINHTTP_OPTION_DISABLE_FEATURE, &disabled, sizeof(disabled));
    DWORD decompress = WINHTTP_DECOMPRESSION_FLAG_GZIP | WINHTTP_DECOMPRESSION_FLAG_DEFLATE;
    WinHttpSetOption(request, WINHTTP_OPTION_DECOMPRESSION, &decompress, sizeof(decompress));
    std::string raw;
    for (auto &[k, v] : headers) {
        if (k.find_first_of("\r\n") != std::string::npos || v.find_first_of("\r\n") != std::string::npos)
            throw Error("请求头格式无效");
        raw += k + ": " + v + "\r\n";
    }
    auto wh = wide(raw);
    if (!wh.empty() && !WinHttpAddRequestHeaders(request, wh.c_str(), DWORD(wh.size()),
                                                 WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
        check();
        throw Error("设置请求头失败", 0, true);
    }
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            body.empty() ? WINHTTP_NO_REQUEST_DATA : (void *)body.data(), DWORD(body.size()),
                            DWORD(body.size()), 0) ||
        !WinHttpReceiveResponse(request, nullptr)) {
        check();
        throw Error("网络请求失败（Windows 错误 " + std::to_string(GetLastError()) + "）", 0, true);
    }
    Response out;
    DWORD status = 0, bytes = sizeof(status);
    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &bytes, WINHTTP_NO_HEADER_INDEX);
    out.status = int(status);
    bytes = 0;
    WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, nullptr,
                        &bytes, WINHTTP_NO_HEADER_INDEX);
    std::wstring rh(bytes / sizeof(wchar_t), 0);
    if (WinHttpQueryHeaders(request, WINHTTP_QUERY_RAW_HEADERS_CRLF, WINHTTP_HEADER_NAME_BY_INDEX, rh.data(),
                            &bytes, WINHTTP_NO_HEADER_INDEX)) {
        std::istringstream lines(narrow(rh));
        std::string line;
        while (std::getline(lines, line)) {
            auto colon = line.find(':');
            if (colon != std::string::npos)
                out.headers[lower(trim(line.substr(0, colon)))].push_back(trim(line.substr(colon + 1)));
        }
    }
    for (;;) {
        check();
        char buffer[8192];
        DWORD read = 0;
        if (!WinHttpReadData(request, buffer, sizeof(buffer), &read)) {
            check();
            throw Error("响应读取失败", 0, true);
        }
        if (!read)
            break;
        if (out.body.size() + read > limit)
            throw Error("响应超过大小限制");
        out.body.append(buffer, read);
    }
    check();
    return out;
}

} // namespace sf
