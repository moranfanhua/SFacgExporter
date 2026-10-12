#include "SFECore/json.hpp"
#include <cctype>
#include <charconv>
#include <stdexcept>

namespace sf {
std::string utf8(char32_t c) {
    if (c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
        throw std::runtime_error("Invalid Unicode codepoint");
    std::string s;
    if (c <= 0x7f)
        s += char(c);
    else if (c <= 0x7ff) {
        s += char(0xc0 | (c >> 6));
        s += char(0x80 | (c & 63));
    } else if (c <= 0xffff && !(c >= 0xd800 && c <= 0xdfff)) {
        s += char(0xe0 | (c >> 12));
        s += char(0x80 | ((c >> 6) & 63));
        s += char(0x80 | (c & 63));
    } else if (c <= 0x10ffff) {
        s += char(0xf0 | (c >> 18));
        s += char(0x80 | ((c >> 12) & 63));
        s += char(0x80 | ((c >> 6) & 63));
        s += char(0x80 | (c & 63));
    } else
        throw std::runtime_error("Invalid Unicode codepoint");
    return s;
}

std::u32string codepoints(const std::string &s) {
    std::u32string out;
    for (size_t i = 0; i < s.size();) {
        unsigned char b = s[i++];
        char32_t c = b;
        int n = 0;
        char32_t min = 0;
        if (b >= 0xc2 && b <= 0xdf) {
            c = b & 31;
            n = 1;
            min = 0x80;
        } else if (b >= 0xe0 && b <= 0xef) {
            c = b & 15;
            n = 2;
            min = 0x800;
        } else if (b >= 0xf0 && b <= 0xf4) {
            c = b & 7;
            n = 3;
            min = 0x10000;
        } else if (b >= 0x80)
            throw std::runtime_error("Invalid UTF-8");
        while (n--) {
            if (i >= s.size() || (s[i] & 0xc0) != 0x80)
                throw std::runtime_error("Invalid UTF-8");
            c = (c << 6) | (s[i++] & 63);
        }
        if (c < min || c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff))
            throw std::runtime_error("Invalid UTF-8");
        out.push_back(c);
    }
    return out;
}

Json::Json() : value(nullptr) {}

Json::Json(std::nullptr_t) : value(nullptr) {}

Json::Json(bool v) : value(v) {}

Json::Json(int v) : value(int64_t(v)) {}

Json::Json(int64_t v) : value(v) {}

Json::Json(const char *v) : value(std::string(v)) {}

Json::Json(std::string v) : value(std::move(v)) {}

Json::Json(object v) : value(std::move(v)) {}

Json::Json(array v) : value(std::move(v)) {}

const Json &Json::operator[](const std::string &k) const {
    static Json nil;
    auto p = std::get_if<object>(&value);
    if (!p)
        return nil;
    auto i = p->find(k);
    return i == p->end() ? nil : i->second;
}

Json &Json::operator[](const std::string &k) {
    if (!std::holds_alternative<object>(value))
        value = object{};
    return std::get<object>(value)[k];
}

std::string Json::text(std::string fallback) const {
    auto p = std::get_if<std::string>(&value);
    return p ? *p : fallback;
}

int64_t Json::number(int64_t fallback) const {
    auto p = std::get_if<int64_t>(&value);
    return p ? *p : fallback;
}

bool Json::boolean(bool fallback) const {
    auto p = std::get_if<bool>(&value);
    return p ? *p : fallback;
}

bool Json::empty() const { return std::holds_alternative<std::nullptr_t>(value); }

const Json::array &Json::items() const {
    static array none;
    auto p = std::get_if<array>(&value);
    return p ? *p : none;
}

const Json::object &Json::fields() const {
    static object none;
    auto p = std::get_if<object>(&value);
    return p ? *p : none;
}

std::string Json::quote(const std::string &s) {
    std::string r = "\"";
    const char *hex = "0123456789abcdef";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') {
            r += '\\';
            r += char(c);
        } else if (c < 32) {
            r += "\\u00";
            r += hex[c >> 4];
            r += hex[c & 15];
        } else
            r += char(c);
    }
    return r + '"';
}

std::string Json::dump() const {
    if (auto p = std::get_if<std::string>(&value))
        return quote(*p);
    if (auto p = std::get_if<int64_t>(&value))
        return std::to_string(*p);
    if (auto p = std::get_if<bool>(&value))
        return *p ? "true" : "false";
    if (auto p = std::get_if<object>(&value)) {
        std::string r = "{";
        for (auto &[k, v] : *p) {
            if (r.size() > 1)
                r += ',';
            r += quote(k) + ":" + v.dump();
        }
        return r + '}';
    }
    if (auto p = std::get_if<array>(&value)) {
        std::string r = "[";
        for (auto &v : *p) {
            if (r.size() > 1)
                r += ',';
            r += v.dump();
        }
        return r + ']';
    }
    return "null";
}

Json Json::parse(const std::string &input) {
    struct Reader {
        const std::string &s;
        size_t i = 0;
        [[noreturn]] void fail() { throw std::runtime_error("Invalid JSON"); }
        void ws() {
            while (i < s.size() && std::isspace((unsigned char)s[i]))
                i++;
        }
        char take() {
            if (i == s.size())
                fail();
            return s[i++];
        }
        uint32_t hex() {
            uint32_t n = 0;
            for (int j = 0; j < 4; j++) {
                char c = take();
                n <<= 4;
                if (c >= '0' && c <= '9')
                    n += c - '0';
                else if (c >= 'a' && c <= 'f')
                    n += c - 'a' + 10;
                else if (c >= 'A' && c <= 'F')
                    n += c - 'A' + 10;
                else
                    fail();
            }
            return n;
        }
        std::string str() {
            if (take() != '"')
                fail();
            std::string r;
            for (;;) {
                char c = take();
                if (c == '"')
                    return r;
                if ((unsigned char)c < 32)
                    fail();
                if (c != '\\') {
                    r += c;
                    continue;
                }
                c = take();
                switch (c) {
                case '"':
                case '\\':
                case '/':
                    r += c;
                    break;
                case 'b':
                    r += '\b';
                    break;
                case 'f':
                    r += '\f';
                    break;
                case 'n':
                    r += '\n';
                    break;
                case 'r':
                    r += '\r';
                    break;
                case 't':
                    r += '\t';
                    break;
                case 'u': {
                    uint32_t n = hex();
                    if (n >= 0xd800 && n <= 0xdbff) {
                        if (take() != '\\' || take() != 'u')
                            fail();
                        auto low = hex();
                        if (low < 0xdc00 || low > 0xdfff)
                            fail();
                        n = 0x10000 + ((n - 0xd800) << 10) + low - 0xdc00;
                    } else if (n >= 0xdc00 && n <= 0xdfff)
                        fail();
                    r += utf8(n);
                    break;
                }
                default:
                    fail();
                }
            }
        }
        Json read(int depth = 0) {
            if (depth > 128)
                fail();
            ws();
            if (i == s.size())
                fail();
            char c = s[i];
            if (c == '"')
                return str();
            if (c == '{') {
                i++;
                object o;
                ws();
                if (i < s.size() && s[i] == '}') {
                    i++;
                    return o;
                }
                for (;;) {
                    ws();
                    auto k = str();
                    ws();
                    if (take() != ':')
                        fail();
                    auto v = read(depth + 1);
                    if (!o.emplace(k, std::move(v)).second)
                        fail();
                    ws();
                    c = take();
                    if (c == '}')
                        return o;
                    if (c != ',')
                        fail();
                }
            }
            if (c == '[') {
                i++;
                array a;
                ws();
                if (i < s.size() && s[i] == ']') {
                    i++;
                    return a;
                }
                for (;;) {
                    a.push_back(read(depth + 1));
                    ws();
                    c = take();
                    if (c == ']')
                        return a;
                    if (c != ',')
                        fail();
                }
            }
            for (auto [lit, val] : {std::pair{"true", Json(true)}, std::pair{"false", Json(false)},
                                    std::pair{"null", Json()}}) {
                size_t n = std::char_traits<char>::length(lit);
                if (s.compare(i, n, lit) == 0) {
                    i += n;
                    return val;
                }
            }
            size_t start = i;
            if (c == '-')
                i++;
            if (i == s.size() || !std::isdigit((unsigned char)s[i]))
                fail();
            if (s[i] == '0')
                i++;
            else
                while (i < s.size() && std::isdigit((unsigned char)s[i]))
                    i++;
            // API prices may be decimals; IDs and counters remain integers.
            bool decimal = false;
            if (i < s.size() && s[i] == '.') {
                decimal = true;
                i++;
                if (i == s.size() || !std::isdigit((unsigned char)s[i]))
                    fail();
                while (i < s.size() && std::isdigit((unsigned char)s[i]))
                    i++;
            }
            if (i < s.size() && (s[i] == 'e' || s[i] == 'E')) {
                decimal = true;
                i++;
                if (i < s.size() && (s[i] == '+' || s[i] == '-'))
                    i++;
                if (i == s.size() || !std::isdigit((unsigned char)s[i]))
                    fail();
                while (i < s.size() && std::isdigit((unsigned char)s[i]))
                    i++;
            }
            if (decimal)
                return int64_t(std::stod(s.substr(start, i - start)));
            int64_t n = 0;
            auto result = std::from_chars(s.data() + start, s.data() + i, n);
            if (result.ec != std::errc())
                fail();
            return n;
        }
    } r{input};
    if (input.size() >= 3 && input.substr(0, 3) == "\xef\xbb\xbf")
        r.i = 3;
    auto j = r.read();
    r.ws();
    if (r.i != input.size())
        r.fail();
    return j;
}

Json obj(std::initializer_list<std::pair<const std::string, Json>> v) { return Json::object(v); }

} // namespace sf
