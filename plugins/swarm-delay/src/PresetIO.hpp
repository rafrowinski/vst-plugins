// Swarm Delay - user preset library and CSV import/export
//
// CSV layout: one preset per row. First column is the preset name, the remaining columns are
// parameters; the header row holds the parameter symbols. Separator ';', decimal point '.'.
// The importer also accepts ',' or TAB as separator and (with ';' or TAB) decimal commas.
#pragma once

#include "Params.hpp"

#include <charconv>
#include <string>
#include <vector>

#ifdef _WIN32
# ifndef NOMINMAX
#  define NOMINMAX
# endif
# include <windows.h>
#else
# include <sys/stat.h>
# include <cstdlib>
#endif

namespace swd {

struct UserPreset {
    std::string name;
    float v[kInputCount];
};

struct ImportReport {
    int added = 0, renamed = 0, skipped = 0, badRows = 0;
    std::string firstName; // name (after renaming) of the first preset that was added
};

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

// where the user preset library lives between sessions
inline std::string libraryPath()
{
#ifdef _WIN32
    const wchar_t* const appdata = _wgetenv(L"APPDATA");
    if (appdata == nullptr) return std::string();
    std::wstring dir = std::wstring(appdata) + L"\\Swarm Delay";
    CreateDirectoryW(dir.c_str(), nullptr);
    dir += L"\\user-presets.csv";
    char buf[4096];
    if (WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, buf, sizeof(buf), nullptr, nullptr) == 0) return std::string();
    return buf;
#else
    const char* const home = std::getenv("HOME");
    if (home == nullptr) return std::string();
    std::string dir = std::string(home) + "/.config";
    mkdir(dir.c_str(), 0755);
    dir += "/Swarm Delay";
    mkdir(dir.c_str(), 0755);
    return dir + "/user-presets.csv";
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
    for (int i = 0; i < kInputCount; ++i)
        if (std::fabs(a[i] - b[i]) > 1e-4f * std::max(1.f, std::fabs(a[i]))) return false;
    return true;
}

inline void sanitize(float* v)
{
    for (int i = 0; i < kInputCount; ++i)
    {
        const Def& d = defOf(i);
        if (!std::isfinite(v[i])) v[i] = defaultOf(i);
        if (d.flags & (fInt | fBool)) v[i] = std::round(v[i]);
        v[i] = std::min(d.max, std::max(d.min, v[i]));
    }
}

inline bool writePresets(const char* path, const std::vector<UserPreset>& presets)
{
    FILE* const f = openUtf8(path, "wb");
    if (f == nullptr) return false;
    std::string s = "preset";
    char sym[32];
    for (int i = 0; i < kInputCount; ++i) { symbolOf(i, sym, sizeof(sym)); s += ';'; s += sym; }
    s += "\r\n";
    for (const UserPreset& p : presets)
    {
        s += quoteCsv(p.name);
        for (int i = 0; i < kInputCount; ++i) { s += ';'; s += fmtFloat(p.v[i]); }
        s += "\r\n";
    }
    const bool ok = std::fwrite(s.data(), 1, s.size(), f) == s.size();
    return std::fclose(f) == 0 && ok;
}

// returns false if the file can't be read or has no usable header
inline bool readPresets(const char* path, std::vector<UserPreset>& out, int* badRows = nullptr)
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
        // split on newlines that are not inside quotes
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
    char sym[32];
    for (size_t c = 1; c < cols.size(); ++c)
    {
        const std::string name = trim(cols[c]);
        for (int i = 0; i < kInputCount; ++i)
        {
            symbolOf(i, sym, sizeof(sym));
            if (name == sym) { map[c] = i; ++known; break; }
        }
    }
    if (known == 0) return false;

    for (++li; li < lines.size(); ++li)
    {
        if (trim(lines[li]).empty()) continue;
        const std::vector<std::string> cells = splitCsv(lines[li], sep);
        UserPreset p;
        p.name = trim(cells[0]);
        if (p.name.empty()) p.name = "Imported";
        if (p.name.size() > 60) p.name.resize(60);
        presetDefaults(p.v);
        int good = 0;
        for (size_t c = 1; c < cells.size() && c < map.size(); ++c)
        {
            float val;
            if (map[c] >= 0 && parseFloat(cells[c], decimalComma, val)) { p.v[map[c]] = val; ++good; }
        }
        if (good == 0) { if (badRows) ++*badRows; continue; }
        sanitize(p.v);
        out.push_back(p);
    }
    return true;
}

class Library
{
public:
    std::vector<UserPreset> user;

    int findUser(const std::string& name) const
    {
        for (size_t i = 0; i < user.size(); ++i) if (user[i].name == name) return int(i);
        return -1;
    }

    // values of a preset (factory or user) with this name, or false
    bool lookup(const std::string& name, float* v) const
    {
        for (int i = 0; i < kPresetCount; ++i)
            if (name == kPresets[i].name) { kPresets[i].fill(v); return true; }
        const int u = findUser(name);
        if (u < 0) return false;
        std::memcpy(v, user[u].v, sizeof(user[u].v));
        return true;
    }

    static bool isFactory(const std::string& name)
    {
        for (int i = 0; i < kPresetCount; ++i) if (name == kPresets[i].name) return true;
        return false;
    }

    // Adds a preset without ever overwriting an existing one:
    //  - same name and same settings already present -> skipped
    //  - same name, different settings -> stored as "Name (2)", "Name (3)", ...
    void addImported(const UserPreset& in, ImportReport& rep)
    {
        float existing[kInputCount];
        std::string name = in.name;
        for (int n = 2; lookup(name, existing); ++n)
        {
            if (sameValues(existing, in.v)) { ++rep.skipped; return; }
            name = in.name + " (" + std::to_string(n) + ")";
        }
        UserPreset p = in;
        p.name = name;
        user.push_back(p);
        if (name != in.name) ++rep.renamed;
        if (rep.added++ == 0) rep.firstName = name;
    }

    void load()
    {
        const std::string path = libraryPath();
        user.clear();
        if (path.empty()) return;
        std::vector<UserPreset> tmp;
        if (!readPresets(path.c_str(), tmp)) return;
        ImportReport rep;
        for (const UserPreset& p : tmp) addImported(p, rep);
    }

    bool save() const
    {
        const std::string path = libraryPath();
        return !path.empty() && writePresets(path.c_str(), user);
    }
};

} // namespace swd
