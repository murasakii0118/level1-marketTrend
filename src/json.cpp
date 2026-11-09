#include "json.h"
#include <cstdlib>
#include <cmath>
#include <cstdio>

const JValue* JValue::find(const std::string& key) const {
    if (type != Obj) return nullptr;
    for (const auto& kv : obj) if (kv.first == key) return &kv.second;
    return nullptr;
}

std::string JValue::str_or(const std::string& key, const std::string& def) const {
    const JValue* v = find(key);
    if (!v) return def;
    if (v->type == Str) return v->str;
    if (v->type == Num) { char b[64]; std::snprintf(b, sizeof b, "%.10g", v->num); return b; }
    return def;
}

double JValue::num_or(const std::string& key, double def) const {
    const JValue* v = find(key);
    if (!v) return def;
    if (v->type == Num) return v->num;
    if (v->type == Str) return std::atof(v->str.c_str());
    return def;
}

bool JValue::bool_or(const std::string& key, bool def) const {
    const JValue* v = find(key);
    if (!v) return def;
    if (v->type == Bool) return v->b;
    return def;
}

namespace {

struct Parser {
    const char* p;
    const char* end;
    std::string err;

    void skip() {
        while (p < end) {
            if (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n') { ++p; continue; }
            break;
        }
    }

    bool fail(const char* m) { if (err.empty()) err = m; return false; }

    bool parse_value(JValue& v, int depth) {
        if (depth > 64) return fail("json: too deep");
        skip();
        if (p >= end) return fail("json: unexpected end");
        switch (*p) {
            case '{': return parse_obj(v, depth);
            case '[': return parse_arr(v, depth);
            case '"': { v.type = JValue::Str; return parse_str(v.str); }
            case 't':
                if (end - p >= 4 && std::string(p, p + 4) == "true") { p += 4; v.type = JValue::Bool; v.b = true; return true; }
                return fail("json: bad literal");
            case 'f':
                if (end - p >= 5 && std::string(p, p + 5) == "false") { p += 5; v.type = JValue::Bool; v.b = false; return true; }
                return fail("json: bad literal");
            case 'n':
                if (end - p >= 4 && std::string(p, p + 4) == "null") { p += 4; v.type = JValue::Null; return true; }
                return fail("json: bad literal");
            default: return parse_num(v);
        }
    }

    bool parse_num(JValue& v) {
        const char* s = p;
        if (p < end && (*p == '-' || *p == '+')) ++p;
        bool any = false;
        while (p < end && ((*p >= '0' && *p <= '9') || *p == '.' || *p == 'e' || *p == 'E' || *p == '-' || *p == '+')) {
            any = true;
            ++p;
        }
        if (!any) return fail("json: bad number");
        std::string tmp(s, p);
        v.type = JValue::Num;
        v.num = std::atof(tmp.c_str());
        return true;
    }

    bool parse_str(std::string& out) {
        if (p >= end || *p != '"') return fail("json: expected string");
        ++p;
        out.clear();
        while (p < end) {
            char c = *p++;
            if (c == '"') return true;
            if (c != '\\') { out.push_back(c); continue; }
            if (p >= end) return fail("json: bad escape");
            char e = *p++;
            switch (e) {
                case '"': out.push_back('"'); break;
                case '\\': out.push_back('\\'); break;
                case '/': out.push_back('/'); break;
                case 'b': out.push_back('\b'); break;
                case 'f': out.push_back('\f'); break;
                case 'n': out.push_back('\n'); break;
                case 'r': out.push_back('\r'); break;
                case 't': out.push_back('\t'); break;
                case 'u': {
                    if (end - p < 4) return fail("json: bad \\u");
                    unsigned v = 0;
                    for (int i = 0; i < 4; ++i) {
                        char h = *p++;
                        v <<= 4;
                        if (h >= '0' && h <= '9') v |= (unsigned)(h - '0');
                        else if (h >= 'a' && h <= 'f') v |= (unsigned)(h - 'a' + 10);
                        else if (h >= 'A' && h <= 'F') v |= (unsigned)(h - 'A' + 10);
                        else return fail("json: bad hex");
                    }
                    // UTF-8 编码（不处理代理对，够用）
                    if (v < 0x80) out.push_back((char)v);
                    else if (v < 0x800) {
                        out.push_back((char)(0xC0 | (v >> 6)));
                        out.push_back((char)(0x80 | (v & 0x3F)));
                    } else {
                        out.push_back((char)(0xE0 | (v >> 12)));
                        out.push_back((char)(0x80 | ((v >> 6) & 0x3F)));
                        out.push_back((char)(0x80 | (v & 0x3F)));
                    }
                    break;
                }
                default: return fail("json: bad escape char");
            }
        }
        return fail("json: unterminated string");
    }

    bool parse_arr(JValue& v, int depth) {
        v.type = JValue::Arr;
        ++p;  // [
        skip();
        if (p < end && *p == ']') { ++p; return true; }
        for (;;) {
            JValue item;
            if (!parse_value(item, depth + 1)) return false;
            v.arr.push_back(std::move(item));
            skip();
            if (p >= end) return fail("json: unterminated array");
            if (*p == ',') { ++p; continue; }
            if (*p == ']') { ++p; return true; }
            return fail("json: expected , or ]");
        }
    }

    bool parse_obj(JValue& v, int depth) {
        v.type = JValue::Obj;
        ++p;  // {
        skip();
        if (p < end && *p == '}') { ++p; return true; }
        for (;;) {
            skip();
            std::string key;
            if (!parse_str(key)) return false;
            skip();
            if (p >= end || *p != ':') return fail("json: expected :");
            ++p;
            JValue item;
            if (!parse_value(item, depth + 1)) return false;
            v.obj.emplace_back(std::move(key), std::move(item));
            skip();
            if (p >= end) return fail("json: unterminated object");
            if (*p == ',') { ++p; continue; }
            if (*p == '}') { ++p; return true; }
            return fail("json: expected , or }");
        }
    }
};

}  // namespace

bool JValue::parse(const std::string& text, JValue& out, std::string& err) {
    Parser ps;
    ps.p = text.data();
    ps.end = text.data() + text.size();
    out = JValue();
    if (!ps.parse_value(out, 0)) { err = ps.err; return false; }
    ps.skip();
    if (ps.p != ps.end) { err = "json: trailing garbage"; return false; }
    return true;
}

std::string json_escape(const std::string& s) {
    std::string o;
    o.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': o += "\\r"; break;
            case '\t': o += "\\t"; break;
            default:
                if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
                else o.push_back((char)c);
        }
    }
    return o;
}
