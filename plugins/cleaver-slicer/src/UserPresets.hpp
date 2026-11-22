// Cleaver Slicer - user presets stored in a plain text file.
//   Windows: %APPDATA%\Cleaver Slicer\user-presets.txt
//   Linux:   ~/.config/Cleaver Slicer/user-presets.txt
// One preset per line, tab separated:  name <TAB> symbol=value <TAB> ... <TAB> pattern=L:..;P:..;F:..;T:..
// Host sync and tempo are not part of a preset (they belong to the project).
#pragma once

#include "SlicerShared.hpp"

#include <string>
#include <vector>

#ifdef _WIN32
#include <direct.h>
#include <wchar.h>
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace cleaver {

struct UserPreset {
    std::string name;
    float par[kNumInputParams];
    Pattern pat;
};

inline bool presetStoresParam(int pid) { return pid != pSync && pid != pBpm; }

#ifdef _WIN32
inline std::wstring presetDirW()
{
    const wchar_t* base = _wgetenv(L"APPDATA");
    std::wstring d = base ? base : L".";
    d += L"\\Cleaver Slicer";
    return d;
}
inline std::string narrow(const std::wstring& w)
{
    if (w.empty()) return std::string();
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s((size_t)n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
inline std::string presetFilePath() { return narrow(presetDirW() + L"\\user-presets.txt"); }
inline FILE* openPresetFile(bool write)
{
    const std::wstring dir = presetDirW();
    if (write) _wmkdir(dir.c_str());
    return _wfopen((dir + L"\\user-presets.txt").c_str(), write ? L"wb" : L"rb");
}
#else
inline std::string presetDir()
{
    const char* xdg = std::getenv("XDG_CONFIG_HOME");
    const char* home = std::getenv("HOME");
    std::string d = (xdg && *xdg) ? std::string(xdg) : std::string(home ? home : ".") + "/.config";
    return d + "/Cleaver Slicer";
}
inline std::string presetFilePath() { return presetDir() + "/user-presets.txt"; }
inline FILE* openPresetFile(bool write)
{
    if (write) {
        const std::string dir = presetDir();
        const size_t slash = dir.find_last_of('/');
        if (slash != std::string::npos) mkdir(dir.substr(0, slash).c_str(), 0755);
        mkdir(dir.c_str(), 0755);
    }
    return std::fopen(presetFilePath().c_str(), write ? "wb" : "rb");
}
#endif

// Trims and removes characters that would break the file format.
inline std::string cleanPresetName(const char* raw)
{
    std::string s;
    for (const char* c = raw; *c; ++c)
        s += (*c == '\t' || *c == '\n' || *c == '\r') ? ' ' : *c;
    while (!s.empty() && s.back() == ' ') s.pop_back();
    while (!s.empty() && s.back() == '*') { s.pop_back(); while (!s.empty() && s.back() == ' ') s.pop_back(); }
    size_t b = 0;
    while (b < s.size() && s[b] == ' ') ++b;
    return s.substr(b);
}

inline std::string serializePreset(const UserPreset& u)
{
    std::string line = u.name;
    char buf[64];
    for (int i = 0; i < kNumInputParams; ++i) {
        if (!presetStoresParam(i)) continue;
        std::snprintf(buf, sizeof(buf), "\t%s=%.4g", kParams[i].symbol, (double)u.par[i]);
        line += buf;
    }
    line += "\tpattern=";
    line += encodePattern(u.pat);
    return line;
}

inline bool parsePreset(const std::string& line, UserPreset& u)
{
    size_t pos = line.find('\t');
    if (pos == std::string::npos || pos == 0) return false;
    u.name = cleanPresetName(line.substr(0, pos).c_str());
    if (u.name.empty()) return false;
    for (int i = 0; i < kNumInputParams; ++i) u.par[i] = kParams[i].def;
    bool gotPattern = false;
    while (pos != std::string::npos) {
        const size_t next = line.find('\t', pos + 1);
        const std::string field = line.substr(pos + 1, next == std::string::npos ? std::string::npos : next - pos - 1);
        const size_t eq = field.find('=');
        if (eq != std::string::npos) {
            const std::string key = field.substr(0, eq), val = field.substr(eq + 1);
            if (key == "pattern") {
                gotPattern = decodePattern(val.c_str(), u.pat);
            } else {
                for (int i = 0; i < kNumInputParams; ++i) {
                    if (key == kParams[i].symbol) {
                        // accept both '.' and ',' as decimal separator (file may be edited by hand)
                        std::string v = val;
                        for (char& ch : v) if (ch == ',') ch = '.';
                        char* end = nullptr;
                        // locale independent parse
                        double sign = 1.0, num = 0.0, scale = 1.0;
                        const char* c = v.c_str();
                        if (*c == '-') { sign = -1.0; ++c; } else if (*c == '+') ++c;
                        bool any = false, frac = false;
                        for (; *c; ++c) {
                            if (*c >= '0' && *c <= '9') { any = true; num = num * 10.0 + (*c - '0'); if (frac) scale *= 10.0; }
                            else if (*c == '.' && !frac) frac = true;
                            else break;
                        }
                        (void)end;
                        if (any) {
                            float f = (float)(sign * num / scale);
                            f = f < kParams[i].min ? kParams[i].min : f > kParams[i].max ? kParams[i].max : f;
                            u.par[i] = f;
                        }
                        break;
                    }
                }
            }
        }
        pos = next;
    }
    return gotPattern;
}

inline void loadUserPresets(std::vector<UserPreset>& out)
{
    out.clear();
    FILE* f = openPresetFile(false);
    if (f == nullptr) return;
    std::string data;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) data.append(buf, n);
    std::fclose(f);
    size_t start = 0;
    if (data.compare(0, 3, "\xEF\xBB\xBF") == 0) start = 3; // BOM added by some editors
    while (start < data.size()) {
        size_t end = data.find('\n', start);
        if (end == std::string::npos) end = data.size();
        std::string line = data.substr(start, end - start);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        UserPreset u;
        if (!line.empty() && line[0] != '#' && parsePreset(line, u)) {
            bool dup = false;
            for (const UserPreset& e : out) if (e.name == u.name) dup = true;
            if (!dup) out.push_back(u);
        }
        start = end + 1;
    }
}

inline bool saveUserPresets(const std::vector<UserPreset>& list)
{
    FILE* f = openPresetFile(true);
    if (f == nullptr) return false;
    std::string data = "# Cleaver Slicer user presets - one preset per line, fields separated by TAB\r\n";
    for (const UserPreset& u : list) data += serializePreset(u) + "\r\n";
    const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    return (std::fclose(f) == 0) && ok;
}

inline int findUserPreset(const std::vector<UserPreset>& list, const std::string& name)
{
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i].name == name) return (int)i;
    return -1;
}

} // namespace cleaver
