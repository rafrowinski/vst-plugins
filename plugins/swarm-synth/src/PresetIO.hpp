// Swarm Synth - user preset library and CSV import/export
//
// CSV layout: one preset per row. First column is the preset name, the remaining columns are
// parameters; the header row holds the parameter symbols. Separator ';', decimal point '.'.
// The importer also accepts ',' or TAB as separator and (with ';' or TAB) decimal commas.
#pragma once

#include "../../../shared/guitar-engine/Params.hpp"

#include <charconv>
#include <cstdlib>
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

namespace swsyn {

struct UserPreset {
    std::string name;
    float v[kInputCount];
    std::string wave = kDefaultWave; // custom wave text, see Wave.hpp
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
    std::wstring dir = std::wstring(appdata) + L"\\Swarm Synth";
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
    dir += "/Swarm Synth";
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
    s += ";wave\r\n";
    for (const UserPreset& p : presets)
    {
        s += quoteCsv(p.name);
        for (int i = 0; i < kInputCount; ++i) { s += ';'; s += fmtFloat(p.v[i]); }
        s += ';'; s += quoteCsv(p.wave);
        s += "\r\n";
    }
    const bool ok = std::fwrite(s.data(), 1, s.size(), f) == s.size();
    return std::fclose(f) == 0 && ok;
}

enum { kColWave = -2 };

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
        if (name == "wave") { map[c] = kColWave; continue; }
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
            if (map[c] == kColWave)
            {
                std::vector<float> table(kWaveSize);
                const std::string w = trim(cells[c]);
                if (compileWave(w.c_str(), table.data())) p.wave = w;
            }
            else if (map[c] >= 0 && parseFloat(cells[c], decimalComma, val)) { p.v[map[c]] = val; ++good; }
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
    bool lookup(const std::string& name, float* v, std::string* wave = nullptr) const
    {
        for (int i = 0; i < kPresetCount; ++i)
            if (name == kPresets[i].name)
            {
                kPresets[i].fill(v);
                if (wave != nullptr) *wave = presetWave(i);
                return true;
            }
        const int u = findUser(name);
        if (u < 0) return false;
        std::memcpy(v, user[u].v, sizeof(user[u].v));
        if (wave != nullptr) *wave = user[u].wave;
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
        std::string existingWave;
        std::string name = in.name;
        for (int n = 2; lookup(name, existing, &existingWave); ++n)
        {
            if (sameValues(existing, in.v) && existingWave == in.wave) { ++rep.skipped; return; }
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

// --------------------------------------------------------------------------------------------------------------------
// single-cycle WAV file -> wave text ("w:..."). The whole file is taken as one cycle.

inline bool loadWavCycle(const char* path, std::string& text, std::string& err)
{
    FILE* const f = openUtf8(path, "rb");
    if (f == nullptr) { err = "could not open the file"; return false; }
    std::vector<unsigned char> d;
    unsigned char buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) { d.insert(d.end(), buf, buf + n); if (d.size() > (16u << 20)) break; }
    std::fclose(f);
    const auto u32 = [&d](size_t o) { return uint32_t(d[o]) | (uint32_t(d[o + 1]) << 8) | (uint32_t(d[o + 2]) << 16) | (uint32_t(d[o + 3]) << 24); };
    const auto u16 = [&d](size_t o) { return uint32_t(d[o]) | (uint32_t(d[o + 1]) << 8); };
    if (d.size() < 44 || std::memcmp(d.data(), "RIFF", 4) != 0 || std::memcmp(d.data() + 8, "WAVE", 4) != 0) { err = "not a WAV file"; return false; }

    uint32_t fmt = 0, ch = 0, bits = 0;
    size_t dataOff = 0, dataLen = 0;
    for (size_t o = 12; o + 8 <= d.size();)
    {
        const uint32_t len = u32(o + 4);
        if (std::memcmp(d.data() + o, "fmt ", 4) == 0 && o + 24 <= d.size())
        {
            fmt = u16(o + 8); ch = u16(o + 10); bits = u16(o + 22);
            if (fmt == 0xFFFE && len >= 26 && o + 34 <= d.size()) fmt = u16(o + 32); // WAVE_FORMAT_EXTENSIBLE
        }
        else if (std::memcmp(d.data() + o, "data", 4) == 0) { dataOff = o + 8; dataLen = std::min(size_t(len), d.size() - dataOff); break; }
        o += 8 + size_t(len) + (len & 1);
    }
    const uint32_t bytes = bits / 8;
    if (dataOff == 0 || ch == 0 || bytes == 0 || bytes > 4 || !(fmt == 1 || fmt == 3) || (fmt == 3 && bits != 32))
    { err = "unsupported WAV format (need PCM 8/16/24/32 bit or 32-bit float)"; return false; }
    const size_t frames = dataLen / (size_t(bytes) * ch);
    if (frames < 8) { err = "the file is too short"; return false; }

    std::vector<float> x(frames);
    for (size_t i = 0; i < frames; ++i)
    {
        const size_t o = dataOff + i * bytes * ch; // first channel only
        float v;
        if (fmt == 3) { float fv; std::memcpy(&fv, d.data() + o, 4); v = fv; }
        else if (bytes == 1) v = (float(d[o]) - 128.f) / 128.f;
        else if (bytes == 2) v = float(int16_t(u16(o))) / 32768.f;
        else if (bytes == 3) v = float(int32_t((u32(o - 1) & 0xFFFFFF00u)) >> 8) / 8388608.f;
        else v = float(int32_t(u32(o))) / 2147483648.f;
        x[i] = std::isfinite(v) ? v : 0.f;
    }
    // resample the cycle to kWavSamples values (average when shrinking, interpolate when stretching)
    float cyc[kWavSamples];
    double mean = 0.0;
    for (int k = 0; k < kWavSamples; ++k)
    {
        const double a = double(k) * double(frames) / kWavSamples, b = double(k + 1) * double(frames) / kWavSamples;
        if (b - a >= 1.0)
        {
            double s = 0.0; int c = 0;
            for (size_t i = size_t(a); i < size_t(b) && i < frames; ++i) { s += x[i]; ++c; }
            cyc[k] = c > 0 ? float(s / c) : 0.f;
        }
        else
        {
            const size_t i0 = size_t(a) % frames, i1 = (i0 + 1) % frames;
            const float fr = float(a - std::floor(a));
            cyc[k] = x[i0] + (x[i1] - x[i0]) * fr;
        }
        mean += cyc[k];
    }
    mean /= kWavSamples;
    float peak = 0.f;
    for (float& v : cyc) { v -= float(mean); peak = std::max(peak, std::fabs(v)); }
    if (peak < 1e-6f) { err = "the file is silent"; return false; }
    for (float& v : cyc) v = 0.5f + 0.5f * v / peak;
    text = "w:" + encodeSamples(cyc);
    return true;
}

} // namespace swsyn
