#define WIN32_LEAN_AND_MEAN
#define _WIN32_WINNT 0x0A00
#include <windows.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "config.h"
#include "json.h"
#include "session.h"
#include "snappy.h"
#include "store.h"
#include "ws.h"

// ---------------------------------------------------------------- 公共

static volatile bool g_stop = false;
static FILE* g_log = nullptr;

static const char* kVersion = "1.0.0";

static void logf_(const char* fmt, ...) {
    char line[2048];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);

    SYSTEMTIME st;
    GetLocalTime(&st);
    char stamp[32];
    std::snprintf(stamp, sizeof stamp, "%02d:%02d:%02d", st.wHour, st.wMinute, st.wSecond);

    std::printf("[%s] %s\n", stamp, line);
    std::fflush(stdout);
    if (g_log) {
        std::fprintf(g_log, "[%04d-%02d-%02d %s] %s\n", st.wYear, st.wMonth, st.wDay, stamp, line);
        std::fflush(g_log);
    }
}

static BOOL WINAPI on_console(DWORD type) {
    if (type == CTRL_C_EVENT || type == CTRL_BREAK_EVENT || type == CTRL_CLOSE_EVENT) {
        g_stop = true;
        return TRUE;
    }
    return FALSE;
}

static long long now_ms() {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    unsigned long long v = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    return (long long)(v / 10000ULL) - 11644473600000LL;
}

static std::string resolve_base(const Settings& s) {
    return is_absolute(s.output_dir) ? s.output_dir : join_path(exe_dir(), s.output_dir);
}

static std::string join_codes(const std::vector<std::string>& v) {
    std::string r;
    for (size_t i = 0; i < v.size(); ++i) {
        if (i) r += ",";
        r += v[i];
    }
    return r;
}

// ---------------------------------------------------------------- 标的

struct Symbol {
    std::string code;
    int market = 0;
    std::string folder;
};

static bool classify_symbol(const std::string& raw, Symbol& out, std::string& err) {
    std::string c = raw;
    while (!c.empty() && (c.front() == ' ' || c.front() == '\t')) c.erase(c.begin());
    while (!c.empty() && (c.back() == ' ' || c.back() == '\t' || c.back() == '\r')) c.pop_back();
    size_t dot = c.find('.');
    if (dot != std::string::npos) c = c.substr(0, dot);
    if (c.size() != 6) { err = "非法代码: " + raw; return false; }
    for (char ch : c) {
        if (ch < '0' || ch > '9') { err = "非法代码: " + raw; return false; }
    }

    auto starts = [&](const char* p) { return c.compare(0, std::strlen(p), p) == 0; };

    const bool etf = starts("51") || starts("52") || starts("56") || starts("58") ||
                     starts("15") || starts("16");
    const bool bj = starts("92") || c[0] == '4' || c[0] == '8';
    const bool sh = c[0] == '6' || c[0] == '9';

    out.code = c;
    if (etf) {
        out.market = (starts("15") || starts("16")) ? 33 : 17;
        out.folder = c + ".ETF";
    } else if (bj) {
        out.market = 151;
        out.folder = c + ".BJ";
    } else if (sh) {
        out.market = 17;
        out.folder = c + ".SH";
    } else if (c[0] == '0' || c[0] == '3' || c[0] == '2') {
        out.market = 33;
        out.folder = c + ".SZ";
    } else {
        err = "无法判断交易所: " + raw;
        return false;
    }
    return true;
}

// ---------------------------------------------------------------- 编码 / 格式化

static std::string random_hex32() {
    static const char* hex = "0123456789abcdef";
    unsigned seed = (unsigned)GetTickCount64() ^ (GetCurrentProcessId() << 8);
    std::string s(32, '0');
    for (auto& ch : s) {
        seed = seed * 1103515245u + 12345u;
        ch = hex[(seed >> 16) & 0xF];
    }
    return s;
}

static bool b64url_decode(const std::string& in, std::string& out) {
    out.clear();
    out.reserve(in.size() * 3 / 4 + 4);
    int val = 0, bits = 0;
    for (char c : in) {
        int d;
        if (c >= 'A' && c <= 'Z') d = c - 'A';
        else if (c >= 'a' && c <= 'z') d = c - 'a' + 26;
        else if (c >= '0' && c <= '9') d = c - '0' + 52;
        else if (c == '-' || c == '+') d = 62;
        else if (c == '_' || c == '/') d = 63;
        else if (c == '=') break;
        else if (c == '\n' || c == '\r' || c == ' ') continue;
        else return false;
        val = (val << 6) | d;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back((char)((val >> bits) & 0xFF));
        }
    }
    return true;
}

static std::string fmt_num(double v) {
    if (std::isnan(v) || std::isinf(v)) return "null";
    char b[64];
    std::snprintf(b, sizeof b, "%.4f", v);
    std::string s = b;
    if (s.find('.') != std::string::npos) {
        while (!s.empty() && s.back() == '0') s.pop_back();
        if (!s.empty() && s.back() == '.') s.pop_back();
    }
    if (s == "-0") s = "0";
    return s;
}

static std::string fmt_time_ms(long long ms) {
    time_t sec = (time_t)(ms / 1000);
    int msec = (int)(ms % 1000);
    if (msec < 0) msec += 1000;
    struct tm tmv {};
    localtime_s(&tmv, &sec);
    char b[32];
    std::snprintf(b, sizeof b, "%02d:%02d:%02d.%03d", tmv.tm_hour, tmv.tm_min, tmv.tm_sec, msec);
    return b;
}

// ---------------------------------------------------------------- 报文

static std::string build_subscribe(const std::vector<Symbol>& syms, const Settings& s, int request_id) {
    std::string data = "[";
    for (size_t i = 0; i < syms.size(); ++i) {
        if (i) data += ",";
        const Symbol& y = syms[i];
        const std::string mk = std::to_string(y.market);
        data += "{\"group\":\"" + mk + "\",\"method\":\"add\",\"obj_key\":\"" + y.code + "\",\"obj\":{";
        data += "\"trade_class\":\"" + s.trade_class + "\",";
        data += "\"data_class\":\"snapshot\",";
        data += "\"market\":" + mk + ",";
        data += "\"code\":\"" + y.code + "\",";
        data += "\"push_level\":" + std::to_string(s.push_level) + ",";
        data += "\"need_order_book\":true}}";
    }
    data += "]";

    std::string msg;
    msg += "{\"service_type\":51,\"version\":\"v1\",\"trace_id\":\"" + random_hex32() + "\",";
    msg += "\"request_id\":" + std::to_string(request_id) + ",";
    msg += "\"request\":{\"sub_type\":\"quote\",\"lang\":\"" + s.lang + "\",";
    msg += "\"accept_formats\":[\"json\"],\"data\":" + data + "}}";
    return msg;
}

static void append_levels(std::string& out, const JValue* arr, const char* price_key,
                          const char* vol_key, int want) {
    out += "[";
    for (int i = 0; i < want; ++i) {
        if (i) out += ",";
        double price = 0;
        double vol = 0;
        if (arr && arr->type == JValue::Arr && i < (int)arr->arr.size()) {
            price = arr->arr[(size_t)i].num_or(price_key, 0);
            vol = arr->arr[(size_t)i].num_or(vol_key, 0);
        }
        out += "[";
        out += price > 0 ? fmt_num(price) : "null";
        out += ",";
        out += fmt_num(vol);
        out += "]";
    }
    out += "]";
}

static std::string build_record(const JValue& it) {
    const long long ts = (long long)it.num_or("1", 0);

    std::string rec;
    rec.reserve(640);
    rec += "{\"t\":\"" + fmt_time_ms(ts) + "\",\"ts\":" + std::to_string(ts);

    struct F { const char* key; const char* field; };
    static const F fields[] = {
        {"last", "10"}, {"size", "49"}, {"vol", "13"}, {"amt", "19"},
        {"open", "7"},  {"high", "8"},  {"low", "9"},  {"pre", "6"},
        {"chg", "264648"}, {"pct", "199112"},
    };
    for (const auto& f : fields) {
        rec += ",\"";
        rec += f.key;
        rec += "\":";
        rec += fmt_num(it.num_or(f.field, 0));
    }

    rec += ",\"bids\":";
    append_levels(rec, it.find("bids"), "24", "25", 5);
    rec += ",\"asks\":";
    append_levels(rec, it.find("asks"), "30", "31", 5);
    rec += "}";
    return rec;
}

// ---------------------------------------------------------------- 落盘管理

static std::string stop_flag_path(const std::string& base) { return join_path(base, "marketTrend.stop"); }
static std::string pid_path(const std::string& base) { return join_path(base, "marketTrend.pid"); }
static std::string log_path(const std::string& base) { return join_path(base, "marketTrend.log"); }

static void flush_all(std::map<std::string, std::unique_ptr<DayStore>>& stores) {
    for (auto& kv : stores) if (kv.second) kv.second->flush();
}

static bool ensure_day(const Settings& s, const std::string& base, const std::string& date,
                       const std::vector<Symbol>& syms,
                       std::map<std::string, std::unique_ptr<DayStore>>& stores,
                       std::string& cur_date) {
    if (date == cur_date && !stores.empty()) return true;

    for (auto& kv : stores) if (kv.second) kv.second->close();
    stores.clear();
    cur_date = date;

    for (const auto& y : syms) {
        const std::string dir = join_path(base, y.folder);
        if (!mkdirs(dir)) {
            logf_("WARN mkdir failed: %s", dir.c_str());
            continue;
        }
        const std::string path = join_path(dir, date + ".json");
        auto st = std::make_unique<DayStore>();
        std::string err;
        if (!st->open(path, err)) {
            logf_("ERROR open %s: %s", path.c_str(), err.c_str());
            continue;
        }
        logf_("open %s", path.c_str());
        stores[y.folder] = std::move(st);
    }
    return true;
}

// ---------------------------------------------------------------- worker

static int run_worker(const Settings& s, const std::vector<Symbol>& syms) {
    const std::string base = resolve_base(s);
    mkdirs(base);

    g_log = std::fopen(log_path(base).c_str(), "a");
    logf_("worker start, v%s, %zu symbol(s), out=%s", kVersion, syms.size(), base.c_str());

    std::map<std::string, std::string> code2folder;
    for (const auto& y : syms) code2folder[y.code] = y.folder;

    std::map<std::string, std::unique_ptr<DayStore>> stores;
    std::string cur_date;

    long long rec_total = 0;
    unsigned long long last_report = 0;

    while (!g_stop && !file_exists(stop_flag_path(base))) {
        SYSTEMTIME st;
        GetLocalTime(&st);

        // 任务 3：只在交易时段工作
        if (!in_trading_session(s.sessions, s.workdays, s.holidays, st)) {
            flush_all(stores);
            Sleep(5000);
            continue;
        }

        ensure_day(s, base, date_str(st), syms, stores, cur_date);

        WsClient ws;
        std::string err;
        if (!ws.connect(L"w-base-common.10jqka.com.cn", L"/ws?platform=hxkline&gpid=2",
                        L"https://stockpage.10jqka.com.cn", err)) {
            logf_("WARN ws connect: %s", err.c_str());
            Sleep((DWORD)s.reconnect_delay_sec * 1000);
            continue;
        }

        logf_("ws connected");
        int request_id = 1;
        long long next_flush = now_ms() + (long long)s.flush_interval_sec * 1000;
        long long next_ping = now_ms() + 15000;
        bool subscribed = false;
        int frames = 0;
        long long session_start = now_ms();

        while (!g_stop && !file_exists(stop_flag_path(base))) {
            GetLocalTime(&st);
            if (!in_trading_session(s.sessions, s.workdays, s.holidays, st)) {
                logf_("session closed, disconnect");
                break;
            }

            std::string msg;
            const int r = ws.recv(msg, err);
            if (r < 0) {
                logf_("WARN ws recv: %s", err.c_str());
                break;
            }
            if (r > 0) {
                if (msg == "ready") {
                    const std::string sub = build_subscribe(syms, s, request_id++);
                    if (ws.send_text(sub, err)) {
                        subscribed = true;
                        logf_("subscribed %zu symbol(s)", syms.size());
                    } else {
                        logf_("WARN subscribe send: %s", err.c_str());
                        break;
                    }
                } else if (msg == "pong") {
                    // 心跳回包，忽略
                } else if (msg == "ping") {
                    std::string e2;
                    ws.send_text("pong", e2);
                } else {
                    // pack_type=2 → base64url + snappy → JSON
                    JValue env;
                    std::string perr;
                    if (!JValue::parse(msg, env, perr)) {
                        logf_("WARN bad envelope: %s", perr.c_str());
                    } else {
                        const int pack = (int)env.num_or("pack_type", 1);
                        const std::string resp = env.str_or("response", "");
                        if ((int)env.num_or("status", 0) != 0) {
                            logf_("WARN server status: %s", msg.substr(0, 200).c_str());
                        } else if (pack == 2) {
                            std::string bin, plain, e2;
                            if (!b64url_decode(resp, bin)) {
                                logf_("WARN base64 decode failed");
                            } else if (!snappy_uncompress(bin, plain, e2)) {
                                logf_("WARN snappy: %s", e2.c_str());
                            } else {
                                JValue doc;
                                if (JValue::parse(plain, doc, e2)) {
                                    const JValue* data = doc.find("data");
                                    if (data && data->type == JValue::Arr) {
                                        for (const auto& it : data->arr) {
                                            const std::string code = it.str_or("code", "");
                                            auto ci = code2folder.find(code);
                                            if (ci == code2folder.end()) continue;
                                            auto si = stores.find(ci->second);
                                            if (si == stores.end() || !si->second) continue;
                                            std::string we;
                                            if (si->second->append(build_record(it), we)) {
                                                ++frames;
                                            } else {
                                                logf_("WARN append: %s", we.c_str());
                                            }
                                        }
                                    }
                                }
                            }
                        } else if (!resp.empty()) {
                            // 回执：{"status_code":0,"status_msg":""}
                            if (resp.find("\"status_code\":5") != std::string::npos)
                                logf_("WARN subscribe rejected: %s", resp.c_str());
                        }
                    }
                }
            }

            // 心跳：每 15s 发一次文本 ping，服务端回 pong
            if (now_ms() >= next_ping) {
                std::string e2;
                if (!ws.send_text("ping", e2)) {
                    logf_("WARN heartbeat: %s", e2.c_str());
                    break;
                }
                next_ping = now_ms() + 15000;
            }

            // 任务 4：小缓冲，每 flush_interval_sec 秒落盘
            if (now_ms() >= next_flush) {
                flush_all(stores);
                next_flush = now_ms() + (long long)s.flush_interval_sec * 1000;
                rec_total += frames;
                frames = 0;
                const unsigned long long tick = GetTickCount64() / 1000;
                if (tick - last_report >= 60) {
                    last_report = tick;
                    logf_("alive, records=%lld, uptime=%llds", rec_total,
                          (now_ms() - session_start) / 1000);
                }
            }
        }

        flush_all(stores);
        ws.close();
        (void)subscribed;
        Sleep(1000);
    }

    flush_all(stores);
    for (auto& kv : stores) if (kv.second) kv.second->close();
    logf_("worker stop, total records=%lld", rec_total);
    return 0;
}

// ---------------------------------------------------------------- supervisor / daemon

static int run_supervisor(const Settings& s, const std::vector<Symbol>& syms,
                          const std::string& settings_path) {
    const std::string base = resolve_base(s);
    mkdirs(base);
    g_log = std::fopen(log_path(base).c_str(), "a");

    const std::string stopf = stop_flag_path(base);
    DeleteFileW(widen(stopf).c_str());

    {
        FILE* f = std::fopen(pid_path(base).c_str(), "w");
        if (f) {
            std::fprintf(f, "%lu\n", GetCurrentProcessId());
            std::fclose(f);
        }
    }

    logf_("supervisor start pid=%lu", GetCurrentProcessId());

    const std::wstring exe = widen(exe_path());

    std::vector<std::string> codes;
    for (const auto& y : syms) codes.push_back(y.code);
    const std::wstring child_cmd = L"\"" + exe + L"\" --worker --settings \"" +
                                   widen(settings_path) + L"\" --get " + widen(join_codes(codes));

    while (!g_stop && !file_exists(stopf)) {
        STARTUPINFOW si;
        ZeroMemory(&si, sizeof si);
        si.cb = sizeof si;
        PROCESS_INFORMATION pi;
        ZeroMemory(&pi, sizeof pi);

        std::vector<wchar_t> cmd(child_cmd.begin(), child_cmd.end());
        cmd.push_back(0);

        if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                            nullptr, nullptr, &si, &pi)) {
            logf_("ERROR spawn worker: %lu", GetLastError());
            Sleep((DWORD)s.restart_delay_sec * 1000);
            continue;
        }
        logf_("worker pid=%lu", pi.dwProcessId);

        for (;;) {
            const DWORD w = WaitForSingleObject(pi.hProcess, 1000);
            if (w == WAIT_OBJECT_0) {
                DWORD code = 0;
                GetExitCodeProcess(pi.hProcess, &code);
                logf_("worker exited, code=%lu", code);
                break;
            }
            if (g_stop || file_exists(stopf)) {
                logf_("stop requested, waiting worker to flush");
                // 给 worker 最多 5s 自己落盘退出（它会看到 stop 标志后 flush）
                if (WaitForSingleObject(pi.hProcess, 5000) != WAIT_OBJECT_0) {
                    logf_("worker did not exit, terminating");
                    TerminateProcess(pi.hProcess, 0);
                    WaitForSingleObject(pi.hProcess, 5000);
                }
                break;
            }
        }

        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);

        if (g_stop || file_exists(stopf)) break;
        Sleep((DWORD)s.restart_delay_sec * 1000);
    }

    DeleteFileW(widen(stopf).c_str());
    DeleteFileW(widen(pid_path(base)).c_str());
    logf_("supervisor stop");
    return 0;
}

static int run_daemon(const Settings& s, const std::vector<Symbol>& syms,
                      const std::string& settings_path) {
    const std::string base = resolve_base(s);
    mkdirs(base);
    DeleteFileW(widen(stop_flag_path(base)).c_str());

    std::vector<std::string> codes;
    for (const auto& y : syms) codes.push_back(y.code);

    const std::wstring cmd = L"\"" + widen(exe_path()) + L"\" --supervisor --settings \"" +
                             widen(settings_path) + L"\" --get " + widen(join_codes(codes));

    STARTUPINFOW si;
    ZeroMemory(&si, sizeof si);
    si.cb = sizeof si;
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof pi);

    std::vector<wchar_t> buf(cmd.begin(), cmd.end());
    buf.push_back(0);

    const DWORD flags = DETACHED_PROCESS | CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP;
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, flags, nullptr, nullptr, &si, &pi)) {
        std::printf("failed to start daemon: %lu\n", GetLastError());
        return 1;
    }

    std::printf("daemon started: supervisor pid=%lu\n", pi.dwProcessId);
    std::printf("output: %s\n", base.c_str());
    std::printf("stop with: %s --stop\n", exe_path().c_str());
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return 0;
}

// ---------------------------------------------------------------- main

static void usage() {
    std::printf(
        "level1-marketTrend v%s\n"
        "\n"
        "Usage:\n"
        "  marketTrend --get 600519[,000001,510300...] [options]\n"
        "\n"
        "Options:\n"
        "  --get <codes>      comma separated stock/ETF codes\n"
        "  --daemon           run in background (detached supervisor restarts worker)\n"
        "  --stop             stop the daemon\n"
        "  --settings <path>  settings.json path (default: <exe dir>/settings.json)\n"
        "  --supervisor       internal: supervisor loop\n"
        "  --worker           internal: worker loop\n"
        "  --help             this help\n",
        kVersion);
}

int main(int argc, char** argv) {
    SetConsoleCtrlHandler(on_console, TRUE);

    std::vector<std::string> raw_codes;
    std::string settings_path;
    bool daemon = false, supervisor = false, worker = false, stop = false;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&](std::string& out) {
            if (i + 1 < argc) { out = argv[++i]; return true; }
            return false;
        };

        if (a == "--help" || a == "-h") { usage(); return 0; }
        else if (a == "--get") {
            std::string v;
            if (!next(v)) { std::printf("--get needs a value\n"); return 1; }
            size_t p = 0;
            while (p <= v.size()) {
                size_t q = v.find(',', p);
                if (q == std::string::npos) q = v.size();
                std::string one = v.substr(p, q - p);
                if (!one.empty()) raw_codes.push_back(one);
                p = q + 1;
            }
        } else if (a.rfind("--get=", 0) == 0) {
            std::string v = a.substr(6);
            size_t p = 0;
            while (p <= v.size()) {
                size_t q = v.find(',', p);
                if (q == std::string::npos) q = v.size();
                std::string one = v.substr(p, q - p);
                if (!one.empty()) raw_codes.push_back(one);
                p = q + 1;
            }
        } else if (a == "--daemon") daemon = true;
        else if (a == "--supervisor") supervisor = true;
        else if (a == "--worker") worker = true;
        else if (a == "--stop") stop = true;
        else if (a == "--settings") {
            std::string v;
            if (!next(v)) { std::printf("--settings needs a value\n"); return 1; }
            settings_path = v;
        } else {
            std::printf("unknown option: %s\n", a.c_str());
            usage();
            return 1;
        }
    }

    if (settings_path.empty()) {
        const std::string p1 = join_path(exe_dir(), "settings.json");
        const std::string p2 = join_path(cwd(), "settings.json");
        settings_path = file_exists(p1) ? p1 : p2;
    }

    Settings s;
    std::string err;
    if (!load_settings(settings_path, s, err)) {
        std::printf("settings error: %s (path: %s)\n", err.c_str(), settings_path.c_str());
        return 1;
    }

    if (stop) {
        const std::string base = resolve_base(s);
        mkdirs(base);
        FILE* f = std::fopen(stop_flag_path(base).c_str(), "w");
        if (!f) { std::printf("cannot write stop flag\n"); return 1; }
        std::fclose(f);
        std::printf("stop flag written: %s\n", stop_flag_path(base).c_str());
        return 0;
    }

    if (raw_codes.empty()) {
        std::printf("--get is required\n\n");
        usage();
        return 1;
    }

    std::vector<Symbol> syms;
    for (const auto& c : raw_codes) {
        Symbol y;
        std::string e;
        if (!classify_symbol(c, y, e)) {
            std::printf("%s\n", e.c_str());
            return 1;
        }
        bool dup = false;
        for (const auto& x : syms) if (x.code == y.code) dup = true;
        if (!dup) syms.push_back(y);
    }

    if (supervisor) return run_supervisor(s, syms, settings_path);
    if (daemon) return run_daemon(s, syms, settings_path);
    (void)worker;
    return run_worker(s, syms);
}
