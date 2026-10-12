#include "SFECore/transport.hpp"
#include <algorithm>
#include <chrono>
#include <exception>
#include <memory>
#include <thread>

namespace sf {
namespace {
struct Transfer {
    Transport &transport;
    Response response;
    size_t limit;
    size_t header_bytes = 0;
    std::exception_ptr error;
};

size_t body_received(char *data, size_t size, size_t count, void *user) noexcept {
    auto &transfer = *static_cast<Transfer *>(user);
    size_t bytes = size * count;
    try {
        transfer.transport.check();
        if (bytes > transfer.limit - transfer.response.body.size())
            throw Error("响应超过大小限制");
        transfer.response.body.append(data, bytes);
        return bytes;
    } catch (...) {
        transfer.error = std::current_exception();
        return 0;
    }
}

size_t header_received(char *data, size_t size, size_t count, void *user) noexcept {
    auto &transfer = *static_cast<Transfer *>(user);
    size_t bytes = size * count;
    try {
        transfer.transport.check();
        transfer.header_bytes += bytes;
        if (transfer.header_bytes > 1024 * 1024)
            throw Error("响应头超过大小限制");
        std::string line(data, bytes);
        if (line.starts_with("HTTP/")) {
            transfer.response.headers.clear();
        } else if (auto colon = line.find(':'); colon != std::string::npos) {
            transfer.response.headers[lower(trim(line.substr(0, colon)))].push_back(trim(line.substr(colon + 1)));
        }
        return bytes;
    } catch (...) {
        transfer.error = std::current_exception();
        return 0;
    }
}

int progress_changed(void *user, curl_off_t, curl_off_t, curl_off_t, curl_off_t) noexcept {
    return static_cast<Transport *>(user)->cancelled ? 1 : 0;
}

std::string url_part(CURLU *url, CURLUPart part) {
    char *text = nullptr;
    if (curl_url_get(url, part, &text, 0) != CURLUE_OK)
        return {};
    std::unique_ptr<char, decltype(&curl_free)> guard(text, curl_free);
    return text;
}

std::string official_url(std::string value) {
    if (value.starts_with("//")) value = "https:" + value;
    if (value.starts_with("http://")) value = "https://" + value.substr(7);
    std::unique_ptr<CURLU, decltype(&curl_url_cleanup)> parsed(curl_url(), curl_url_cleanup);
    if (!parsed || curl_url_set(parsed.get(), CURLUPART_URL, value.c_str(), 0) != CURLUE_OK)
        throw Error("网络地址无效");
    auto host = lower(url_part(parsed.get(), CURLUPART_HOST));
    auto port = url_part(parsed.get(), CURLUPART_PORT);
    if (lower(url_part(parsed.get(), CURLUPART_SCHEME)) != "https" || (!port.empty() && port != "443") ||
        (!url_part(parsed.get(), CURLUPART_USER).empty()) || (!url_part(parsed.get(), CURLUPART_PASSWORD).empty()) ||
        (host != "sfacg.com" && !host.ends_with(".sfacg.com")))
        throw Error("只接受 SF 官方 HTTPS 地址");
    return url_part(parsed.get(), CURLUPART_URL);
}
} // namespace

Response::Response() : status(200) {}
Transport::Transport() : session(nullptr), cancelled(false) {
    static const CURLcode initialized = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (initialized != CURLE_OK || !(session = curl_easy_init()))
        throw Error("网络初始化失败", 0, true);
}
Transport::~Transport() { curl_easy_cleanup(session); }
std::string Response::header(std::string key) const {
    auto item = headers.find(lower(key));
    return item == headers.end() || item->second.empty() ? "" : item->second.front();
}
void Transport::check() const {
    if (cancelled) throw Error("任务已停止，缓存可在下次导出时继续使用", -1, false, true);
}
void Transport::cancel() { cancelled = true; }
void Transport::pause() {
    int delay = random_delay();
    for (int elapsed = 0; elapsed < delay; elapsed += 10) {
        check();
        std::this_thread::sleep_for(std::chrono::milliseconds((std::min)(10, delay - elapsed)));
    }
    check();
}

Response Transport::send(std::string url, const std::string &method,
                         const std::map<std::string, std::string> &headers, const std::string &body,
                         size_t limit, bool signed_request) {
    check();
    url = official_url(url);
    if (fixture) return fixture(url, method, headers, body);
    bool authenticated = false;
    for (const auto &[key, value] : headers) {
        if (key.find_first_of("\r\n") != std::string::npos || value.find_first_of("\r\n") != std::string::npos)
            throw Error("请求头格式无效");
        authenticated |= lower(key) == "cookie" || lower(key) == "authorization";
    }
    std::string request_method = method, request_body = body;
    for (int redirects = 0; ; ++redirects) {
        check();
        // Reset options while keeping libcurl's connection and DNS caches.
        curl_easy_reset(session);
        auto option = [&](CURLoption key, auto value) {
            if (curl_easy_setopt(session, key, value) != CURLE_OK) throw Error("设置网络请求失败");
        };
        Transfer transfer{*this, Response{}, limit};
        curl_slist *raw_headers = nullptr;
        std::unique_ptr<curl_slist, decltype(&curl_slist_free_all)> header_guard(nullptr, curl_slist_free_all);
        for (const auto &[key, value] : headers) {
            auto *appended = curl_slist_append(raw_headers, (key + ": " + value).c_str());
            if (!appended) throw Error("设置请求头失败");
            raw_headers = appended;
            header_guard.release();
            header_guard.reset(raw_headers);
        }
        option(CURLOPT_URL, url.c_str());
        option(CURLOPT_PROTOCOLS_STR, "https");
        option(CURLOPT_FOLLOWLOCATION, 0L); // Validate each public redirect before the next request.
        option(CURLOPT_NOSIGNAL, 1L);
        option(CURLOPT_CONNECTTIMEOUT_MS, 25000L);
        option(CURLOPT_TIMEOUT_MS, 25000L);
        option(CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_1_1);
        option(CURLOPT_ACCEPT_ENCODING, "gzip,deflate");
        option(CURLOPT_USERAGENT, "SFExporter/0.10.1");
        option(CURLOPT_HTTPHEADER, raw_headers);
        option(CURLOPT_SSL_VERIFYPEER, 1L);
        option(CURLOPT_SSL_VERIFYHOST, 2L);
#ifndef _WIN32
        auto certificates = read_file(path_from_utf8(ca_bundle), 1024 * 1024);
        curl_blob bundle{certificates.data(), certificates.size(), CURL_BLOB_COPY};
        option(CURLOPT_CAINFO_BLOB, &bundle);
#endif
        option(CURLOPT_WRITEFUNCTION, body_received);
        option(CURLOPT_WRITEDATA, &transfer);
        option(CURLOPT_HEADERFUNCTION, header_received);
        option(CURLOPT_HEADERDATA, &transfer);
        option(CURLOPT_NOPROGRESS, 0L);
        option(CURLOPT_XFERINFOFUNCTION, progress_changed);
        option(CURLOPT_XFERINFODATA, this);
        if (request_method == "POST" || !request_body.empty()) {
            option(CURLOPT_POSTFIELDS, request_body.c_str());
            option(CURLOPT_POSTFIELDSIZE_LARGE, curl_off_t(request_body.size()));
        }
        option(CURLOPT_CUSTOMREQUEST, request_method.c_str());
        CURLcode result = curl_easy_perform(session);
        check();
        if (transfer.error) std::rethrow_exception(transfer.error);
        if (result != CURLE_OK)
            throw Error(std::string("网络请求失败：") + curl_easy_strerror(result), int(result), true);
        long status = 0;
        curl_easy_getinfo(session, CURLINFO_RESPONSE_CODE, &status);
        transfer.response.status = int(status);
        if (!signed_request && (status == 301 || status == 302 || status == 303 || status == 307 || status == 308)) {
            char *next = nullptr;
            curl_easy_getinfo(session, CURLINFO_REDIRECT_URL, &next);
            if (next) {
                if (authenticated || redirects >= 4) throw Error("服务器重定向无效");
                url = official_url(next);
                if (status == 303 || ((status == 301 || status == 302) && request_method == "POST")) {
                    request_method = "GET";
                    request_body.clear();
                }
                continue;
            }
        }
        return std::move(transfer.response);
    }
}
} // namespace sf
