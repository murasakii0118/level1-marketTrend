#pragma once
#include <cstdio>
#include <string>

// 按天追加写的 JSON 数组存储。
// 文件任意时刻都是合法 JSON：每次追加前把结尾的 ']' 覆盖掉，写完再补回 ']'。
class DayStore {
public:
    DayStore() = default;
    ~DayStore();
    DayStore(const DayStore&) = delete;
    DayStore& operator=(const DayStore&) = delete;

    bool open(const std::string& path, std::string& err);
    bool append(const std::string& record, std::string& err);
    void flush();
    void close();

    bool is_open() const { return f_ != nullptr; }
    const std::string& path() const { return path_; }
    long records() const { return records_; }

private:
    bool recover(std::string& err);

    FILE* f_ = nullptr;
    long bracket_ = 0;   // ']' 所在偏移
    long records_ = 0;
    std::string path_;
};
