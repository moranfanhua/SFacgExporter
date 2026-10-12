#pragma once
#include <cstdint>
#include <initializer_list>
#include <map>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace sf {
std::string utf8(char32_t c);
std::u32string codepoints(const std::string &s);
class Json {
  public:
    using object = std::map<std::string, Json>;
    using array = std::vector<Json>;
    std::variant<std::nullptr_t, bool, int64_t, std::string, object, array> value;
    Json();
    Json(std::nullptr_t);
    Json(bool v);
    Json(int v);
    Json(int64_t v);
    Json(const char *v);
    Json(std::string v);
    Json(object v);
    Json(array v);
    const Json &operator[](const std::string &k) const;
    Json &operator[](const std::string &k);
    std::string text(std::string fallback = "") const;
    int64_t number(int64_t fallback = 0) const;
    bool boolean(bool fallback = false) const;
    bool empty() const;
    const array &items() const;
    const object &fields() const;
    static std::string quote(const std::string &s);
    std::string dump() const;
    static Json parse(const std::string &input);
};
Json obj(std::initializer_list<std::pair<const std::string, Json>> v);
} // namespace sf
