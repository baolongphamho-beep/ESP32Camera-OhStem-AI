#pragma once
// Laptop -> camera /control -> camera UART -> YOLO UNO messages for the servo sweep.
//   BOX_<x1>_<y1>_<x2>_<y2>_<conf>   person box, normalized 0..1 (top-left origin), conf 0..100
//   NOBODY                           no person
//   MULTI                            two or more people of similar size
// Only letters, digits, '_' and '.' (';' separates /control fields). The camera may
// wrap it, e.g. "BOX_...;0.900" (label;probability): parseBodyLine() finds the token.
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <initializer_list>

enum class BodyMessageType { Invalid, Box, NoBody, Multi };

struct BodyMessage {
    BodyMessageType type = BodyMessageType::Invalid;
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0;  // normalized
    int confidence = 0;
};

inline BodyMessage parseBodyMessage(const char* token) {
    BodyMessage out;
    if (!token) return out;
    if (!std::strcmp(token, "NOBODY")) { out.type = BodyMessageType::NoBody; return out; }
    if (!std::strcmp(token, "MULTI")) { out.type = BodyMessageType::Multi; return out; }
    if (std::strncmp(token, "BOX_", 4) != 0) return out;
    const char* p = token + 4;
    float v[4];
    for (float& f : v) {
        if (!std::isdigit((unsigned char)*p)) return out;  // No sign, no spaces.
        char* end = nullptr;
        f = std::strtof(p, &end);
        if (end == p || *end != '_' || !std::isfinite(f) || f < 0 || f > 1) return out;
        p = end + 1;
    }
    if (!std::isdigit((unsigned char)*p)) return out;
    char* end = nullptr;
    const long conf = std::strtol(p, &end, 10);
    if (*end != '\0' || conf < 0 || conf > 100) return out;
    if (!(v[2] > v[0]) || !(v[3] > v[1])) return out;
    out.type = BodyMessageType::Box;
    out.x1 = v[0]; out.y1 = v[1]; out.x2 = v[2]; out.y2 = v[3];
    out.confidence = (int)conf;
    return out;
}

inline BodyMessage parseBodyLine(const char* line) {
    BodyMessage none;
    if (!line) return none;
    const size_t n = std::strlen(line);
    if (n > 159) return none;
    char up[160];
    for (size_t i = 0; i <= n; ++i) up[i] = (char)std::toupper((unsigned char)line[i]);
    const char* hit = nullptr;
    for (const char* key : {"NOBODY", "MULTI", "BOX_"}) {
        const char* p = std::strstr(up, key);
        if (p && (!hit || p < hit)) hit = p;
    }
    if (!hit) return none;
    if (hit > up && (std::isalnum((unsigned char)hit[-1]) || hit[-1] == '_')) return none;
    char token[48];
    size_t len = 0;
    while (hit[len] && hit[len] != ';' && hit[len] != ',' && !std::isspace((unsigned char)hit[len])) {
        if (len >= sizeof(token) - 1) return none;
        token[len] = hit[len];
        ++len;
    }
    token[len] = '\0';
    return parseBodyMessage(token);
}
