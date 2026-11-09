#pragma once
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

struct SessionWindow {
    int start_min = 0;  // 从 0 点算起的分钟
    int end_min = 0;
};

bool parse_hhmm(const std::string& s, int& minutes);

std::string date_str(const SYSTEMTIME& st);
int minute_of_day(const SYSTEMTIME& st);

bool in_trading_session(const std::vector<SessionWindow>& sessions,
                        const std::vector<int>& workdays,
                        const std::vector<std::string>& holidays,
                        const SYSTEMTIME& st);
