#pragma once
#include <string>
#include <vector>
#include <utility>

// 极简 JSON：只做本项目需要的事（解析 settings 与服务端报文，输出字符串转义）
struct JValue {
    enum Type { Null, Bool, Num, Str, Arr, Obj };
    Type type = Null;
    bool b = false;
    double num = 0;
    std::string str;
    std::vector<JValue> arr;
    std::vector<std::pair<std::string, JValue>> obj;

    const JValue* find(const std::string& key) const;
    bool has(const std::string& key) const { return find(key) != nullptr; }

    std::string str_or(const std::string& key, const std::string& def) const;
    double num_or(const std::string& key, double def) const;
    bool bool_or(const std::string& key, bool def) const;

    static bool parse(const std::string& text, JValue& out, std::string& err);
};

std::string json_escape(const std::string& s);
