#include "snappy.h"
#include <cstdint>

bool snappy_uncompress(const std::string& in, std::string& out, std::string& err) {
    const uint8_t* p = (const uint8_t*)in.data();
    const uint8_t* end = p + in.size();

    uint64_t len = 0;
    int shift = 0;
    for (;;) {
        if (p >= end) { err = "snappy: truncated length"; return false; }
        uint8_t b = *p++;
        len |= (uint64_t)(b & 0x7f) << shift;
        if (!(b & 0x80)) break;
        shift += 7;
        if (shift > 35) { err = "snappy: bad length"; return false; }
    }
    if (len > (1ull << 30)) { err = "snappy: payload too large"; return false; }

    out.clear();
    out.reserve((size_t)len);

    while (p < end) {
        uint8_t tag = *p++;
        int t = tag & 3;

        if (t == 0) {  // literal
            uint32_t l = tag >> 2;
            if (l < 60) {
                l += 1;
            } else {
                uint32_t n = l - 59;  // 1..4 extra length bytes
                if ((size_t)(end - p) < n) { err = "snappy: truncated literal len"; return false; }
                uint64_t v = 0;
                for (uint32_t i = 0; i < n; ++i) v |= (uint64_t)(*p++) << (8 * i);
                l = (uint32_t)v + 1;
            }
            if ((size_t)(end - p) < l) { err = "snappy: literal overflow"; return false; }
            out.append((const char*)p, l);
            p += l;
        } else if (t == 1) {  // copy, 1-byte offset
            uint32_t l = ((tag >> 2) & 0x7) + 4;
            if (p >= end) { err = "snappy: truncated copy1"; return false; }
            uint32_t off = ((uint32_t)(tag >> 5) << 8) | *p++;
            if (off == 0 || off > out.size()) { err = "snappy: bad offset"; return false; }
            for (uint32_t i = 0; i < l; ++i) out.push_back(out[out.size() - off]);
        } else if (t == 2) {  // copy, 2-byte offset
            uint32_t l = (tag >> 2) + 1;
            if (end - p < 2) { err = "snappy: truncated copy2"; return false; }
            uint32_t off = (uint32_t)p[0] | ((uint32_t)p[1] << 8);
            p += 2;
            if (off == 0 || off > out.size()) { err = "snappy: bad offset"; return false; }
            for (uint32_t i = 0; i < l; ++i) out.push_back(out[out.size() - off]);
        } else {  // copy, 4-byte offset
            uint32_t l = (tag >> 2) + 1;
            if (end - p < 4) { err = "snappy: truncated copy4"; return false; }
            uint32_t off = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
            p += 4;
            if (off == 0 || off > out.size()) { err = "snappy: bad offset"; return false; }
            for (uint32_t i = 0; i < l; ++i) out.push_back(out[out.size() - off]);
        }
    }

    if (out.size() != (size_t)len) { err = "snappy: size mismatch"; return false; }
    return true;
}
