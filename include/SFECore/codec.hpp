#pragma once
#include "SFECore/util.hpp"
#include <functional>

namespace sf {
struct ImageTag {
    size_t start, end;
    std::string url;
};
std::vector<ImageTag> images(const std::string &s);
std::string transform_text(const std::string &s, const std::function<std::string(const std::string &)> &f,
                           bool preserve = true);
class Codec {
    std::map<char32_t, char32_t> alphabet;

  public:
    explicit Codec(const fs::path &table_path);
    std::string decode(const std::string &s) const;
    static std::u32string han(const std::string &s);
    void verify(const std::string &raw, const std::string &clear) const;
};
std::string entities(const std::string &s);
std::map<std::string, std::string> attributes(const std::string &s);
std::string html_body(const std::string &html);
} // namespace sf
