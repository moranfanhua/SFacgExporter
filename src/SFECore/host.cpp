#include "SFECore.h"
#include "SFECore/host.hpp"
#include "SFECore/util.hpp"
#include <mutex>
#include <string_view>

namespace {
std::mutex hooks_mutex;
sf_host_callback host_request = nullptr;
sf_host_free_callback host_release = nullptr;
} // namespace

extern "C" SF_API void sf_set_host_callbacks(sf_host_callback call, sf_host_free_callback free_response) {
    std::lock_guard lock(hooks_mutex);
    host_request = call;
    host_release = free_response;
}

extern "C" SF_API int sf_uses_host_callbacks(void) {
#ifdef _WIN32
    return 0;
#else
    return 1;
#endif
}

namespace sf {
Json host_call(const Json &request) {
    sf_host_callback call;
    sf_host_free_callback release;
    {
        std::lock_guard lock(hooks_mutex);
        call = host_request;
        release = host_release;
    }
    if (!call || !release)
        throw Error("会话加密服务尚未初始化，请先注册加密回调");
    auto payload = request.dump();
    char *response = call(payload.c_str());
    if (!response)
        throw Error("平台服务未返回结果");
    Json result;
    try {
        result = Json::parse(response);
    } catch (...) {
        release(response);
        throw;
    }
    release(response);
    if (!result["ok"].boolean())
        throw Error(result["error"].text("平台服务失败"), int(result["code"].number()),
                    result["retry"].boolean(), result["cancelled"].boolean());
    return result["result"];
}

std::string base64_encode(const std::string &bytes) {
    constexpr auto alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (size_t i = 0; i < bytes.size(); i += 3) {
        uint32_t n = uint32_t(uint8_t(bytes[i])) << 16;
        if (i + 1 < bytes.size())
            n |= uint32_t(uint8_t(bytes[i + 1])) << 8;
        if (i + 2 < bytes.size())
            n |= uint8_t(bytes[i + 2]);
        out += alphabet[(n >> 18) & 63];
        out += alphabet[(n >> 12) & 63];
        out += i + 1 < bytes.size() ? alphabet[(n >> 6) & 63] : '=';
        out += i + 2 < bytes.size() ? alphabet[n & 63] : '=';
    }
    return out;
}

std::string base64_decode(const std::string &text) {
    constexpr auto alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    if (text.size() % 4 != 0)
        throw Error("平台服务返回的二进制数据格式无效");
    std::string out;
    for (size_t i = 0; i < text.size(); i += 4) {
        uint32_t n = 0;
        int padding = 0;
        for (size_t j = 0; j < 4; ++j) {
            char c = text[i + j];
            n <<= 6;
            if (c == '=') {
                if (i + 4 != text.size() || j < 2)
                    throw Error("二进制数据填充格式无效");
                ++padding;
            } else {
                auto p = std::string_view(alphabet).find(c);
                if (padding || p == std::string_view::npos)
                    throw Error("二进制数据编码无效");
                n |= uint32_t(p);
            }
        }
        out += char(n >> 16);
        if (padding < 2)
            out += char(n >> 8);
        if (padding < 1)
            out += char(n);
    }
    return out;
}
} // namespace sf
