#include "SFECore.h"
#include "SFECore/ebook.hpp"
#include "SFECore/transport.hpp"
#include <algorithm>
#include <chrono>
#include <regex>
#include <set>
#include <sstream>

namespace sf {
class Core {
    static constexpr auto device = "910D166A-736E-3231-8B21-8D12DFD75F16";
    fs::path session_file, cache_root, table_path;
    std::map<std::string, std::string> cookies;
    std::string nonce = guid(), persisted, account, password;
    int64_t clock_offset = 0;
    sf_event_callback callback = nullptr;
    void *user = nullptr;

  public:
    Transport network;
    Codec codec;
    bool logged = false;
    Core(const Json &config, sf_event_callback cb = nullptr, void *u = nullptr)
        : session_file(wide(config["sessionFile"].text())), cache_root(wide(config["cacheRoot"].text())),
          table_path(wide(config["cipherTablePath"].text())), callback(cb), user(u), codec(table_path) {
        if (session_file.empty() || cache_root.empty())
            throw Error("核心配置缺少会话或缓存路径");
    }
    void emit(const std::string &type, const std::string &text, int done = 0, int total = 0) {
        if (callback) {
            auto event = obj({{"type", type}, {"text", safe(text)}, {"done", done}, {"total", total}}).dump();
            callback(event.c_str(), user);
        }
    }
    std::string safe(std::string text) {
        for (auto &[_, secret] : cookies)
            if (!secret.empty())
                replace_all(text, secret, "[已隐藏]");
        if (!account.empty())
            replace_all(text, account, "[已隐藏]");
        if (!password.empty())
            replace_all(text, password, "[已隐藏]");
        text = std::regex_replace(text, std::regex(R"(1[3-9][0-9]{9})"), "[手机号已隐藏]");
        text = std::regex_replace(text, std::regex(R"([\w.+-]+@[\w.-]+\.[A-Za-z]{2,})"), "[邮箱已隐藏]");
        replace_all(text, "\r", " ");
        replace_all(text, "\n", " ");
        return text;
    }
    bool has_session() const {
        std::error_code ec;
        return fs::exists(session_file, ec);
    }
    void require_cookies() {
        if (cookies[".SFCommunity"].empty() || cookies["session_APP"].empty())
            throw Error("App 会话不完整，.SFCommunity 和 session_APP 缺一不可");
    }
    void logout() {
        logged = false;
        cookies.clear();
        persisted.clear();
        account.clear();
        password.clear();
        std::error_code ec;
        fs::remove(session_file, ec);
        if (ec && ec != std::errc::no_such_file_or_directory)
            throw Error("无法清除本机登录会话");
    }
    void persist() {
        if (!logged)
            return;
        try {
            require_cookies();
            Json c = Json::object{};
            for (auto &[k, v] : cookies)
                c[k] = v;
            auto plain = obj({{"Version", 1}, {"Cookies", c}, {"Nonce", nonce}}).dump();
            if (plain == persisted)
                return;
            auto encrypted = protected_data(plain, false);
            atomic_write(session_file, encrypted);
            persisted = plain;
        } catch (const std::exception &) {
            emit("log", "本次登录有效，但会话保存失败；下次启动需要重新登录");
        }
    }
    template <class F>
    auto retries(const std::string &target, F fetch, bool signed_request = false) -> decltype(fetch()) {
        for (int retry = 0;; retry++) {
            network.check();
            try {
                return fetch();
            } catch (const Error &e) {
                if (e.cancelled || !e.retry)
                    throw;
                if (retry == 3) {
                    emit("log", target + " 重试 3 次后仍失败：" + e.what());
                    throw;
                }
                if (signed_request && e.code == 417)
                    nonce = guid();
                emit("log", target + " 失败，正在重试 " + std::to_string(retry + 1) + "/3：" + e.what());
                network.pause();
            }
        }
    }
    void capture(const Response &r) {
        auto i = r.headers.find("set-cookie");
        if (i == r.headers.end())
            return;
        for (auto &s : i->second) {
            auto first = s.substr(0, s.find(';'));
            auto eq = first.find('=');
            if (eq == std::string::npos)
                continue;
            auto name = trim(first.substr(0, eq)), value = trim(first.substr(eq + 1));
            if (name != ".SFCommunity" && name != "session_APP")
                continue;
            if (value.empty() || lower(s).find("max-age=0") != std::string::npos)
                cookies.erase(name);
            else
                cookies[name] = value;
        }
    }
    Json api(const std::string &path, const Json &body = Json()) {
        return retries(
            "App /" + path.substr(0, path.find('?')),
            [&] {
                std::map<std::string, std::string> headers = {
                    {"Authorization", "Basic YW5kcm9pZHVzZXI6MWEjJDUxLXl0Njk7KkFjdkBxeHE="},
                    {"Accept", "application/vnd.sfacg.api+json;version=1"},
                    {"Accept-Charset", "UTF-8"},
                    {"User-Agent", "boluobao/5.2.16(android;35)/OPPO/" + lower(device) + "/OPPO"}};
                auto timestamp = now() + clock_offset;
                headers["sfsecurity"] = "nonce=" + nonce + "&timestamp=" + std::to_string(timestamp) +
                                        "&devicetoken=" + device + "&sign=" + sign(nonce, timestamp, device);
                std::string cookie;
                for (auto name : {".SFCommunity", "session_APP"})
                    if (cookies.count(name) && !cookies[name].empty()) {
                        if (!cookie.empty())
                            cookie += "; ";
                        cookie += std::string(name) + "=" + cookies[name];
                    }
                if (!cookie.empty())
                    headers["Cookie"] = cookie;
                if (!body.empty())
                    headers["Content-Type"] = "application/json; charset=utf-8";
                auto response =
                    network.send("https://api.sfacg.com/" + path, body.empty() ? "GET" : "POST", headers,
                                 body.empty() ? "" : body.dump(), 25 * 1024 * 1024, true);
                capture(response);
                Json doc;
                try {
                    doc = Json::parse(response.body);
                } catch (const std::exception &) {
                    throw Error("接口没有返回可识别的数据（HTTP " + std::to_string(response.status) + "）",
                                response.status, true);
                }
                int code = int(doc["status"]["httpCode"].number());
                int error = int(doc["status"]["errorCode"].number());
                if (code == 200 && response.status >= 200 && response.status < 300) {
                    if (path.starts_with("Chaps/"))
                        chapter_content(doc);
                    if (logged)
                        persist();
                    return doc;
                }
                if (!code)
                    throw Error("接口未返回有效状态", response.status, true);
                if (code == 200)
                    code = response.status;
                if (code == 417) {
                    auto date = response.header("date");
                    SYSTEMTIME st{};
                    if (!date.empty() && WinHttpTimeToSystemTime(wide(date).c_str(), &st)) {
                        FILETIME ft{};
                        SystemTimeToFileTime(&st, &ft);
                        ULARGE_INTEGER n;
                        n.LowPart = ft.dwLowDateTime;
                        n.HighPart = ft.dwHighDateTime;
                        int64_t offset = int64_t(n.QuadPart / 10000 - 11644473600000ULL) - now();
                        if (std::abs(offset) > 2000)
                            clock_offset = offset;
                    }
                }
                bool retry = code == 408 || code == 417 || code == 429 || code >= 500;
                auto message = "接口 /" + path.substr(0, path.find('?')) + " 拒绝请求（HTTP " +
                               std::to_string(response.status) + "，状态 " + std::to_string(code) +
                               "，错误码 " + std::to_string(error) + "）";
                auto msg = safe(doc["status"]["msg"].text());
                if (!msg.empty())
                    message += "：" + msg;
                throw Error(message, code, retry);
            },
            true);
    }
    static std::string chapter_content(const Json &doc) {
        auto data = doc["data"];
        auto content = data["content"].text() + data["expand"]["content"].text();
        if (trim(content).empty())
            throw Error("接口没有返回章节正文", 0, true);
        return content;
    }
    void validate() {
        require_cookies();
        try {
            api("user");
            logged = true;
            persist();
        } catch (const Error &e) {
            if (e.code == 401) {
                logout();
                emit("log", "/user 已确认会话失效，请重新登录");
            }
            throw;
        }
    }
    bool restore() {
        if (!has_session())
            return false;
        Json saved;
        try {
            auto plain = protected_data(read_file(session_file, 64 * 1024), true);
            saved = Json::parse(plain);
            SecureZeroMemory(plain.data(), plain.size());
        } catch (const std::exception &) {
            throw Error("保存的登录会话无法读取，请重新登录");
        }
        if (saved["Version"].number() != 1 || saved["Nonce"].text().size() != 36)
            throw Error("保存的登录会话格式无效，请重新登录");
        logged = false;
        cookies.clear();
        for (auto name : {".SFCommunity", "session_APP"})
            cookies[name] = saved["Cookies"][name].text();
        require_cookies();
        nonce = saved["Nonce"].text();
        persisted = saved.dump();
        validate();
        return true;
    }
    void login(std::string a, std::string p) {
        if (trim(a).empty() || p.empty())
            throw Error("请填写账号和密码");
        logout();
        account = std::move(a);
        password = std::move(p);
        try {
            api("sessions", obj({{"username", account}, {"password", password}, {"shuMeiId", ""}}));
            validate();
            account.clear();
            password.clear();
        } catch (...) {
            account.clear();
            password.clear();
            throw;
        }
    }
    void use_cookie(const std::string &text) {
        if (text.find_first_of("\r\n") != std::string::npos)
            throw Error("Cookie 必须是单行");
        std::map<std::string, std::string> supplied;
        std::istringstream parts(text);
        std::string part;
        while (std::getline(parts, part, ';')) {
            auto eq = part.find('=');
            if (eq == std::string::npos)
                continue;
            auto name = trim(part.substr(0, eq));
            if (name == ".SFCommunity" || name == "session_APP")
                supplied[name] = trim(part.substr(eq + 1));
        }
        if (supplied[".SFCommunity"].empty() || supplied["session_APP"].empty())
            throw Error("请提供完整的 App Cookie");
        logout();
        cookies = std::move(supplied);
        validate();
    }
    std::string web_once(const std::string &url, bool image = false) {
        auto r = network.send(url, "GET", {{"User-Agent", "Mozilla/5.0 (Windows NT 10.0; Win64; x64)"}});
        if (r.status < 200 || r.status >= 300)
            throw Error("网页请求失败（HTTP " + std::to_string(r.status) + "）", r.status,
                        r.status == 408 || r.status == 417 || r.status == 429 || r.status >= 500);
        if (image && !lower(r.header("content-type")).starts_with("image/"))
            throw Error("响应不是图片");
        return r.body;
    }
    std::string web(const std::string &url, bool image = false) {
        return retries(image ? "图片下载" : "网页下载", [&] { return web_once(url, image); });
    }
    static std::string book_id(std::string s) {
        s = trim(s);
        if (std::regex_match(s, std::regex("[0-9]{1,10}")))
            return s;
        std::smatch m;
        if (std::regex_match(s, m,
                             std::regex(R"(https?://(?:book|m)\.sfacg\.com/(?:Novel|b|i)/([0-9]{1,10})/?.*)",
                                        std::regex::icase)))
            return m[1];
        throw Error("请输入数字小说 ID");
    }
    fs::path book_dir(const std::string &id) { return cache_root / L".sfcore-cache" / wide(id); }
    Json cached_json(const fs::path &p) {
        try {
            if (fs::exists(p))
                return read_json(p);
        } catch (const std::exception &) {
        }
        return Json();
    }
    std::string cached(const std::string &id, int64_t chapter, bool raw) {
        auto p = book_dir(id);
        if (raw)
            p /= L"raw";
        auto j = cached_json(p / wide(std::to_string(chapter) + ".json"));
        if (j["BookId"].text() != id || j["Id"].number() != chapter ||
            j[raw ? "ProtocolVersion" : "DecoderVersion"].number() != 2)
            return {};
        return j["Content"].text();
    }
    void save_chapter(const std::string &id, int64_t chapter, const std::string &content, bool raw) {
        auto p = book_dir(id);
        if (raw)
            p /= L"raw";
        save_json(p / wide(std::to_string(chapter) + ".json"),
                  obj({{"BookId", id},
                       {"Id", chapter},
                       {raw ? "ProtocolVersion" : "DecoderVersion", 2},
                       {"Content", content}}));
    }
    Json catalog(const std::string &id, bool refresh) {
        auto cached_book = cached_json(book_dir(id) / L"novel.json");
        if (!refresh && cached_book["Id"].text() == id && !cached_book["Chapters"].items().empty()) {
            std::set<int64_t> ids;
            bool valid = true;
            for (auto &c : cached_book["Chapters"].items())
                if (c["Id"].number() <= 0 || !ids.insert(c["Id"].number()).second)
                    valid = false;
            if (valid)
                return cached_book;
        }
        emit("status", "正在读取目录……");
        auto info = api("novels/" + id + "?expand=bigNovelCover")["data"];
        auto dirs = api("novels/" + id + "/dirs?expand=originNeedFireMoney")["data"];
        auto html = web("https://book.sfacg.com/Novel/" + id + "/MainIndex/");
        std::map<int64_t, std::string> paths;
        std::regex anchors(R"ANCHOR(<a\b[^>]*>)ANCHOR", std::regex::icase),
            chapter(R"((?:/Novel/[0-9]+/[0-9]+/|/vip/c/)([0-9]+)/?$)", std::regex::icase);
        for (auto it = std::sregex_iterator(html.begin(), html.end(), anchors); it != std::sregex_iterator();
             ++it) {
            auto href = attributes(it->str())["href"];
            std::smatch m;
            if (std::regex_search(href, m, chapter)) {
                auto chapter_id = std::stoll(m[1].str());
                if (href.starts_with("/"))
                    href = "https://book.sfacg.com" + href;
                paths[chapter_id] = href;
            }
        }
        Json::array chapters;
        std::set<int64_t> ids;
        for (auto &volume : dirs["volumeList"].items())
            for (auto &c : volume["chapterList"].items()) {
                auto cid = c["chapId"].number();
                if (cid <= 0 || !ids.insert(cid).second)
                    throw Error("目录章节 ID 无效或重复");
                auto path = paths[cid];
                bool vip = path.find("/vip/c/") != std::string::npos ||
                           (path.empty() && c["originNeedFireMoney"].number() > 0);
                chapters.push_back(obj({{"Id", cid},
                                        {"Title", c["title"].text(std::to_string(cid))},
                                        {"Volume", volume["title"].text("正文")},
                                        {"Vip", vip},
                                        {"WebUrl", path}}));
            }
        if (chapters.empty())
            throw Error("没有取得章节目录");
        auto book = obj({{"Id", id},
                         {"Title", info["novelName"].text(id)},
                         {"Author", info["authorName"].text()},
                         {"Cover", info["expand"]["bigNovelCover"].text()},
                         {"Chapters", chapters}});
        save_json(book_dir(id) / L"novel.json", book);
        return book;
    }
    std::string raw_chapter(int64_t id) {
        try {
            return chapter_content(
                api("Chaps/" + std::to_string(id) + "?expand=content%2Cexpand.content&autoOrder=false"));
        } catch (const Error &e) {
            if (e.code == 401 && logged) {
                emit("log", "章节接口返回 401，正在复核登录会话");
                try {
                    validate();
                } catch (const Error &failure) {
                    if (failure.cancelled)
                        throw;
                    if (failure.code == 401)
                        throw Error("App 会话已失效，请重新登录", 401);
                    throw Error("登录状态暂时无法复核", -4);
                }
                throw Error("登录仍有效，但正文接口拒绝访问；请检查权限或协议兼容性", -4);
            }
            throw;
        }
    }
    std::string raw(const std::string &book, const Json &ch, bool refresh = false) {
        auto id = ch["Id"].number();
        auto text = refresh ? "" : cached(book, id, true);
        if (text.empty()) {
            text = raw_chapter(id);
            save_chapter(book, id, text, true);
        }
        return text;
    }
    std::string clear(const std::string &book, const Json &ch) {
        auto id = ch["Id"].number();
        auto text = cached(book, id, false);
        if (!text.empty())
            return text;
        if (ch["Vip"].boolean())
            text = codec.decode(raw(book, ch));
        else {
            auto url = ch["WebUrl"].text();
            if (url.empty())
                throw Error("免费章节缺少官网地址");
            text = retries("免费章节 /" + std::to_string(id), [&] { return html_body(web_once(url)); });
        }
        if (trim(text).empty())
            throw Error("没有取得完整正文");
        save_chapter(book, id, text, false);
        return text;
    }
    Json export_book(const Json &request) {
        auto id = book_id(request["id"].text());
        if (!request["cacheRoot"].text().empty())
            cache_root = wide(request["cacheRoot"].text());
        auto book = catalog(id, false);
        if (!request["txt"].boolean() && !request["epub"].boolean())
            throw Error("至少选择 TXT 或 EPUB 一种格式");
        std::set<std::string> chosen;
        for (auto &v : request["volumes"].items())
            chosen.insert(v.text());
        std::vector<Json> selected;
        for (auto &c : book["Chapters"].items())
            if (chosen.count(c["Volume"].text()))
                selected.push_back(c);
        if (selected.empty())
            throw Error("请至少选择一卷");
        bool cache_only = true, need_login = false, has_vip = false;
        for (auto &c : selected) {
            bool cl = !cached(id, c["Id"].number(), false).empty(),
                 rw = !cached(id, c["Id"].number(), true).empty();
            has_vip |= c["Vip"].boolean();
            cache_only &= cl || (c["Vip"].boolean() && rw);
            need_login |= c["Vip"].boolean() && !cl && !rw;
        }
        if (need_login && !logged) {
            if (has_session())
                restore();
            if (!logged)
                throw Error("请先登录自己的 App 账号");
        }
        if (has_vip) {
            std::vector<Json> free;
            for (auto &c : book["Chapters"].items())
                if (!c["Vip"].boolean() && !c["WebUrl"].text().empty())
                    free.push_back(c);
            if (free.size() < 3)
                throw Error("可用于核对协议的免费章节不足 3 章");
            std::set<size_t> indices = {0, 1, 2, free.size() / 2};
            for (auto index : indices) {
                network.check();
                emit("status", "正在核对解码规则……");
                auto r = raw(id, free[index], !cache_only);
                auto cl = clear(id, free[index]);
                codec.verify(r, cl);
                if (!cache_only)
                    network.pause();
            }
        }
        std::vector<SavedChapter> saved;
        int failed = 0, done = 0;
        for (auto &ch : selected) {
            network.check();
            emit("progress", ch["Title"].text(), done, int(selected.size()));
            bool had_cache =
                !cached(id, ch["Id"].number(), false).empty() || !cached(id, ch["Id"].number(), true).empty();
            try {
                saved.push_back({ch, clear(id, ch)});
                if (!had_cache)
                    network.pause();
            } catch (const Error &e) {
                if (e.cancelled || e.code == 401 || e.code == 429 || e.code == -3 || e.code == -4)
                    throw;
                failed++;
                emit("log", "未取得：" + ch["Title"].text() + "（ID " + std::to_string(ch["Id"].number()) +
                                "） — " + e.what());
            }
            emit("progress", ch["Title"].text(), ++done, int(selected.size()));
        }
        if (saved.empty())
            throw Error("没有取得完整可解码的章节，未生成空电子书");
        auto name = safe_name(book["Title"].text());
        fs::path out = request["outputFolder"].text().empty()
                           ? cache_root / wide(name + "_" + std::to_string(now()) + "_" + guid().substr(0, 4))
                           : fs::path(wide(request["outputFolder"].text()));
        auto suffix = failed ? "_不完整" : selected.size() < book["Chapters"].items().size() ? "_节选" : "";
        auto stem = out / wide(name + suffix);
        auto check = [&] { network.check(); };
        if (request["txt"].boolean()) {
            auto file = stem;
            file += L".txt";
            write_txt(book, saved, file, check);
        }
        if (request["epub"].boolean()) {
            emit("status", "正在打包 EPUB 和插图……");
            auto file = stem;
            file += L".epub";
            write_epub(
                book, saved, file, [&](auto url) { return web(url, true); },
                [&](auto line) { emit("log", line); }, check);
        }
        return obj({{"folder", narrow(out.wstring())},
                    {"exported", int(saved.size())},
                    {"requested", int(selected.size())},
                    {"failed", failed}});
    }
    Json dispatch(const Json &r) {
        auto action = r["action"].text();
        network.check();
        if (action == "status")
            return obj({{"hasSavedSession", has_session()}});
        if (action == "restore")
            return obj({{"restored", restore()}});
        if (action == "login") {
            login(r["account"].text(), r["password"].text());
            return Json();
        }
        if (action == "cookie") {
            use_cookie(r["cookie"].text());
            return Json();
        }
        if (action == "logout") {
            logout();
            return Json();
        }
        if (action == "catalog") {
            if (!r["cacheRoot"].text().empty())
                cache_root = wide(r["cacheRoot"].text());
            return catalog(book_id(r["id"].text()), r["refresh"].boolean());
        }
        if (action == "export")
            return export_book(r);
        if (action == "decode")
            return codec.decode(r["text"].text());
        if (action == "verify") {
            codec.verify(r["raw"].text(), r["clear"].text());
            return Json();
        }
        if (action == "sign")
            return sign(r["nonce"].text(), r["timestamp"].number(), r["device"].text());
        if (action == "self_test") {
            self_test(r["root"].text(), table_path);
            return Json();
        }
        throw Error("未知核心操作");
    }
    static void self_test(const std::string &root, const fs::path &table_path);
};
inline void assert_test(bool ok, const char *message) {
    if (!ok)
        throw Error(std::string("Native test failed: ") + message);
}
void Core::self_test(const std::string &root, const fs::path &table_path) {
    assert_test(Json::parse(R"({"s":"\uD83D\uDE00\u5C9A"})")["s"].text() == "😀岚", "Unicode JSON");
    assert_test(sign("8BB4D95F-7BE3-40D4-BC98-E4DF0B5D4568", 1791777000123, device) ==
                    "C60655DD55198E0A9F176799E3C8206A",
                "signature parity");
    Codec codec(table_path);
    const auto table = read_json(table_path);
    const auto &[from, to] = *table.fields().begin();
    auto a = codepoints(from)[0], b = codepoints(to.text())[0];
    std::string tag = "[img=700,875]https://rss.sfacg.com/" + utf8(a) + ".jpg[/img]";
    assert_test(codec.decode(utf8(a) + "岚曜砥😀𠮷" + tag) == utf8(b) + "岚曜砥😀𠮷" + tag,
                "Unicode and full image tag preservation");
    assert_test(images("[IMG=1200,1200]\nhttps://rss.sfacg.com/a.jpg\n[/IMG]")[0].url ==
                    "https://rss.sfacg.com/a.jpg",
                "dimension tag URL");
    assert_test(html_body("<script>foo</script><div id='ChapterBody'><p>原创 &amp; 😀</p><img "
                          "src='https://rss.sfacg.com/a.jpg'/><p>后文</p></div><footer>不应提取</footer>") ==
                    "原创 & 😀\n\n[img=https://rss.sfacg.com/a.jpg]\n\n后文",
                "HTML body only");
    auto config = [&](const std::string &session, const std::string &cache) {
        return obj({{"sessionFile", session},
                    {"cacheRoot", cache},
                    {"cipherTablePath", narrow(table_path.wstring())}});
    };
    Core c(config(root + "/missing/session.dat", root));
    c.logout();
    assert_test(!c.has_session(), "logout without session directory");
    int attempts = 0;
    std::set<std::string> nonces;
    c.network.fixture = [&](auto url, auto, const auto &headers, auto) {
        auto security = headers.at("sfsecurity");
        nonces.insert(security.substr(6, 36));
        Response response;
        if (url.find("sessions") != std::string::npos) {
            response.body = R"({"status":{"httpCode":200},"data":{}})";
            response.headers["set-cookie"] = {".SFCommunity=synthetic-community; Path=/user",
                                              "session_APP=synthetic-session; Path=/sessions"};
        } else if (url.ends_with("/user")) {
            assert_test(headers.at("Cookie") ==
                            ".SFCommunity=synthetic-community; session_APP=synthetic-session",
                        "manual cookie scope");
            response.body = R"({"status":{"httpCode":200},"data":{"userId":1}})";
        } else {
            attempts++;
            response.status = attempts < 4 ? 417 : 200;
            response.body = attempts < 4
                                ? R"({"status":{"httpCode":417,"errorCode":782,"msg":"接口校验失败"}})"
                                : R"({"status":{"httpCode":200},"data":{"content":"原创正文"}})";
        }
        return response;
    };
    c.login("synthetic-account", "synthetic-password");
    assert_test(c.logged && c.has_session(), "first login creates encrypted session");
    auto encrypted = read_file(wide(root + "/missing/session.dat"));
    assert_test(encrypted.find("synthetic-session") == std::string::npos &&
                    encrypted.find("synthetic-password") == std::string::npos,
                "session ciphertext");
    assert_test(c.raw_chapter(1) == "原创正文" && attempts == 4 && nonces.size() >= 4,
                "three additional retries");
    Core restarted(config(root + "/missing/session.dat", root));
    restarted.network.fixture = c.network.fixture;
    assert_test(restarted.restore() && restarted.logged, "session compatibility across native instances");
    restarted.network.fixture = [](auto, auto, const auto &, auto) {
        Response r;
        r.status = 401;
        r.body = R"({"status":{"httpCode":401},"data":null})";
        return r;
    };
    try {
        restarted.validate();
        assert_test(false, "expiry should fail");
    } catch (const Error &e) {
        assert_test(e.code == 401 && !restarted.logged && !restarted.has_session(),
                    "expired session removed");
    }
    attempts = 0;
    c.network.fixture = [&](auto, auto, const auto &, auto) -> Response {
        attempts++;
        throw Error("synthetic timeout", 0, true);
    };
    try {
        c.raw_chapter(2);
        assert_test(false, "retry failure should throw");
    } catch (const Error &) {
        assert_test(attempts == 4, "retry upper bound");
    }
    c.network.cancel();
    try {
        c.raw_chapter(2);
        assert_test(false, "cancel should throw");
    } catch (const Error &e) {
        assert_test(e.cancelled && attempts == 4, "cancel stops requests");
    }
    for (int i = 0; i < 100; i++) {
        int d = random_delay();
        assert_test(d >= 50 && d <= 150, "random delay bounds");
    }
    Core free_client(config(root + "/free-session.dat", root));
    int free_attempts = 0;
    free_client.network.fixture = [&](auto, auto, const auto &, auto) {
        free_attempts++;
        Response r;
        r.status = 503;
        return r;
    };
    try {
        free_client.clear(
            "free-test", obj({{"Id", 1}, {"Vip", false}, {"WebUrl", "https://book.sfacg.com/Novel/1/1/1/"}}));
        assert_test(false, "web failure should throw");
    } catch (const Error &) {
        assert_test(free_attempts == 4, "free chapters must not nest retry loops");
    }
    const unsigned char gif[] = {71, 73,  70,  56,  57, 97,  1, 0, 1, 0,  240, 0, 0,  0, 255,
                                 0,  255, 255, 255, 33, 249, 4, 1, 0, 0,  0,   0, 44, 0, 0,
                                 0,  0,   1,   0,   1,  0,   0, 2, 2, 68, 1,   0, 59};
    std::string bytes((const char *)gif, sizeof(gif));
    auto book = obj({{"Id", "1"},
                     {"Title", "原创插图 & 😀"},
                     {"Author", "测试"},
                     {"Cover", "https://rss.sfacg.com/cover.gif"}});
    auto info = obj({{"Title", "插图章节 <测试>"}, {"Volume", "插图卷"}});
    std::vector<SavedChapter> chapters = {
        {info, "前文 & < >\n[img=700,875]https://rss.sfacg.com/body.gif[/img]\n后文😀"}};
    std::vector<std::string> urls;
    auto out = fs::path(wide(root)) / L"image-books";
    write_txt(book, chapters, out / L"images.txt", [] {});
    write_epub(
        book, chapters, out / L"images.epub",
        [&](auto url) {
            urls.push_back(url);
            return bytes;
        },
        [](auto) { assert_test(false, "image failure unexpected"); }, [] {});
    assert_test(urls.size() == 2 && urls[1] == "https://rss.sfacg.com/body.gif",
                "image dimensions never become URLs");
    Core fresh(config(root + "/new-session.dat", root + "/fresh"));
    int api_chapters = 0, public_chapters = 0;
    fresh.network.fixture = [&](auto url, auto method, const auto &headers, auto body) {
        Response r;
        Json data = Json::object{};
        if (url.find("api.sfacg.com") != std::string::npos) {
            assert_test(headers.count("sfsecurity") != 0, "native API carries signature");
            if (url.ends_with("/sessions")) {
                assert_test(method == "POST" && Json::parse(body)["password"].text() == "synthetic-password",
                            "native login JSON");
                r.headers["set-cookie"] = {".SFCommunity=synthetic-community; Path=/user",
                                           "session_APP=synthetic-session; Path=/sessions"};
            } else {
                assert_test(headers.at("Cookie") ==
                                ".SFCommunity=synthetic-community; session_APP=synthetic-session",
                            "native download carries both session cookies");
                if (url.find("/novels/9/dirs") != std::string::npos) {
                    Json::array list;
                    for (int i = 101; i <= 105; i++)
                        list.push_back(obj({{"chapId", i},
                                            {"title", "新章节 " + std::to_string(i)},
                                            {"originNeedFireMoney", 0}}));
                    auto volume = obj({{"title", "卷一"}, {"chapterList", list}});
                    data = obj({{"volumeList", Json::array{volume}}});
                } else if (url.find("/novels/9?") != std::string::npos)
                    data = obj({{"novelName", "新建缓存测试书"},
                                {"authorName", "测试作者"},
                                {"expand", obj({{"bigNovelCover", ""}})}});
                else if (url.find("/Chaps/") != std::string::npos) {
                    api_chapters++;
                    assert_test(url.find("autoOrder=false") != std::string::npos,
                                "automatic purchase disabled");
                    data = obj({{"content", utf8(a) + "岚曜😀"}});
                }
            }
            r.body = obj({{"status", obj({{"httpCode", 200}})}, {"data", data}}).dump();
        } else {
            assert_test(headers.count("Cookie") == 0, "API session never leaks to web requests");
            if (url.ends_with("MainIndex/")) {
                r.body = "<div class='catalog-list'>";
                for (int i = 101; i <= 105; i++)
                    r.body += "<a href='" + std::string(i > 103 ? "/vip/c/" : "/Novel/9/1/") +
                              std::to_string(i) + "/'>章节</a>";
                r.body += "</div>";
            } else {
                public_chapters++;
                r.body = "<div id='ChapterBody'><p>" + utf8(b) + "岚曜😀</p></div>";
            }
        }
        return r;
    };
    fresh.login("synthetic-account", "synthetic-password");
    auto downloaded = fresh.export_book(obj({{"id", "9"},
                                             {"volumes", Json::array{"卷一"}},
                                             {"txt", true},
                                             {"epub", true},
                                             {"outputFolder", root + "/download-books"}}));
    assert_test(downloaded["exported"].number() == 5 && downloaded["failed"].number() == 0 &&
                    api_chapters == 5 && public_chapters == 3,
                "fresh catalog, web and VIP downloads produce five complete chapters");
    assert_test(!fs::exists(fs::path(wide(root + "/fresh")) / L".chapter-cache") &&
                    fs::exists(fs::path(wide(root + "/fresh")) / L".sfcore-cache"),
                "independent new cache");
}
} // namespace sf
static thread_local std::string init_error;
extern "C" {
SF_API void *sf_create(const char *config, sf_event_callback callback, void *user) {
    try {
        return new sf::Core(sf::Json::parse(config ? config : "{}"), callback, user);
    } catch (const std::exception &e) {
        init_error = e.what();
        return nullptr;
    }
}
SF_API const char *sf_init_error() { return init_error.c_str(); }
SF_API char *sf_call(void *core, const char *request) {
    sf::Json result;
    auto *c = static_cast<sf::Core *>(core);
    try {
        if (!c)
            throw sf::Error("原生核心未初始化");
        auto value = c->dispatch(sf::Json::parse(request ? request : "{}"));
        result = sf::obj({{"ok", true},
                          {"result", value},
                          {"loggedIn", c->logged},
                          {"hasSavedSession", c->has_session()}});
    } catch (const sf::Error &e) {
        result = sf::obj({{"ok", false},
                          {"error", c ? c->safe(e.what()) : e.what()},
                          {"code", e.code},
                          {"cancelled", e.cancelled},
                          {"loggedIn", c && c->logged},
                          {"hasSavedSession", c && c->has_session()}});
    } catch (const std::exception &e) {
        result = sf::obj({{"ok", false},
                          {"error", c ? c->safe(e.what()) : e.what()},
                          {"loggedIn", c && c->logged},
                          {"hasSavedSession", c && c->has_session()}});
    }
    auto text = result.dump();
    char *out = new char[text.size() + 1];
    memcpy(out, text.c_str(), text.size() + 1);
    return out;
}
SF_API void sf_free(char *value) { delete[] value; }
SF_API void sf_reset_cancel(void *core) {
    if (core)
        static_cast<sf::Core *>(core)->network.cancelled = false;
}
SF_API void sf_cancel(void *core) {
    if (core)
        static_cast<sf::Core *>(core)->network.cancel();
}
SF_API void sf_destroy(void *core) { delete static_cast<sf::Core *>(core); }
}
