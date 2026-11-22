// Cleaver Slicer - shared definitions (parameters, pattern format, factory presets).
// Used by the DSP engine, the plugin wrapper and the UI.
#pragma once

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace cleaver {

constexpr int kMaxSteps = 32;

enum ParamId {
    pSync = 0,   // follow host tempo / position
    pBpm,        // internal tempo
    pRate,       // step length (enum)
    pSteps,      // pattern length
    pAttack,     // edge softness of each slice
    pDuty,       // slice length within the step
    pRelease,    // fade-out time of each slice
    pDepth,      // how far the gate closes
    pSwing,
    pMix,        // "Balance": direct <-> effect
    pOutput,     // dB
    pStereo,     // stereo mode (enum)
    pWidth,
    pFiltMode,   // Off / LP / BP / HP
    pReso,
    pHarm,       // level of pitch-shifted sound in pitched steps
    pRetrig,     // restart pattern on picked note
    pSens,       // retrigger threshold (dB)
    kNumInputParams,
    oStep = kNumInputParams, // output: current step
    oGate,                   // output: current gate gain
    kNumParams
};

enum { fFloat = 0, fInt = 1, fBool = 2, fEnum = 3 };

struct ParamDef {
    const char* name;
    const char* symbol;
    const char* unit;
    float min, max, def;
    int kind;
};

static const ParamDef kParams[kNumParams] = {
    {"Host Sync",   "sync",    "",    0,    1,    1,   fBool},
    {"Tempo",       "bpm",     "BPM", 40,   300,  120, fFloat},
    {"Rate",        "rate",    "",    0,    7,    5,   fEnum},
    {"Steps",       "steps",   "",    1,    32,   16,  fInt},
    {"Attack",      "attack",  "%",   0,    100,  5,   fFloat},
    {"Duty",        "duty",    "%",   5,    100,  50,  fFloat},
    {"Release",     "release", "ms",  1,    200,  6,   fFloat},
    {"Depth",       "depth",   "%",   0,    100,  100, fFloat},
    {"Swing",       "swing",   "%",   0,    100,  0,   fFloat},
    {"Balance",     "mix",     "%",   0,    100,  100, fFloat},
    {"Output",      "output",  "dB",  -24,  12,   0,   fFloat},
    {"Stereo Mode", "stereo",  "",    0,    4,    0,   fEnum},
    {"Width",       "width",   "%",   0,    100,  100, fFloat},
    {"Filter Mode", "filtmode","",    0,    3,    0,   fEnum},
    {"Resonance",   "reso",    "%",   0,    100,  35,  fFloat},
    {"Harmonic",    "harm",    "%",   0,    100,  100, fFloat},
    {"Retrigger",   "retrig",  "",    0,    1,    0,   fBool},
    {"Sensitivity", "sens",    "dB",  -60,  -6,   -30, fFloat},
    {"Step",        "outstep", "",    0,    31,   0,   fInt},
    {"Gate",        "outgate", "",    0,    1,    0,   fFloat},
};

constexpr int kNumRates = 8;
static const char* const kRateNames[kNumRates] = {"1/2", "1/4", "1/4T", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
// step length in quarter notes
static const double kRateQN[kNumRates] = {2.0, 1.0, 2.0 / 3.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125};

enum { stFixed = 0, stPattern, stPingPong, stRandom, stAuto, kNumStereo };
static const char* const kStereoNames[kNumStereo] = {"Fixed", "Pattern", "Ping-Pong", "Random", "Auto"};

enum { fmOff = 0, fmLP, fmBP, fmHP, kNumFiltModes };
static const char* const kFiltNames[kNumFiltModes] = {"Off", "Low-pass", "Band-pass", "High-pass"};

// ---------------------------------------------------------------------------------------------
// Pattern

struct Step {
    float level = 1.0f; // 0..1
    float pan = 0.0f;   // -1..1
    float filt = 1.0f;  // 0..1 (1 = open / bright)
    int pitch = 0;      // semitones, -12..+12
};

struct Pattern {
    Step s[kMaxSteps];
};

// Text format: "L:100,0,...;P:0,...;F:100,...;T:0,..."  (integers, 32 per lane)
inline std::string encodePattern(const Pattern& p)
{
    std::string out;
    char buf[16];
    const char lanes[4] = {'L', 'P', 'F', 'T'};
    for (int l = 0; l < 4; ++l) {
        out += lanes[l];
        out += ':';
        for (int i = 0; i < kMaxSteps; ++i) {
            int v = 0;
            switch (l) {
            case 0: v = (int)std::lround(p.s[i].level * 100.f); break;
            case 1: v = (int)std::lround(p.s[i].pan * 100.f); break;
            case 2: v = (int)std::lround(p.s[i].filt * 100.f); break;
            case 3: v = p.s[i].pitch; break;
            }
            std::snprintf(buf, sizeof(buf), i ? ",%d" : "%d", v);
            out += buf;
        }
        if (l < 3) out += ';';
    }
    return out;
}

inline bool decodePattern(const char* str, Pattern& p)
{
    if (str == nullptr || *str == 0) return false;
    Pattern tmp;
    bool any = false;
    const char* c = str;
    while (*c) {
        const char lane = *c;
        if (c[1] != ':') return false;
        c += 2;
        for (int i = 0; i < kMaxSteps; ++i) {
            char* end = nullptr;
            const long v = std::strtol(c, &end, 10);
            if (end == c) break;
            switch (lane) {
            case 'L': tmp.s[i].level = std::fmin(1.f, std::fmax(0.f, v / 100.f)); break;
            case 'P': tmp.s[i].pan = std::fmin(1.f, std::fmax(-1.f, v / 100.f)); break;
            case 'F': tmp.s[i].filt = std::fmin(1.f, std::fmax(0.f, v / 100.f)); break;
            case 'T': tmp.s[i].pitch = (int)(v < -12 ? -12 : v > 12 ? 12 : v); break;
            default: return false;
            }
            any = true;
            c = end;
            if (*c == ',') ++c; else break;
        }
        while (*c && *c != ';') ++c;
        if (*c == ';') ++c;
    }
    if (any) p = tmp;
    return any;
}

// ---------------------------------------------------------------------------------------------
// Factory presets. Lanes are given as compact strings, repeated to fill 32 steps.
//   level: one char per step, '0'..'9' -> 0..90 %, 'X' -> 100 %, '.' -> 0
//   pitch: space separated semitones;  filt / pan: space separated percents

struct Preset {
    const char* name;
    const char* level;
    const char* pitch; // may be nullptr
    const char* filt;  // may be nullptr
    const char* pan;   // may be nullptr
    int rate, steps;
    float attack, duty, release, swing;
    int stereo, filtMode;
    float reso;
};

static const Preset kPresets[] = {
    // name               level               pitch  filt   pan    rate st  att duty rel  swing stereo      filter reso
    {"Sixteenths",        "X",                nullptr, nullptr, nullptr, 5, 16, 5,  50, 6,  0,  stFixed,    fmOff, 35},
    {"Eighths",           "X.",               nullptr, nullptr, nullptr, 5, 16, 5,  100,8,  0,  stFixed,    fmOff, 35},
    {"Gallop",            "X.XX",             nullptr, nullptr, nullptr, 5, 16, 3,  60, 6,  0,  stFixed,    fmOff, 35},
    {"Reverse Gallop",    "XX.X",             nullptr, nullptr, nullptr, 5, 16, 3,  60, 6,  0,  stFixed,    fmOff, 35},
    {"Offbeat",           "..X.",             nullptr, nullptr, nullptr, 5, 16, 5,  100,10, 0,  stFixed,    fmOff, 35},
    {"Tresillo",          "X..X..X.",         nullptr, nullptr, nullptr, 5, 16, 4,  90, 8,  0,  stFixed,    fmOff, 35},
    {"Trance Gate",       "X.XX.XX.XX.XX.X.", nullptr, nullptr, nullptr, 5, 16, 4,  70, 6,  0,  stFixed,    fmOff, 35},
    {"Syncopated",        "X..X.X..X.X.X..X", nullptr, nullptr, nullptr, 5, 16, 4,  75, 6,  0,  stFixed,    fmOff, 35},
    {"Accents",           "X4648464",         nullptr, nullptr, nullptr, 5, 16, 6,  60, 6,  0,  stFixed,    fmOff, 35},
    {"Swell In",          "258X",             nullptr, nullptr, nullptr, 5, 16, 10, 80, 6,  0,  stFixed,    fmOff, 35},
    {"Decay",             "X852",             nullptr, nullptr, nullptr, 5, 16, 3,  80, 6,  0,  stFixed,    fmOff, 35},
    {"Shuffle",           "X.X",              nullptr, nullptr, nullptr, 4, 12, 5,  70, 8,  0,  stFixed,    fmOff, 35},
    {"Swung Sixteenths",  "X6",               nullptr, nullptr, nullptr, 5, 16, 5,  55, 6,  60, stFixed,    fmOff, 35},
    {"Soft Tremolo",      "X",                nullptr, nullptr, nullptr, 3, 8,  90, 50, 60, 0,  stFixed,    fmOff, 35},
    {"Stutter Roll",      "X.X.X.X.XXXXXXXXX...X...XXXX.X.X", nullptr, nullptr, nullptr, 7, 32, 2, 55, 3, 0, stFixed, fmOff, 35},
    {"Morse",             "X.XXX.X...XX.X..", nullptr, nullptr, nullptr, 5, 16, 2,  65, 4,  0,  stFixed,    fmOff, 35},
    {"Ping-Pong",         "X",                nullptr, nullptr, nullptr, 5, 16, 5,  55, 6,  0,  stPingPong, fmOff, 35},
    {"Stereo Scatter",    "X.XXX.X.XX.X.XX.", nullptr, nullptr, "-100 0 60 -40 100 0 -70 0 30 -100 0 80 0 -30 100 0", 5, 16, 4, 65, 6, 0, stPattern, fmOff, 35},
    {"Auto Pan Chop",     "X.XX",             nullptr, nullptr, nullptr, 5, 16, 5,  60, 6,  0,  stAuto,     fmOff, 35},
    {"Octave Jump",       "X",                "0 12", nullptr, nullptr, 5, 16, 4,  60, 6,  0,  stFixed,    fmOff, 35},
    {"Harmonic Arp",      "X",                "0 7 12 7 0 5 12 5", nullptr, nullptr, 5, 16, 4, 70, 8, 0, stFixed, fmOff, 35},
    {"Fifths Gallop",     "X.XX",             "0 0 7 12", nullptr, nullptr, 5, 16, 3, 60, 6, 0, stFixed, fmOff, 35},
    {"Sub Drop",          "X.XXX.X.",         "0 0 0 -12 0 0 -12 0", nullptr, nullptr, 5, 16, 4, 75, 8, 0, stFixed, fmOff, 35},
    {"Filter Climb",      "X",                nullptr, "20 30 40 50 60 70 80 95", nullptr, 5, 16, 4, 65, 6, 0, stFixed, fmLP, 55},
    {"Wah Gallop",        "X.XX",             nullptr, "35 35 70 90", nullptr, 5, 16, 3, 65, 6, 0, stFixed, fmBP, 60},
    {"Acid Steps",        "X.XXX.XXX.X.XX.X", nullptr, "30 30 80 45 60 60 25 90 40 40 70 70 35 85 85 55", nullptr, 5, 16, 3, 70, 6, 0, stFixed, fmLP, 75},
    {"Thin Slices",       "X.X.XX.X",         nullptr, "60 60 30 30 80 45 45 70", nullptr, 5, 16, 3, 55, 5, 0, stPingPong, fmHP, 30},
    {"Robot Talk",        "X.XX.XX.",         "0 0 12 0 0 -12 7 0", "40 40 85 55 55 30 70 70", nullptr, 5, 16, 3, 70, 6, 0, stPattern, fmBP, 65},
};
constexpr int kNumPresets = (int)(sizeof(kPresets) / sizeof(kPresets[0]));

inline void buildPresetPattern(const Preset& pr, Pattern& p)
{
    const int ll = (int)std::strlen(pr.level);
    for (int i = 0; i < kMaxSteps; ++i) {
        const char ch = pr.level[i % ll];
        p.s[i].level = ch == 'X' ? 1.f : (ch >= '0' && ch <= '9') ? (ch - '0') / 10.f : 0.f;
        p.s[i].pitch = 0;
        p.s[i].filt = 1.f;
        p.s[i].pan = 0.f;
    }
    auto lane = [&](const char* str, int which) {
        if (str == nullptr) return;
        float vals[kMaxSteps];
        int n = 0;
        const char* c = str;
        while (*c && n < kMaxSteps) {
            char* end = nullptr;
            const long v = std::strtol(c, &end, 10);
            if (end == c) break;
            vals[n++] = (float)v;
            c = end;
        }
        if (n == 0) return;
        for (int i = 0; i < kMaxSteps; ++i) {
            const float v = vals[i % n];
            if (which == 0) p.s[i].pitch = (int)v;
            else if (which == 1) p.s[i].filt = v / 100.f;
            else p.s[i].pan = v / 100.f;
        }
    };
    lane(pr.pitch, 0);
    lane(pr.filt, 1);
    lane(pr.pan, 2);
}

} // namespace cleaver
