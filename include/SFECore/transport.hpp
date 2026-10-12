#pragma once
#include "SFECore/util.hpp"
#include <curl/curl.h>
#include <atomic>
#include <functional>

namespace sf {
struct Response {
    int status;
    std::string body;
    std::map<std::string, std::vector<std::string>> headers;
    Response();
    std::string header(std::string k) const;
};
class Transport {
    CURL *session;

  public:
    Transport();
    ~Transport();
    std::atomic<bool> cancelled;
    std::string ca_bundle;
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
