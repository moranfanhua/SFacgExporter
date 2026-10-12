#include "SFECore/codec.hpp"
#include <regex>
#include <set>

namespace sf {
std::vector<ImageTag> images(const std::string &s) {
    static const std::regex re(
        R"(\[img\s*=\s*\d+\s*,\s*\d+\s*\]\s*([^\[\]\r\n]+?)\s*\[/img\s*\]|\[img\s*\]\s*([^\[\]\r\n]+?)\s*\[/img\s*\]|\[img\s*=\s*([^\]\r\n]+)\](?:\s*\[/img\s*\])?)",
        std::regex::icase);
    std::vector<ImageTag> out;
    for (auto it = std::sregex_iterator(s.begin(), s.end(), re); it != std::sregex_iterator(); ++it) {
        auto &m = *it;
        auto url = m[1].matched ? m[1].str() : m[2].matched ? m[2].str() : m[3].str();
        out.push_back({size_t(m.position()), size_t(m.position() + m.length()), trim(url)});
    }
    return out;
}

std::string transform_text(const std::string &s, const std::function<std::string(const std::string &)> &f,
                           bool preserve) {
    std::string out;
    size_t start = 0;
    for (auto &t : images(s)) {
        out += f(s.substr(start, t.start - start));
        if (preserve)
            out += s.substr(t.start, t.end - t.start);
        start = t.end;
    }
    return out + f(s.substr(start));
}

Codec::Codec(const fs::path &table_path) {
    if (table_path.empty())
        throw Error("核心配置缺少字符表路径");
    Json table;
    try {
        table = Json::parse(read_file(table_path, 256 * 1024));
    } catch (const std::exception &e) {
        throw Error("无法加载字符表：" + narrow(table_path.wstring()) + "；" + e.what());
    }
    if (!std::holds_alternative<Json::object>(table.value) || table.fields().size() != 3751)
        throw Error("字符表格式无效：需要完整的 3751 项 JSON 字符映射");
    std::set<char32_t> values;
    for (const auto &[from, to] : table.fields()) {
        auto a = codepoints(from), b = codepoints(to.text());
        if (a.size() != 1 || b.size() != 1 || a[0] < 0x4e00 || a[0] > 0x9fff || b[0] < 0x4e00 ||
            b[0] > 0x9fff || !values.insert(b[0]).second)
            throw Error("字符表格式无效：每项必须为单个汉字且目标字符不能重复");
        alphabet.emplace(a[0], b[0]);
    }
    for (const auto &[from, to] : alphabet)
        if (!values.contains(from))
            throw Error("字符表不完整：来源与目标字符集合不一致");
}

std::string Codec::decode(const std::string &s) const {
    return transform_text(s, [&](auto text) {
        std::string out;
        for (auto c : codepoints(text)) {
            auto i = alphabet.find(c);
            out += utf8(i == alphabet.end() ? c : i->second);
        }
        return out;
    });
}

std::u32string Codec::han(const std::string &s) {
    auto text = transform_text(s, [](auto t) { return t; }, false);
    std::u32string out;
    for (auto c : codepoints(text))
        if (c >= 0x4e00 && c <= 0x9fff)
            out += c;
    return out;
}

void Codec::verify(const std::string &raw, const std::string &clear) const {
    auto a = han(raw), b = han(clear);
    if (a.empty() || a.size() != b.size())
        throw Error("核对来源长度不一致", -2);
    if (han(decode(raw)) != b)
        throw Error("正文与协议解码结果不一致，已停止导出", -3);
}

std::string entities(const std::string &s) {
    static std::map<std::string, std::string> named = {
        {"amp", "&"},   {"lt", "<"},    {"gt", ">"},    {"quot", "\""},  {"apos", "'"},
        {"nbsp", " "},  {"mdash", "—"}, {"ndash", "–"}, {"hellip", "…"}, {"ldquo", "“"},
        {"rdquo", "”"}, {"lsquo", "‘"}, {"rsquo", "’"}};
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '&') {
            auto end = s.find(';', i + 1);
            if (end != std::string::npos && end - i < 20) {
                auto key = s.substr(i + 1, end - i - 1);
                auto it = named.find(key);
                if (it != named.end()) {
                    out += it->second;
                    i = end;
                    continue;
                }
                if (!key.empty() && key[0] == '#') {
                    try {
                        bool hex = key.size() > 1 && (key[1] == 'x' || key[1] == 'X');
                        size_t consumed = 0;
                        auto text = key.substr(hex ? 2 : 1);
                        auto c = std::stoul(text, &consumed, hex ? 16 : 10);
                        if (consumed == text.size()) {
                            out += utf8(c);
                            i = end;
                            continue;
                        }
                    } catch (...) {
                    }
                }
            }
        }
        out += s[i];
    }
    return out;
}

std::map<std::string, std::string> attributes(const std::string &s) {
    static const std::regex re(R"ATTR(([\w:-]+)\s*=\s*(["'])(.*?)\2)ATTR");
    std::map<std::string, std::string> out;
    for (auto i = std::sregex_iterator(s.begin(), s.end(), re); i != std::sregex_iterator(); ++i)
        out[lower((*i)[1])] = entities((*i)[3]);
    return out;
}

std::string html_body(const std::string &html) {
    std::string out;
    bool active = false, found = false;
    int depth = 0;
    size_t p = 0;
    for (;;) {
        auto open = html.find('<', p);
        if (open == std::string::npos)
            break;
        if (active)
            out += entities(html.substr(p, open - p));
        if (html.compare(open, 4, "<!--") == 0) {
            auto close = html.find("-->", open + 4);
            if (close == std::string::npos)
                break;
            p = close + 3;
            continue;
        }
        auto close = html.find('>', open);
        if (close == std::string::npos)
            break;
        auto token = trim(html.substr(open + 1, close - open - 1));
        bool ending = !token.empty() && token[0] == '/';
        auto name = lower(token.substr(ending ? 1 : 0));
        name = name.substr(0, name.find_first_of(" \t\r\n/"));
        auto attrs = attributes(token);
        if (attrs["id"] == "vipImage" || attrs["class"].find("pay-section") != std::string::npos)
            throw Error("网页仅提供付费预览或图片正文", 403);
        if (name == "script" || name == "style") {
            auto end = lower(html).find("</" + name, close + 1);
            if (end == std::string::npos)
                break;
            p = html.find('>', end) + 1;
            continue;
        }
        bool isvoid = name == "br" || name == "img" || name == "input" || name == "meta" || name == "link" ||
                      name == "hr" || (!token.empty() && token.back() == '/');
        if (!active && attrs["id"] == "ChapterBody") {
            active = true;
            found = true;
            depth = 1;
        } else if (active) {
            if (ending) {
                if (--depth == 0)
                    break;
                if (name == "p" || name == "div")
                    out += '\n';
            } else {
                if (!isvoid)
                    depth++;
                if (name == "p" || name == "div" || name == "br")
                    out += '\n';
                if (name == "img") {
                    auto src = attrs.count("data-original") ? attrs["data-original"] : attrs["src"];
                    out += "\n[img=" + src + "]\n";
                }
            }
        }
        p = close + 1;
    }
    if (!found || trim(out).empty())
        throw Error("没有找到完整网页正文", 0, true);
    return trim(out);
}

} // namespace sf
