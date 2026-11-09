#include "session.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>

bool parse_hhmm(const std::string& s, int& minutes) {
    int h = 0, m = 0;
    if (std::sscanf(s.c_str(), "%d:%d", &h, &m) != 2) return false;
    if (h < 0 || h > 23 || m < 0 || m > 59) return false;
    minutes = h * 60 + m;
    return true;
}

std::string date_str(const SYSTEMTIME& st) {
    char b[32];
    std::snprintf(b, sizeof b, "%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
    return b;
}

int minute_of_day(const SYSTEMTIME& st) { return st.wHour * 60 + st.wMinute; }

bool in_trading_session(const std::vector<SessionWindow>& sessions,
                        const std::vector<int>& workdays,
                        const std::vector<std::string>& holidays,
                        const SYSTEMTIME& st) {
    if (std::find(workdays.begin(), workdays.end(), (int)st.wDayOfWeek) == workdays.end()) return false;

    const std::string today = date_str(st);
    if (std::find(holidays.begin(), holidays.end(), today) != holidays.end()) return false;

    const int cur = minute_of_day(st);
    for (const auto& w : sessions) {
        if (cur >= w.start_min && cur < w.end_min) return true;
    }
    return false;
}
