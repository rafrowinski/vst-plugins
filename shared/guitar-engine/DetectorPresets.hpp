// Saved detector settings, shared by Swarm Synth and Guitar to MIDI Live.
//
// Both plugins use the same note detector, so a setting tuned for one guitar works in either. The user's saved
// settings live in one file both plugins read and write:
//   Windows: %APPDATA%\Guitar Detector\detector-presets.csv     Linux/macOS: ~/.config/Guitar Detector/...
//
// CSV layout: one setting per row; first column is the name, the others are detector parameters, named in the
// header by the same symbols the plugins use (gate, cut, track, det_clarity, ...). Separator ';', decimal point.
// Import also takes ',' or TAB as separator, decimal commas (as Excel writes them), any column order, and missing
// columns (they get the plugin's current values).
//
// This file is identical in both repositories - change it in one, copy it to the other.
#pragma once

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#ifdef _WIN32
# ifndef NOMINMAX
#  define NOMINMAX
# endif
# include <windows.h>
#else
# include <sys/stat.h>
#endif

namespace detp {

enum { kCount = 12 };

struct Field { const char* sym; float min, max; bool isInt; };

// order of the values in every DetPreset (each plugin maps them to its own parameter indices)
static const Field kFields[kCount] = {
    { "gate",          -80.f, -20.f, false },
    { "cut",           -60.f, -6.f,  false },
    { "track",         0.5f,  2.f,   false },
    { "det_clarity",   50.f,  95.f,  false },
    { "det_legato",    2.f,   60.f,  false },
    { "det_low",       2.f,   80.f,  false },
    { "det_harm",      10.f,  120.f, false },
    { "det_probation", 0.f,   150.f, false },
    { "det_repick",    1.15f, 3.f,   false },
    { "det_refract",   15.f,  100.f, false },
    { "det_tail",      0.f,   60.f,  false },
    { "det_bridge",    0.f,   1.f,   true  },
};

struct DetPreset {
    std::string name;
    float v[kCount];
};

struct Report {
    int added = 0, renamed = 0, skipped = 0, badRows = 0;
    std::string firstName;
};

// ---------------------------------------------------------------------------------------------------------------------
// small helpers

inline FILE* openUtf8(const char* path, const char* mode)
{
#ifdef _WIN32
    wchar_t wpath[2048], wmode[8];
    if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 2048) == 0) return nullptr;
    MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, 8);
    return _wfopen(wpath, wmode);
#else
    return std::fopen(path, mode);
#endif
}

inline std::string libraryPath()
{
#ifdef _WIN32
    const wchar_t* const appdata = _wgetenv(L"APPDATA");
    if (appdata == nullptr) return std::string();
    std::wstring dir = std::wstring(appdata) + L"\\Guitar Detector";
    CreateDirectoryW(dir.c_str(), nullptr);
    dir += L"\\detector-presets.csv";
    char buf[4096];
    if (WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, buf, sizeof(buf), nullptr, nullptr) == 0) return std::string();
    return buf;
#else
    const char* const home = std::getenv("HOME");
    if (home == nullptr) return std::string();
    std::string dir = std::string(home) + "/.config";
    mkdir(dir.c_str(), 0755);
    dir += "/Guitar Detector";
    mkdir(dir.c_str(), 0755);
    return dir + "/detector-presets.csv";
#endif
}

inline std::string trim(const std::string& s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

inline std::string fmtFloat(float f)
{
    char b[64];
    const std::to_chars_result r = std::to_chars(b, b + sizeof(b), f, std::chars_format::fixed);
    return std::string(b, r.ptr);
}

inline bool parseFloat(std::string s, bool decimalComma, float& out)
{
    s = trim(s);
    if (decimalComma) for (char& c : s) if (c == ',') c = '.';
    if (!s.empty() && s[0] == '+') s.erase(0, 1);
    if (s.empty()) return false;
    const std::from_chars_result r = std::from_chars(s.data(), s.data() + s.size(), out);
    return r.ec == std::errc() && std::isfinite(out);
}

inline std::vector<std::string> splitCsv(const std::string& line, char sep)
{
    std::vector<std::string> out;
    std::string cur;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); ++i)
    {
        const char c = line[i];
        if (quoted)
        {
            if (c == '"') { if (i + 1 < line.size() && line[i + 1] == '"') { cur += '"'; ++i; } else quoted = false; }
            else cur += c;
        }
        else if (c == '"') quoted = true;
        else if (c == sep) { out.push_back(cur); cur.clear(); }
        else if (c != '\r' && c != '\n') cur += c;
    }
    out.push_back(cur);
    return out;
}

inline std::string quoteCsv(const std::string& s)
{
    if (s.find_first_of(";,\"\t\n") == std::string::npos) return s;
    std::string q = "\"";
    for (char c : s) { if (c == '"') q += '"'; q += c; }
    return q + "\"";
}

inline bool sameValues(const float* a, const float* b)
{
    for (int i = 0; i < kCount; ++i)
        if (std::fabs(a[i] - b[i]) > 1e-4f * std::max(1.f, std::fabs(a[i]))) return false;
    return true;
}

inline void sanitize(float* v, const float* fallback)
{
    for (int i = 0; i < kCount; ++i)
    {
        const Field& f = kFields[i];
        if (!std::isfinite(v[i])) v[i] = fallback[i];
        if (f.isInt) v[i] = std::round(v[i]);
        v[i] = std::min(f.max, std::max(f.min, v[i]));
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// files

inline bool writePresets(const char* path, const std::vector<DetPreset>& presets)
{
    FILE* const f = openUtf8(path, "wb");
    if (f == nullptr) return false;
    std::string s = "name";
    for (const Field& fl : kFields) { s += ';'; s += fl.sym; }
    s += "\r\n";
    for (const DetPreset& p : presets)
    {
        s += quoteCsv(p.name);
        for (int i = 0; i < kCount; ++i) { s += ';'; s += fmtFloat(p.v[i]); }
        s += "\r\n";
    }
    const bool ok = std::fwrite(s.data(), 1, s.size(), f) == s.size();
    return std::fclose(f) == 0 && ok;
}

// fallback: values for columns the file does not have. Returns false if the file can't be read or has no
// detector column at all (for example a full Swarm Synth preset file is fine: its detector columns are used).
inline bool readPresets(const char* path, std::vector<DetPreset>& out, const float* fallback, int* badRows = nullptr)
{
    FILE* const f = openUtf8(path, "rb");
    if (f == nullptr) return false;
    std::string data;
    char buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) { data.append(buf, n); if (data.size() > (8u << 20)) break; }
    std::fclose(f);
    if (data.compare(0, 3, "\xEF\xBB\xBF") == 0) data.erase(0, 3);

    std::vector<std::string> lines;
    {
        std::string cur; bool quoted = false;
        for (char c : data)
        {
            if (c == '"') quoted = !quoted;
            if (c == '\n' && !quoted) { lines.push_back(cur); cur.clear(); } else cur += c;
        }
        if (!trim(cur).empty()) lines.push_back(cur);
    }
    size_t li = 0;
    while (li < lines.size() && trim(lines[li]).empty()) ++li;
    if (li >= lines.size()) return false;

    const std::string& header = lines[li];
    int cs = 0, cc = 0, ct = 0;
    for (char c : header) { cs += c == ';'; cc += c == ','; ct += c == '\t'; }
    const char sep = (cs >= cc && cs >= ct) ? ';' : (ct >= cc ? '\t' : ',');
    const bool decimalComma = sep != ',';

    const std::vector<std::string> cols = splitCsv(header, sep);
    std::vector<int> map(cols.size(), -1);
    int known = 0;
    for (size_t c = 1; c < cols.size(); ++c)
    {
        const std::string name = trim(cols[c]);
        for (int i = 0; i < kCount; ++i)
            if (name == kFields[i].sym) { map[c] = i; ++known; break; }
    }
    if (known == 0) return false;

    for (++li; li < lines.size(); ++li)
    {
        if (trim(lines[li]).empty()) continue;
        const std::vector<std::string> cells = splitCsv(lines[li], sep);
        DetPreset p;
        p.name = trim(cells[0]);
        if (p.name.empty()) p.name = "Imported";
        if (p.name.size() > 60) p.name.resize(60);
        std::memcpy(p.v, fallback, sizeof(p.v));
        int good = 0;
        for (size_t c = 1; c < cells.size() && c < map.size(); ++c)
        {
            float val;
            if (map[c] >= 0 && parseFloat(cells[c], decimalComma, val)) { p.v[map[c]] = val; ++good; }
        }
        if (good == 0) { if (badRows) ++*badRows; continue; }
        sanitize(p.v, fallback);
        out.push_back(p);
    }
    return true;
}

// ---------------------------------------------------------------------------------------------------------------------
// the user's saved settings. Every change re-reads the file first, so two plugins open at the same time
// don't wipe out each other's settings.

class Library
{
    std::vector<std::string> fReserved;   // names of the plugin's built-in profiles

public:
    std::vector<DetPreset> user;
    std::string path;                      // empty = use the shared file (tests can point it elsewhere)

    explicit Library(const std::vector<std::string>& reserved = {}) : fReserved(reserved) {}

    std::string file() const { return path.empty() ? libraryPath() : path; }

    bool isReserved(const std::string& name) const
    {
        return std::find(fReserved.begin(), fReserved.end(), name) != fReserved.end();
    }

    int find(const std::string& name) const
    {
        for (size_t i = 0; i < user.size(); ++i) if (user[i].name == name) return int(i);
        return -1;
    }

    // first saved setting with exactly these values, or -1
    int match(const float* v) const
    {
        for (size_t i = 0; i < user.size(); ++i) if (sameValues(user[i].v, v)) return int(i);
        return -1;
    }

    void load()
    {
        user.clear();
        const std::string f = file();
        if (f.empty()) return;
        std::vector<DetPreset> tmp;
        float fallback[kCount];
        for (int i = 0; i < kCount; ++i) fallback[i] = 0.5f * (kFields[i].min + kFields[i].max);
        if (!readPresets(f.c_str(), tmp, fallback)) return;
        Report rep;
        for (const DetPreset& p : tmp) addImported(p, rep);
    }

    bool save() const
    {
        const std::string f = file();
        return !f.empty() && writePresets(f.c_str(), user);
    }

    // stores (or replaces) a setting under this name
    bool store(const std::string& name, const float* v)
    {
        load();
        DetPreset p;
        p.name = name;
        std::memcpy(p.v, v, sizeof(p.v));
        const int i = find(name);
        if (i >= 0) user[i] = p; else user.push_back(p);
        return save();
    }

    bool remove(const std::string& name)
    {
        load();
        const int i = find(name);
        if (i < 0) return false;
        user.erase(user.begin() + i);
        return save();
    }

    // Adds without overwriting: same name and same values -> skipped; same name (or a built-in profile's name)
    // with other values -> "Name (2)", "Name (3)", ...
    void addImported(const DetPreset& in, Report& rep)
    {
        std::string name = in.name;
        for (int n = 2;; ++n)
        {
            const int i = find(name);
            if (i >= 0 && sameValues(user[i].v, in.v)) { ++rep.skipped; return; }
            if (i < 0 && !isReserved(name)) break;
            name = in.name + " (" + std::to_string(n) + ")";
        }
        DetPreset p = in;
        p.name = name;
        user.push_back(p);
        if (name != in.name) ++rep.renamed;
        if (rep.added++ == 0) rep.firstName = name;
    }

    // imports a CSV file into the saved settings and stores them
    bool importFile(const char* csv, const float* fallback, Report& rep)
    {
        std::vector<DetPreset> in;
        if (!readPresets(csv, in, fallback, &rep.badRows)) return false;
        load();
        for (const DetPreset& p : in) addImported(p, rep);
        if (rep.added > 0) save();
        return true;
    }
};

} // namespace detp
