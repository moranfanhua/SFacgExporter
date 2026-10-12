#pragma once
#include "SFECore/util.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <functional>
#include <mutex>
#include <winhttp.h>

namespace sf {
struct Response {
    int status;
    std::string body;
    std::map<std::string, std::vector<std::string>> headers;
    Response();
    std::string header(std::string k) const;
};
class Transport {
    std::mutex active_mutex;
    HINTERNET active;
    HINTERNET session;
    std::map<std::pair<std::wstring, INTERNET_PORT>, HINTERNET> connections;
    struct Active {
        Transport &t;
        HINTERNET h;
        ~Active();
    };

  public:
    Transport();
    ~Transport();
    std::atomic<bool> cancelled;
    // Used only by the in-process self-test, never configured by the UI.
    std::function<Response(const std::string &, const std::string &,
                           const std::map<std::string, std::string> &, const std::string &)>
        fixture;
    void check() const;
    void cancel();
    void pause();
    Response send(std::string url, const std::string &method,
                  const std::map<std::string, std::string> &headers = {}, const std::string &body = "",
                  size_t limit = 25 * 1024 * 1024, bool signed_request = false);
};
} // namespace sf
