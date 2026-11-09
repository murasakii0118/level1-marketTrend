#include "store.h"

#include <cstring>
#include <string>

DayStore::~DayStore() { close(); }

bool DayStore::open(const std::string& path, std::string& err) {
    close();
    path_ = path;

    FILE* f = std::fopen(path.c_str(), "r+b");
    if (!f) {
        f = std::fopen(path.c_str(), "w+b");
        if (!f) { err = "无法创建文件: " + path; return false; }
        const char* init = "[\n]";
        std::fwrite(init, 1, std::strlen(init), f);
        std::fflush(f);
        bracket_ = 2;  // ']' 的偏移
        records_ = 0;
        f_ = f;
        return true;
    }

    f_ = f;
    if (!recover(err)) { close(); return false; }
    return true;
}

// 打开已存在文件时，定位结尾 ']'；若上次崩溃留下了半截数据则修复
bool DayStore::recover(std::string& err) {
    std::fseek(f_, 0, SEEK_END);
    long sz = std::ftell(f_);
    if (sz <= 0) {
        std::fclose(f_);
        f_ = std::fopen(path_.c_str(), "w+b");
        if (!f_) { err = "无法重建文件: " + path_; return false; }
        const char* init = "[\n]";
        std::fwrite(init, 1, std::strlen(init), f_);
        std::fflush(f_);
        bracket_ = 2;
        records_ = 0;
        return true;
    }

    std::string content((size_t)sz, '\0');
    std::fseek(f_, 0, SEEK_SET);
    size_t got = std::fread(&content[0], 1, (size_t)sz, f_);
    content.resize(got);

    size_t br = content.rfind(']');
    bool tail_ok = false;
    if (br != std::string::npos) {
        tail_ok = content.find_first_not_of(" \t\r\n", br + 1) == std::string::npos;
    }

    if (br != std::string::npos && tail_ok) {
        bracket_ = (long)br;
        records_ = content.find('{') != std::string::npos ? 1 : 0;
        return true;
    }

    // 修复：截到最后一个完整对象，补上 ]
    size_t last_obj = content.rfind('}');
    std::string rebuilt;
    if (last_obj == std::string::npos) {
        rebuilt = "[\n]";
        records_ = 0;
    } else {
        rebuilt = content.substr(0, last_obj + 1) + "\n]";
        records_ = 1;
    }

    std::fclose(f_);
    f_ = std::fopen(path_.c_str(), "w+b");
    if (!f_) { err = "无法重建文件: " + path_; return false; }
    std::fwrite(rebuilt.data(), 1, rebuilt.size(), f_);
    std::fflush(f_);
    bracket_ = (long)(rebuilt.rfind(']'));
    return true;
}

bool DayStore::append(const std::string& record, std::string& err) {
    if (!f_) { err = "文件未打开"; return false; }

    std::string payload;
    payload.reserve(record.size() + 8);
    payload += records_ > 0 ? ",\n" : "\n";
    payload += record;
    payload += "\n]";

    if (std::fseek(f_, bracket_, SEEK_SET) != 0) { err = "seek 失败"; return false; }
    if (std::fwrite(payload.data(), 1, payload.size(), f_) != payload.size()) {
        err = "写入失败";
        return false;
    }
    bracket_ = std::ftell(f_) - 1;
    ++records_;
    return true;
}

void DayStore::flush() {
    if (f_) std::fflush(f_);
}

void DayStore::close() {
    if (f_) {
        std::fflush(f_);
        std::fclose(f_);
        f_ = nullptr;
    }
    bracket_ = 0;
    records_ = 0;
}
