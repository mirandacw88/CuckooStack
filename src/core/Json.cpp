#include "Json.h"

#include <cstdio>

namespace cs {

std::string jsonNum(double v) { char b[32]; std::snprintf(b, sizeof b, "%.4g", v); return b; }

std::string jsonEsc(const std::string& s) {
    std::string o;
    for (char ch : s) {
        if (ch == '"' || ch == '\\') { o += '\\'; o += ch; }
        else if (ch == '\n') o += "\\n";
        else if (static_cast<unsigned char>(ch) < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", ch); o += b; }
        else o += ch;
    }
    return o;
}

} // namespace cs
