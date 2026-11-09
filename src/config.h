#pragma once
#include <string>
#include <vector>

#include "session.h"

struct Settings {
    std::string output_dir = "level1";
    int flush_interval_sec = 15;
    std::string ws_url = "wss://w-base-common.10jqka.com.cn/ws?platform=hxkline&gpid=2";
    int push_level = 3;
    std::string lang = "en_us";
    std::string trade_class = "intraday";
    std::vector<SessionWindow> sessions;
    std::vector<int> workdays;
    std::vector<std::string> holidays;
    int reconnect_delay_sec = 3;
    int restart_delay_sec = 3;
};

void settings_defaults(Settings& s);
bool load_settings(const std::string& path, Settings& s, std::string& err);

std::wstring widen(const std::string& s);
std::string narrow(const std::wstring& s);
std::string exe_dir();
std::string exe_path();
std::string cwd();
std::string join_path(const std::string& a, const std::string& b);
bool file_exists(const std::string& p);
bool mkdirs(const std::string& path);
bool is_absolute(const std::string& p);
