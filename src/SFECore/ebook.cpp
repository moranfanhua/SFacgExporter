#include "SFECore/ebook.hpp"
#include <cstdio>
#include <sstream>

namespace sf {
void Zip::u16(uint16_t v) {
    data += char(v);
    data += char(v >> 8);
}

void Zip::u32(uint32_t v) {
    u16(uint16_t(v));
    u16(uint16_t(v >> 16));
}

uint32_t Zip::crc32(const std::string &s) {
    uint32_t crc = 0xffffffff;
    for (unsigned char b : s) {
        crc ^= b;
        for (int i = 0; i < 8; i++)
            crc = (crc >> 1) ^ (0xedb88320u & uint32_t(-int(crc & 1)));
    }
    return ~crc;
}

void Zip::add(std::string name, const std::string &bytes) {
    if (name.size() > 65535 || bytes.size() > UINT32_MAX || data.size() > UINT32_MAX)
        throw Error("电子书超过 ZIP 大小限制");
    Entry e{name, crc32(bytes), uint32_t(bytes.size()), uint32_t(data.size())};
    u32(0x04034b50);
    u16(20);
    u16(0x800);
    u16(0);
    u16(0);
    u16(33);
    u32(e.crc);
    u32(e.size);
    u32(e.size);
    u16(uint16_t(name.size()));
    u16(0);
    data += name;
    data += bytes;
    entries.push_back(e);
}

std::string Zip::finish() {
    if (entries.size() > 65535)
        throw Error("电子书文件数量过多");
    uint32_t offset = uint32_t(data.size());
    for (auto &e : entries) {
        u32(0x02014b50);
        u16(20);
        u16(20);
        u16(0x800);
        u16(0);
        u16(0);
        u16(33);
        u32(e.crc);
        u32(e.size);
        u32(e.size);
        u16(uint16_t(e.name.size()));
        u16(0);
        u16(0);
        u16(0);
        u16(0);
        u32(0);
        u32(e.offset);
        data += e.name;
    }
    uint32_t size = uint32_t(data.size()) - offset;
    u32(0x06054b50);
    u16(0);
    u16(0);
    u16(uint16_t(entries.size()));
    u16(uint16_t(entries.size()));
    u32(size);
    u32(offset);
    u16(0);
    return std::move(data);
}

std::string page(const std::string &title, const std::string &body) {
    return "<?xml version=\"1.0\" encoding=\"utf-8\"?><html xmlns=\"http://www.w3.org/1999/xhtml\" "
           "lang=\"zh-CN\"><head><title>" +
           xml(title) + "</title><link rel=\"stylesheet\" href=\"style.css\"/></head><body>" + body +
           "</body></html>";
}

std::string paragraphs(std::string text) {
    replace_all(text, "\r", "");
    std::istringstream lines(text);
    std::string line, out;
    while (std::getline(lines, line))
        if (!trim(line).empty())
            out += "<p>" + xml(line) + "</p>";
    return out;
}

std::string plain_images(const std::string &s) {
    std::string out;
    size_t start = 0;
    for (auto &t : images(s)) {
        out += s.substr(start, t.start - start) + "【插图：" + t.url + "】";
        start = t.end;
    }
    return out + s.substr(start);
}

void write_txt(const Json &book, const std::vector<SavedChapter> &chapters, const fs::path &path,
               std::function<void()> check) {
    std::string out = "\xef\xbb\xbf" + book["Title"].text() + "\n作者：" + book["Author"].text() +
                      "\n来源：https://book.sfacg.com/Novel/" + book["Id"].text() + "/\n",
                volume;
    for (auto &c : chapters) {
        check();
        if (volume != c.info["Volume"].text()) {
            volume = c.info["Volume"].text();
            out += '\n' + volume + '\n';
        }
        out += '\n' + c.info["Title"].text() + "\n\n" + plain_images(c.content) + '\n';
    }
    check();
    atomic_write(path, out);
}

void write_epub(const Json &book, const std::vector<SavedChapter> &chapters, const fs::path &path,
                std::function<std::string(std::string)> fetch, std::function<void(std::string)> log,
                std::function<void()> check) {
    struct Asset {
        std::string name, type, bytes;
    };
    std::vector<Asset> assets;
    std::map<std::string, std::string> names;
    auto image = [&](const std::string &url, const std::string &source) {
        check();
        if (url.empty())
            return std::string();
        if (names.count(url))
            return names[url];
        try {
            auto bytes = fetch(url);
            std::string ext, type;
            if (bytes.size() >= 8 && bytes.substr(0, 4) == "\x89PNG") {
                ext = "png";
                type = "image/png";
            } else if (bytes.size() >= 3 && (unsigned char)bytes[0] == 255 &&
                       (unsigned char)bytes[1] == 216) {
                ext = "jpg";
                type = "image/jpeg";
            } else if (bytes.starts_with("GIF87a") || bytes.starts_with("GIF89a")) {
                ext = "gif";
                type = "image/gif";
            } else
                throw Error("图片格式暂不支持");
            std::string name = "images/image" + std::to_string(assets.size() + 1) + "." + ext;
            assets.push_back({name, type, std::move(bytes)});
            names[url] = name;
            return name;
        } catch (const Error &e) {
            if (e.cancelled)
                throw;
            log("图片未取得（" + source + "）：" + url + "；原因：" + e.what());
            names[url] = "";
            return std::string();
        }
    };
    auto cover = image(book["Cover"].text(), "封面");
    std::vector<std::pair<std::string, std::string>> pages;
    std::string manifest =
                    "<item id=\"nav\" href=\"nav.xhtml\" media-type=\"application/xhtml+xml\" "
                    "properties=\"nav\"/><item id=\"style\" href=\"style.css\" media-type=\"text/css\"/>",
                spine,
                nav = "<h1>" + xml(book["Title"].text()) +
                      "</h1><nav xmlns:epub=\"http://www.idpf.org/2007/ops\" epub:type=\"toc\" "
                      "id=\"toc\"><h2>目录</h2><ol>";
    if (!cover.empty()) {
        manifest += "<item id=\"coverpage\" href=\"cover.xhtml\" media-type=\"application/xhtml+xml\"/>";
        spine += "<itemref idref=\"coverpage\"/>";
        pages.emplace_back("cover.xhtml", page("封面", "<h1>" + xml(book["Title"].text()) + "</h1><p>" +
                                                           xml(book["Author"].text()) + "</p><img src=\"" +
                                                           cover + "\" alt=\"封面\"/>"));
    }
    size_t index = 0;
    std::string volume;
    bool group = false;
    for (auto &ch : chapters) {
        check();
        std::string body = "<h1>" + xml(ch.info["Title"].text()) + "</h1>";
        size_t start = 0;
        for (auto &t : images(ch.content)) {
            body += paragraphs(ch.content.substr(start, t.start - start));
            auto name = image(t.url, ch.info["Title"].text());
            body += name.empty()
                        ? "<p>" + xml("【插图未取得：" + t.url + "】") + "</p>"
                        : "<p class=\"illustration\"><img src=\"" + name + "\" alt=\"小说插图\"/></p>";
            start = t.end;
        }
        body += paragraphs(ch.content.substr(start));
        char number[32];
        snprintf(number, sizeof(number), "chapter%05zu.xhtml", index);
        auto name = std::string(number), id = "c" + std::to_string(index++);
        pages.emplace_back(name, page(ch.info["Title"].text(), body));
        manifest += "<item id=\"" + id + "\" href=\"" + name + "\" media-type=\"application/xhtml+xml\"/>";
        spine += "<itemref idref=\"" + id + "\"/>";
        auto v = ch.info["Volume"].text();
        if (!group || v != volume) {
            if (group)
                nav += "</ol></li>";
            nav += "<li><span>" + xml(v) + "</span><ol>";
            group = true;
            volume = v;
        }
        nav += "<li><a href=\"" + name + "\">" + xml(ch.info["Title"].text()) + "</a></li>";
    }
    if (group)
        nav += "</ol></li>";
    nav += "</ol></nav>";
    size_t a = 0;
    for (auto &asset : assets)
        manifest += "<item id=\"img" + std::to_string(a++) + "\" href=\"" + asset.name + "\" media-type=\"" +
                    asset.type + "\"" + (asset.name == cover ? " properties=\"cover-image\"" : "") + "/>";
    auto date = utc_timestamp();
    std::string opf =
        "<?xml version=\"1.0\" encoding=\"utf-8\"?><package xmlns=\"http://www.idpf.org/2007/opf\" "
        "version=\"3.0\" unique-identifier=\"bookid\"><metadata "
        "xmlns:dc=\"http://purl.org/dc/elements/1.1/\"><dc:identifier id=\"bookid\">urn:uuid:" +
        guid() + "</dc:identifier><dc:title>" + xml(book["Title"].text()) + "</dc:title><dc:creator>" +
        xml(book["Author"].text()) +
        "</dc:creator><dc:language>zh-CN</dc:language><dc:source>https://book.sfacg.com/Novel/" +
        xml(book["Id"].text()) + "/</dc:source><meta property=\"dcterms:modified\">" + date +
        "</meta></metadata><manifest>" + manifest + "</manifest><spine>" + spine + "</spine></package>";
    Zip zip;
    zip.add("mimetype", "application/epub+zip");
    zip.add("META-INF/container.xml",
            "<?xml version=\"1.0\"?><container version=\"1.0\" "
            "xmlns=\"urn:oasis:names:tc:opendocument:xmlns:container\"><rootfiles><rootfile "
            "full-path=\"OEBPS/content.opf\" "
            "media-type=\"application/oebps-package+xml\"/></rootfiles></container>");
    zip.add("OEBPS/content.opf", opf);
    zip.add("OEBPS/nav.xhtml", page("目录", nav));
    zip.add("OEBPS/style.css",
            "body{font-family:serif;line-height:1.8}h1{font-size:1.35em}p{text-indent:2em;margin:0.6em "
            "0}img{max-width:100%;height:auto}.illustration{text-indent:0;text-align:center}");
    for (auto &[name, bytes] : pages) {
        check();
        zip.add("OEBPS/" + name, bytes);
    }
    for (auto &asset : assets) {
        check();
        zip.add("OEBPS/" + asset.name, asset.bytes);
    }
    check();
    auto bytes = zip.finish();
    check();
    atomic_write(path, bytes);
}

} // namespace sf
