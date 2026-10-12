#pragma once
#include "SFECore/codec.hpp"

namespace sf {
class Zip {
    std::string data;
    struct Entry {
        std::string name;
        uint32_t crc, size, offset;
    };
    std::vector<Entry> entries;
    void u16(uint16_t v);
    void u32(uint32_t v);
    static uint32_t crc32(const std::string &s);

  public:
    void add(std::string name, const std::string &bytes);
    std::string finish();
};
struct SavedChapter {
    Json info;
    std::string content;
};
std::string page(const std::string &title, const std::string &body);
std::string paragraphs(std::string text);
std::string plain_images(const std::string &s);
void write_txt(const Json &book, const std::vector<SavedChapter> &chapters, const fs::path &path,
               std::function<void()> check);
void write_epub(const Json &book, const std::vector<SavedChapter> &chapters, const fs::path &path,
                std::function<std::string(std::string)> fetch, std::function<void(std::string)> log,
                std::function<void()> check);
} // namespace sf
