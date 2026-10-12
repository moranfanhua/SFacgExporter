#include "SFECore/util.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <bcrypt.h>
#include <chrono>
#include <cctype>
#include <fstream>
#include <objbase.h>
#include <random>
#include <wincrypt.h>

namespace sf {
Error::Error(std::string text, int c, bool r, bool cancel)
    : runtime_error(text), code(c), retry(r), cancelled(cancel) {}

std::wstring wide(const std::string &s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), int(s.size()), nullptr, 0);
    if (!n)
        throw Error("UTF-8 路径无效");
    std::wstring r(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), r.data(), n);
    return r;
}

std::string narrow(const std::wstring &s) {
    if (s.empty())
        return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0, nullptr, nullptr);
    std::string r(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, s.data(), int(s.size()), r.data(), n, nullptr, nullptr);
    return r;
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
    GUID g;
    if (CoCreateGuid(&g) != S_OK)
        throw Error("无法生成请求标识");
    wchar_t b[40];
    StringFromGUID2(g, b, 40);
    return narrow(std::wstring(b + 1, 36));
}

int64_t now() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string read_file(const fs::path &p, size_t limit) {
    std::ifstream f(p, std::ios::binary);
    if (!f)
        throw Error("无法读取文件：" + narrow(p.wstring()));
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
    tmp += wide("." + guid() + ".part");
    try {
        std::ofstream f(tmp, std::ios::binary);
        f.write(data.data(), data.size());
        f.flush();
        if (!f)
            throw Error("写入文件失败");
        f.close();
        if (!MoveFileExW(tmp.c_str(), p.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw Error("保存文件失败：" + narrow(p.wstring()));
    } catch (...) {
        std::error_code ec;
        fs::remove(tmp, ec);
        throw;
    }
}

Json read_json(const fs::path &p) { return Json::parse(read_file(p)); }

void save_json(const fs::path &p, const Json &j) { atomic_write(p, j.dump()); }

std::string md5(const std::string &s) {
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
    DATA_BLOB in{DWORD(data.size()), (BYTE *)data.data()}, out{};
    BOOL ok =
        decrypt ? CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)
                : CryptProtectData(&in, L"", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out);
    SecureZeroMemory(data.data(), data.size());
    if (!ok)
        throw Error("本机登录会话加密或读取失败，请重新登录");
    std::string r((char *)out.pbData, out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return r;
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
