#pragma once
#include "SFECore/json.hpp"
#include <filesystem>
#include <optional>
#include <stdexcept>

namespace sf {
namespace fs = std::filesystem;
struct Error : std::runtime_error {
    int code;
    bool retry;
    bool cancelled;
    Error(std::string text, int c = 0, bool r = false, bool cancel = false);
};
std::wstring wide(const std::string &s);
std::string narrow(const std::wstring &s);
fs::path path_from_utf8(const std::string &s);
std::string path_utf8(const fs::path &p);
void secure_erase(void *data, size_t size);
std::optional<int64_t> http_time(const std::string &date);
std::string utc_timestamp();
std::string trim(std::string s);
std::string lower(std::string s);
void replace_all(std::string &s, const std::string &a, const std::string &b);
std::string xml(std::string s);
std::string guid();
int64_t now();
std::string read_file(const fs::path &p, size_t limit = 64 * 1024 * 1024);
void atomic_write(const fs::path &p, const std::string &data);
Json read_json(const fs::path &p);
void save_json(const fs::path &p, const Json &j);
std::string md5(const std::string &s);
std::string sign(const std::string &nonce, int64_t timestamp, const std::string &device);
std::string protected_data(std::string data, bool decrypt);
int random_delay();
std::string safe_name(const std::string &s);
} // namespace sf
