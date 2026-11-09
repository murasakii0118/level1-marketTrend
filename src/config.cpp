#include "config.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <fstream>
#include <sstream>

#include "json.h"

void settings_defaults(Settings& s) {
    s.output_dir = "level1";
    s.flush_interval_sec = 15;
    s.ws_url = "wss://w-base-common.10jqka.com.cn/ws?platform=hxkline&gpid=2";
    s.push_level = 3;
    s.lang = "en_us";
    s.trade_class = "intraday";
    s.sessions.clear();
    s.sessions.push_back(SessionWindow{9 * 60 + 15, 11 * 60 + 30});
    s.sessions.push_back(SessionWindow{13 * 60, 15 * 60});
    s.workdays = {1, 2, 3, 4, 5};
    s.holidays.clear();
    s.reconnect_delay_sec = 3;
    s.restart_delay_sec = 3;
}

bool load_settings(const std::string& path, Settings& s, std::string& err) {
    settings_defaults(s);

    std::ifstream in(path, std::ios::binary);
    if (!in) { err = "无法打开 settings: " + path; return false; }
    std::stringstream ss;
    ss << in.rdbuf();
    const std::string text = ss.str();

    JValue root;
    std::string perr;
    if (!JValue::parse(text, root, perr)) { err = "settings.json 解析失败: " + perr; return false; }
    if (root.type != JValue::Obj) { err = "settings.json 根节点不是对象"; return false; }

    if (root.has("output_dir")) s.output_dir = root.str_or("output_dir", s.output_dir);
    s.flush_interval_sec = (int)root.num_or("flush_interval_sec", s.flush_interval_sec);
    if (s.flush_interval_sec <= 0) s.flush_interval_sec = 15;
    s.ws_url = root.str_or("ws_url", s.ws_url);
    s.push_level = (int)root.num_or("push_level", s.push_level);
    s.lang = root.str_or("lang", s.lang);
    s.trade_class = root.str_or("trade_class", s.trade_class);
    s.reconnect_delay_sec = (int)root.num_or("reconnect_delay_sec", s.reconnect_delay_sec);
    s.restart_delay_sec = (int)root.num_or("restart_delay_sec", s.restart_delay_sec);

    const JValue* sess = root.find("sessions");
    if (sess && sess->type == JValue::Arr && !sess->arr.empty()) {
        std::vector<SessionWindow> list;
        for (const auto& w : sess->arr) {
            if (w.type != JValue::Obj) continue;
            int a = 0, b = 0;
            if (!parse_hhmm(w.str_or("start", ""), a)) continue;
            if (!parse_hhmm(w.str_or("end", ""), b)) continue;
            if (b <= a) continue;
            list.push_back(SessionWindow{a, b});
        }
        if (!list.empty()) s.sessions = list;
    }

    const JValue* wd = root.find("workdays");
    if (wd && wd->type == JValue::Arr && !wd->arr.empty()) {
        std::vector<int> list;
        for (const auto& v : wd->arr) if (v.type == JValue::Num) list.push_back((int)v.num);
        if (!list.empty()) s.workdays = list;
    }

    const JValue* hol = root.find("holidays");
    if (hol && hol->type == JValue::Arr) {
        s.holidays.clear();
        for (const auto& v : hol->arr) if (v.type == JValue::Str) s.holidays.push_back(v.str);
    }

    return true;
}

std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w((size_t)n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

std::string narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}

std::string exe_path() {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetModuleFileNameW(nullptr, buf, (DWORD)(sizeof(buf) / sizeof(buf[0])));
    return narrow(std::wstring(buf, n));
}

std::string exe_dir() {
    const std::string p = exe_path();
    size_t i = p.find_last_of("\\/");
    return i == std::string::npos ? "." : p.substr(0, i);
}

std::string cwd() {
    wchar_t buf[MAX_PATH * 4];
    DWORD n = GetCurrentDirectoryW((DWORD)(sizeof(buf) / sizeof(buf[0])), buf);
    return narrow(std::wstring(buf, n));
}

std::string join_path(const std::string& a, const std::string& b) {
    if (a.empty()) return b;
    if (b.empty()) return a;
    char last = a.back();
    if (last == '\\' || last == '/') return a + b;
    return a + "\\" + b;
}

bool file_exists(const std::string& p) {
    DWORD attr = GetFileAttributesW(widen(p).c_str());
    return attr != INVALID_FILE_ATTRIBUTES;
}

bool is_absolute(const std::string& p) {
    return p.size() >= 2 && p[1] == ':';
}

bool mkdirs(const std::string& path) {
    if (path.empty()) return false;
    std::string cur;
    for (size_t i = 0; i < path.size(); ++i) {
        const char c = path[i];
        cur.push_back(c);
        if (c == '\\' || c == '/') {
            if (cur.size() > 3) CreateDirectoryW(widen(cur).c_str(), nullptr);
        }
    }
    CreateDirectoryW(widen(path).c_str(), nullptr);
    DWORD e = GetLastError();
    return e == ERROR_SUCCESS || e == ERROR_ALREADY_EXISTS || file_exists(path);
}
