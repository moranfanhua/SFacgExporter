#include "SFECore/util.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#if defined(_WIN32)
#include <windows.h>
#endif
#ifdef _WIN32
#include <bcrypt.h>
#include <objbase.h>
#include <wincrypt.h>
#else
#include <openssl/evp.h>
#include <openssl/rand.h>
#endif
#include "SFECore/host.hpp"
#include <chrono>
#include <cctype>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <locale>
#include <random>
#include <sstream>

namespace sf {
Error::Error(std::string text, int c, bool r, bool cancel)
    : runtime_error(text), code(c), retry(r), cancelled(cancel) {}

std::wstring wide(const std::string &s) {
#ifdef _WIN32
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
    if (!n)
        throw Error("UTF-8 路径无效");
    std::wstring r(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), r.data(), n);
    return r;
#else
    auto points = codepoints(s);
    return std::wstring(points.begin(), points.end());
#endif
}

std::string narrow(const std::wstring &s) {
#ifdef _WIN32
    if (s.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
    std::string r(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), r.data(), n, nullptr, nullptr);
    return r;
#else
    std::string out;
    for (auto c : s)
        out += utf8(char32_t(c));
    return out;
#endif
}

fs::path path_from_utf8(const std::string &s) {
#ifdef _WIN32
    return fs::path(wide(s));
#else
    codepoints(s);
    return fs::path(s);
#endif
}

std::string path_utf8(const fs::path &p) {
#ifdef _WIN32
    return narrow(p.wstring());
#else
    return p.string();
#endif
}

void secure_erase(void *data, size_t size) {
    auto bytes = static_cast<volatile unsigned char *>(data);
    while (size--)
        *bytes++ = 0;
}

std::optional<int64_t> http_time(const std::string &date) {
    std::tm time{};
    std::istringstream input(date);
    input.imbue(std::locale::classic());
    input >> std::get_time(&time, "%a, %d %b %Y %H:%M:%S GMT");
    if (input.fail())
        return std::nullopt;
#ifdef _WIN32
    auto seconds = _mkgmtime(&time);
#else
    auto seconds = timegm(&time);
#endif
    if (seconds < 0)
        return std::nullopt;
    return int64_t(seconds) * 1000;
}

std::string utc_timestamp() {
    auto seconds = std::time(nullptr);
    std::tm time{};
#ifdef _WIN32
    gmtime_s(&time, &seconds);
#else
    gmtime_r(&seconds, &time);
#endif
    char date[32];
    std::strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%SZ", &time);
    return date;
}

std::string trim(std::string s) {
    auto white = [](unsigned char c) { return std::isspace(c); };
    while (!s.empty() && white(s.back()))
        s.pop_back();
    size_t p = 0;
    while (p < s.size() && white(s[p]))
        p++;
    return s.substr(p);
}

std::string lower(std::string s) {
    for (char &c : s)
        if (c >= 'A' && c <= 'Z')
            c += 32;
    return s;
}

void replace_all(std::string &s, const std::string &a, const std::string &b) {
    if (a.empty())
        return;
    size_t p = 0;
    while ((p = s.find(a, p)) != std::string::npos) {
        s.replace(p, a.size(), b);
        p += b.size();
    }
}

std::string xml(std::string s) {
    replace_all(s, "&", "&amp;");
    replace_all(s, "<", "&lt;");
    replace_all(s, ">", "&gt;");
    replace_all(s, "\"", "&quot;");
    replace_all(s, "'", "&apos;");
    std::string r;
    for (unsigned char c : s)
        if (c >= 32 || c == 9 || c == 10 || c == 13)
            r += char(c);
    return r;
}

std::string guid() {
#ifdef _WIN32
    GUID g;
    if (CoCreateGuid(&g) != S_OK)
        throw Error("无法生成请求标识");
    wchar_t b[40];
    StringFromGUID2(g, b, 40);
    return narrow(std::wstring(b + 1, 36));
#else
    unsigned char bytes[16];
    if (RAND_bytes(bytes, sizeof(bytes)) != 1)
        throw Error("无法生成请求标识");
    bytes[6] = (bytes[6] & 15) | 64;
    bytes[8] = (bytes[8] & 63) | 128;
    constexpr auto hex = "0123456789ABCDEF";
    std::string value;
    for (size_t i = 0; i < sizeof(bytes); ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) value += '-';
        value += hex[bytes[i] >> 4];
        value += hex[bytes[i] & 15];
    }
    return value;
#endif
}

int64_t now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string read_file(const fs::path &p, size_t limit) {
    std::ifstream f(p, std::ios::binary);
    if (!f)
        throw Error("无法读取文件：" + path_utf8(p));
    f.seekg(0, std::ios::end);
    auto n = f.tellg();
    if (n < 0 || uint64_t(n) > limit)
        throw Error("文件超过大小限制");
    std::string s(size_t(n), '\0');
    f.seekg(0);
    if (!s.empty() && !f.read(s.data(), s.size()))
        throw Error("文件读取不完整");
    return s;
}

void atomic_write(const fs::path &p, const std::string &data) {
    fs::create_directories(p.parent_path());
    auto tmp = p;
    tmp += "." + guid() + ".part";
    try {
        std::ofstream f(tmp, std::ios::binary);
        f.write(data.data(), data.size());
        f.flush();
        if (!f)
            throw Error("写入文件失败");
        f.close();
#ifdef _WIN32
        if (!MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw Error("保存文件失败：" + path_utf8(p));
#else
        fs::rename(tmp, p);
#endif
    } catch (...) {
        std::error_code ec;
        fs::remove(tmp, ec);
        throw;
    }
}

Json read_json(const fs::path &p) { return Json::parse(read_file(p)); }

void save_json(const fs::path &p, const Json &j) { atomic_write(p, j.dump()); }

std::string md5(const std::string &s) {
#ifdef _WIN32
    BCRYPT_ALG_HANDLE a = nullptr;
    BCRYPT_HASH_HANDLE h = nullptr;
    unsigned char bytes[16];
    if (BCryptOpenAlgorithmProvider(&a, BCRYPT_MD5_ALGORITHM, nullptr, 0) < 0)
        throw Error("MD5 初始化失败");
    auto close = [&] {
        if (h)
            BCryptDestroyHash(h);
        BCryptCloseAlgorithmProvider(a, 0);
    };
    if (BCryptCreateHash(a, &h, nullptr, 0, nullptr, 0, 0) < 0 ||
        BCryptHashData(h, (PUCHAR)s.data(), ULONG(s.size()), 0) < 0 ||
        BCryptFinishHash(h, bytes, 16, 0) < 0) {
        close();
        throw Error("签名计算失败");
    }
    close();
    std::string out;
    const char *hex = "0123456789ABCDEF";
    for (auto b : bytes) {
        out += hex[b >> 4];
        out += hex[b & 15];
    }
    return out;
#else
    unsigned char bytes[EVP_MAX_MD_SIZE];
    unsigned int length = 0;
    if (EVP_Digest(s.data(), s.size(), bytes, &length, EVP_md5(), nullptr) != 1 || length != 16)
        throw Error("签名计算失败");
    constexpr auto hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned int i = 0; i < length; ++i) {
        out += hex[bytes[i] >> 4];
        out += hex[bytes[i] & 15];
    }
    return out;
#endif
}

std::string sign(const std::string &nonce, int64_t timestamp, const std::string &device) {
    if (nonce.size() != 36 || device.size() != 36)
        throw Error("请求标识无效");
    std::string repeated = nonce + nonce + nonce + nonce, reordered;
    int sizes[] = {13, 16, 36, 36};
    for (int i = 0; i < 4; i++)
        reordered += repeated.substr((unsigned char)repeated[i + 1] % 36, sizes[i]);
    auto source = std::to_string(timestamp) + "lPQDb9AKO7$LjkPG" + device + nonce;
    if (source.size() != 101)
        throw Error("请求时间戳无效");
    std::string averaged;
    for (int i = 0; i < 101; i++)
        averaged += char(((unsigned char)source[i] + (unsigned char)reordered[i]) >> 1);
    auto shuffled =
        averaged.substr(65) + averaged.substr(0, 13) + averaged.substr(29, 36) + averaged.substr(13, 16);
    for (char &c : shuffled) {
        unsigned char n = c;
        if (n < '0')
            c = char(n + 19 > '9' && n + 19 < 'A' ? '9' : n + 19);
        else if ((n > '9' && n < 'A') || (n > 'Z' && n < 'a'))
            c = char(n + 19);
    }
    return md5(shuffled);
}

std::string protected_data(std::string data, bool decrypt) {
#ifdef _WIN32
    DATA_BLOB in{DWORD(data.size()), (BYTE *)data.data()}, out{};
    BOOL ok =
        decrypt ? CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)
                : CryptProtectData(&in, L"", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out);
    secure_erase(data.data(), data.size());
    if (!ok)
        throw Error("本机登录会话加密或读取失败，请重新登录");
    std::string r((char *)out.pbData, out.cbData);
    secure_erase(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return r;
#else
    try {
        auto result =
            host_call(obj({{"action", decrypt ? "unprotect" : "protect"}, {"data", base64_encode(data)}}))
                .text();
        secure_erase(data.data(), data.size());
        return base64_decode(result);
    } catch (...) {
        secure_erase(data.data(), data.size());
        throw;
    }
#endif
}

int random_delay() {
    thread_local std::mt19937 rng(std::random_device{}());
    return std::uniform_int_distribution<int>(50, 150)(rng);
}

std::string safe_name(const std::string &s) {
    auto chars = codepoints(s);
    std::string out;
    size_t count = 0;
    for (auto c : chars) {
        if (++count > 90)
            break;
        out += c < 32 || std::u32string(U"<>:\"/\\|?*").find(c) != std::u32string::npos ? "_" : utf8(c);
    }
    while (!out.empty() && (out.back() == '.' || out.back() == ' '))
        out.pop_back();
    return out.empty() ? "小说" : out;
}

} // namespace sf
