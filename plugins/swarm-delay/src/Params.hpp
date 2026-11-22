// Swarm Delay - shared parameter definitions, effective per-voice values, presets.
// Used by both the DSP engine and the UI (so the graph shows exactly what the DSP does).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace swd {

enum { kNumVoices = 6 };

enum GlobalParam {
    gMix, gOutput, gTimeMul, gFbMul, gWarmth,
    gTimeMode, gPingPong, gFreeze,
    gSpTime, gSpDetune, gSpPan,
    gChDetune, gChRate, gChMix,
    gBits, gCrRate, gCrLoop,
    gLoCut, gHiCut,
    gGlProb, gGlRev, gGlJump, gGlStut,
    gDuck, gDuckSens, gDuckRel,
    gSwMode, gSwDepth, gSwTime, gSwDiv, gSwSync, gSwGlide, gSwOneWay,
    kGlobalCount
};

enum VoiceParam {
    vOn, vLevel, vPan,
    vTime, vDiv, vFeedback, vRev,
    vPitch, vDetune, vCrush, vSync,
    kVoiceParamCount
};

enum {
    kInputCount = kGlobalCount + kNumVoices * kVoiceParamCount,
    kOutNow     = kInputCount,            // one per voice: current delay time in ms
    kOutState   = kOutNow + kNumVoices,   // one per voice: 0 normal, 1 reversed, 2 half-time jump, 3 stutter
    kOutDuck    = kOutState + kNumVoices, // how much the repeats are being ducked, 0..1
    kOutBpm,                              // host tempo
    kParamCount,
    kOutputCount = kParamCount - kInputCount
};

enum { kDivCount = 14 };
static const char* const kDivNames[kDivCount] = {
    "1/1", "1/2.", "1/2", "1/2T", "1/4.", "1/4", "1/4T", "1/8.", "1/8", "1/8T", "1/16.", "1/16", "1/16T", "1/32"
};
// delay time in quarter notes
static const float kDivBeats[kDivCount] = {
    4.f, 3.f, 2.f, 4.f / 3.f, 1.5f, 1.f, 2.f / 3.f, 0.75f, 0.5f, 1.f / 3.f, 0.375f, 0.25f, 1.f / 6.f, 0.125f
};

enum { kSweepModeCount = 3 };
static const char* const kSweepModeNames[kSweepModeCount] = { "Off", "Up", "Down" };

enum { kTimeModeCount = 2 };
static const char* const kTimeModeNames[kTimeModeCount] = { "Tape", "Jump" };

static const float kMaxTimeMs = 4000.f;

enum Flags { fLog = 1, fInt = 2, fBool = 4 };

struct Def {
    const char* name;
    const char* sym;
    const char* unit;
    float min, max, def;
    int flags;
};

static const Def kGlobalDefs[kGlobalCount] = {
    { "Mix",            "mix",        "%",   0.f,    100.f,   35.f,  0 },
    { "Output",         "output",     "dB",  -24.f,  12.f,    0.f,   0 },
    { "Time x",         "time_mul",   "x",   0.03f,  2.f,     1.f,   fLog },
    { "Feedback x",     "fb_mul",     "%",   0.f,    150.f,   100.f, 0 },
    { "Warmth",         "warmth",     "%",   0.f,    100.f,   0.f,   0 },
    { "Time mode",      "time_mode",  "",    0.f,    1.f,     0.f,   fInt },
    { "Ping-pong",      "pingpong",   "",    0.f,    1.f,     0.f,   fBool },
    { "Freeze",         "freeze",     "",    0.f,    1.f,     0.f,   fBool },
    { "Time spread",    "sp_time",    "%",   0.f,    100.f,   0.f,   0 },
    { "Detune spread",  "sp_detune",  "ct",  0.f,    50.f,    0.f,   0 },
    { "Pan spread",     "sp_pan",     "%",   0.f,    100.f,   0.f,   0 },
    { "Chorus detune",  "ch_detune",  "ct",  0.f,    50.f,    0.f,   0 },
    { "Chorus rate",    "ch_rate",    "Hz",  0.1f,   8.f,     0.6f,  fLog },
    { "Chorus mix",     "ch_mix",     "%",   0.f,    100.f,   50.f,  0 },
    { "Crush bits",     "bits",       "bit", 1.f,    16.f,    16.f,  0 },
    { "Crush rate",     "cr_rate",    "Hz",  200.f,  48000.f, 48000.f, fLog },
    { "Crush in loop",  "cr_loop",    "",    0.f,    1.f,     1.f,   fBool },
    { "Lo cut",         "lo_cut",     "Hz",  20.f,   2000.f,  20.f,  fLog },
    { "Hi cut",         "hi_cut",     "Hz",  500.f,  20000.f, 20000.f, fLog },
    { "Glitch chance",  "gl_prob",    "%",   0.f,    100.f,   0.f,   0 },
    { "Glitch reverse", "gl_rev",     "",    0.f,    1.f,     1.f,   fBool },
    { "Glitch jump",    "gl_jump",    "",    0.f,    1.f,     1.f,   fBool },
    { "Glitch stutter", "gl_stut",    "",    0.f,    1.f,     1.f,   fBool },
    { "Ducking",        "duck",       "%",   0.f,    100.f,   0.f,   0 },
    { "Duck sens",      "duck_sens",  "dB",  0.f,    40.f,    18.f,  0 },
    { "Duck release",   "duck_rel",   "ms",  50.f,   1500.f,  300.f, fLog },
    { "Sweep mode",     "sw_mode",    "",    0.f,    2.f,     0.f,   fInt },
    { "Sweep depth",    "sw_depth",   "%",   0.f,    95.f,    50.f,  0 },
    { "Sweep every",    "sw_time",    "ms",  50.f,   8000.f,  750.f, fLog },
    { "Sweep division", "sw_div",     "",    0.f,    float(kDivCount - 1), 7.f, fInt },
    { "Sweep sync",     "sw_sync",    "",    0.f,    1.f,     0.f,   fBool },
    { "Sweep glide",    "sw_glide",   "%",   5.f,    100.f,   40.f,  0 },
    { "Sweep one way",  "sw_oneway",  "",    0.f,    1.f,     0.f,   fBool },
};

static const Def kVoiceDefs[kVoiceParamCount] = {
    { "On",        "on",       "",    0.f,    1.f,    0.f,   fBool },
    { "Level",     "level",    "%",   0.f,    150.f,  100.f, 0 },
    { "Pan",       "pan",      "",    -100.f, 100.f,  0.f,   0 },
    { "Time",      "time",     "ms",  1.f,    2000.f, 350.f, fLog },
    { "Division",  "div",      "",    0.f,    float(kDivCount - 1), 5.f, fInt },
    { "Feedback",  "feedback", "%",   0.f,    100.f,  35.f,  0 },
    { "Reverse",   "reverse",  "",    0.f,    1.f,    0.f,   fBool },
    { "Pitch",     "pitch",    "st",  -12.f,  12.f,   0.f,   fInt },
    { "Detune",    "detune",   "ct",  -50.f,  50.f,   0.f,   0 },
    { "Crush",     "crush",    "%",   0.f,    100.f,  100.f, 0 },
    { "Note sync", "sync",     "",    0.f,    1.f,    0.f,   fBool },
};

inline int voiceIndex(int voice, int param) { return kGlobalCount + voice * kVoiceParamCount + param; }
inline bool isVoiceParam(int index) { return index >= kGlobalCount && index < kInputCount; }

inline const Def& defOf(int index)
{
    if (index < kGlobalCount) return kGlobalDefs[index];
    return kVoiceDefs[(index - kGlobalCount) % kVoiceParamCount];
}

inline float defaultOf(int index)
{
    // every voice starts on a different time, so switching one on adds a new tap straight away
    static const float kTimes[kNumVoices] = { 350.f, 525.f, 700.f, 262.5f, 175.f, 875.f };
    static const float kDivs[kNumVoices]  = { 5.f, 4.f, 2.f, 7.f, 8.f, 1.f };
    if (index < kGlobalCount) return kGlobalDefs[index].def;
    const int v = (index - kGlobalCount) / kVoiceParamCount;
    const int p = (index - kGlobalCount) % kVoiceParamCount;
    if (p == vOn) return v < 1 ? 1.f : 0.f;
    if (p == vTime) return kTimes[v];
    if (p == vDiv) return kDivs[v];
    return kVoiceDefs[p].def;
}

// "v3_time" style symbol, also used as the key in exported preset files
inline void symbolOf(int index, char* buf, size_t n)
{
    if (index < kGlobalCount) { std::snprintf(buf, n, "%s", kGlobalDefs[index].sym); return; }
    const int v = (index - kGlobalCount) / kVoiceParamCount;
    const int p = (index - kGlobalCount) % kVoiceParamCount;
    std::snprintf(buf, n, "v%d_%s", v + 1, kVoiceDefs[p].sym);
}

// --------------------------------------------------------------------------------------------------------------------
// Effective per-voice values after tempo sync, "Time x" and the global "spread" macros are applied

struct Eff {
    bool  on;
    bool  rev;
    bool  sync;    // time comes from a note value
    float timeMs;  // delay time
    float fb;      // 0..1.1
    float cents;   // pitch + detune
    float pan;     // -1..1
    float level;   // 0..1.5
    float factor;  // timeMs / the voice's own Time setting (used when dragging on the graph)
};

inline int computeEff(const float* p, Eff* e, float bpm)
{
    if (!(bpm > 1.f)) bpm = 120.f;
    int n = 0;
    for (int v = 0; v < kNumVoices; ++v)
        if (p[voiceIndex(v, vOn)] > 0.5f) ++n;

    int k = 0;
    for (int v = 0; v < kNumVoices; ++v)
    {
        const float* vp = p + voiceIndex(v, 0);
        const bool on = vp[vOn] > 0.5f;
        const float pos  = (on && n > 1) ? -1.f + 2.f * float(k) / float(n - 1) : 0.f;
        const float rank = on ? float(k) : 0.f;

        const int div = std::min(int(kDivCount) - 1, std::max(0, int(vp[vDiv] + 0.5f)));
        const bool sync = vp[vSync] > 0.5f;
        const float base = sync ? 60000.f / bpm * kDivBeats[div] : vp[vTime];
        const float factor = (1.f + p[gSpTime] * 0.01f * rank) * p[gTimeMul];

        e[v].on     = on;
        e[v].rev    = vp[vRev] > 0.5f;
        e[v].sync   = sync;
        e[v].factor = factor;
        e[v].timeMs = std::min(kMaxTimeMs, std::max(1.f, base * factor));
        e[v].fb     = std::min(1.1f, std::max(0.f, vp[vFeedback] * p[gFbMul] * 0.0001f));
        e[v].cents  = std::round(vp[vPitch]) * 100.f + vp[vDetune] + p[gSpDetune] * pos;
        e[v].pan    = std::min(1.f, std::max(-1.f, vp[vPan] * 0.01f + p[gSpPan] * 0.01f * pos));
        e[v].level  = vp[vLevel] * 0.01f;
        if (on) ++k;
    }
    return n;
}

// length of one time-sweep cycle in seconds
inline float sweepPeriod(const float* p, float bpm)
{
    if (!(bpm > 1.f)) bpm = 120.f;
    if (p[gSwSync] > 0.5f)
        return 60.f / bpm * kDivBeats[std::min(int(kDivCount) - 1, std::max(0, int(p[gSwDiv] + 0.5f)))];
    return p[gSwTime] * 0.001f;
}

// --------------------------------------------------------------------------------------------------------------------
// Factory presets

inline void presetDefaults(float* p)
{
    for (int i = 0; i < kInputCount; ++i) p[i] = defaultOf(i);
}

inline void setVoices(float* p, int count)
{
    for (int v = 0; v < kNumVoices; ++v) p[voiceIndex(v, vOn)] = v < count ? 1.f : 0.f;
}

inline void setAll(float* p, int param, float value)
{
    for (int v = 0; v < kNumVoices; ++v) p[voiceIndex(v, param)] = value;
}

struct Preset { const char* name; void (*fill)(float*); };

static const Preset kPresets[] = {
    { "Quarter Echo", [](float* p) {
        presetDefaults(p);
    } },
    { "Slapback", [](float* p) {
        presetDefaults(p);
        setAll(p, vTime, 115.f); setAll(p, vFeedback, 0.f);
        p[gMix] = 45.f; p[gWarmth] = 25.f;
    } },
    { "Dotted Eighth (synced)", [](float* p) {
        presetDefaults(p);
        setAll(p, vSync, 1.f); setAll(p, vDiv, 7.f); setAll(p, vFeedback, 42.f);
        p[gHiCut] = 6000.f;
    } },
    { "Ping-Pong", [](float* p) {
        presetDefaults(p);
        setAll(p, vTime, 300.f); setAll(p, vFeedback, 55.f);
        p[gPingPong] = 1.f; p[gHiCut] = 7000.f; p[gMix] = 40.f;
    } },
    { "Multi-Tap Swarm", [](float* p) {
        presetDefaults(p); setVoices(p, 6);
        setAll(p, vTime, 130.f); setAll(p, vFeedback, 20.f);
        p[gSpTime] = 62.f; p[gSpPan] = 100.f; p[gSpDetune] = 6.f; p[gMix] = 42.f;
    } },
    { "Tape Echo", [](float* p) {
        presetDefaults(p);
        setAll(p, vTime, 380.f); setAll(p, vFeedback, 55.f);
        p[gWarmth] = 60.f; p[gHiCut] = 3500.f; p[gLoCut] = 150.f;
        p[gChDetune] = 7.f; p[gChRate] = 0.5f; p[gChMix] = 100.f;
    } },
    { "Chorus Repeats", [](float* p) {
        presetDefaults(p); setVoices(p, 2);
        p[voiceIndex(0, vTime)] = 330.f; p[voiceIndex(1, vTime)] = 495.f;
        setAll(p, vFeedback, 40.f);
        p[gChDetune] = 16.f; p[gChRate] = 0.8f; p[gChMix] = 50.f; p[gSpPan] = 70.f;
    } },
    { "Reverse Swell", [](float* p) {
        presetDefaults(p); setVoices(p, 2);
        p[voiceIndex(0, vTime)] = 600.f; p[voiceIndex(1, vTime)] = 900.f;
        setAll(p, vRev, 1.f); setAll(p, vFeedback, 25.f);
        p[gMix] = 60.f; p[gSpPan] = 60.f; p[gHiCut] = 8000.f;
    } },
    { "Lo-Fi Crush", [](float* p) {
        presetDefaults(p);
        setAll(p, vTime, 290.f); setAll(p, vFeedback, 50.f);
        p[gBits] = 7.f; p[gCrRate] = 6000.f; p[gCrLoop] = 1.f; p[gLoCut] = 200.f;
    } },
    { "Shimmer", [](float* p) {
        presetDefaults(p); setVoices(p, 2);
        p[voiceIndex(0, vTime)] = 420.f; p[voiceIndex(1, vTime)] = 630.f;
        p[voiceIndex(0, vPitch)] = 12.f; p[voiceIndex(1, vPitch)] = 0.f;
        setAll(p, vFeedback, 58.f);
        p[gHiCut] = 5000.f; p[gSpPan] = 80.f; p[gMix] = 40.f; p[gChDetune] = 8.f;
    } },
    { "Octave Down Cascade", [](float* p) {
        presetDefaults(p);
        setAll(p, vTime, 450.f); setAll(p, vFeedback, 60.f); setAll(p, vPitch, -12.f);
        p[gWarmth] = 35.f; p[gMix] = 40.f;
    } },
    { "Ducked Lead", [](float* p) {
        presetDefaults(p); setVoices(p, 2);
        p[voiceIndex(0, vTime)] = 380.f; p[voiceIndex(1, vTime)] = 570.f;
        setAll(p, vFeedback, 45.f);
        p[gDuck] = 75.f; p[gMix] = 50.f; p[gSpPan] = 60.f; p[gHiCut] = 6000.f;
    } },
    { "Sweep Down (synced)", [](float* p) {
        presetDefaults(p);
        setAll(p, vSync, 1.f); setAll(p, vDiv, 7.f); setAll(p, vFeedback, 50.f);
        p[gSwMode] = 2.f; p[gSwSync] = 1.f; p[gSwDiv] = 2.f; p[gSwDepth] = 50.f; p[gSwGlide] = 30.f;
        p[gHiCut] = 7000.f; p[gMix] = 45.f;
    } },
    { "Sweep Up", [](float* p) {
        presetDefaults(p); setVoices(p, 2);
        p[voiceIndex(0, vTime)] = 300.f; p[voiceIndex(1, vTime)] = 450.f;
        setAll(p, vFeedback, 55.f);
        p[gSwMode] = 1.f; p[gSwTime] = 1200.f; p[gSwDepth] = 60.f; p[gSwGlide] = 50.f;
        p[gSpPan] = 70.f; p[gWarmth] = 30.f; p[gMix] = 45.f;
    } },
    { "One-Way Rise", [](float* p) {
        presetDefaults(p);
        setAll(p, vTime, 320.f); setAll(p, vFeedback, 55.f);
        p[gSwMode] = 2.f; p[gSwOneWay] = 1.f; p[gSwTime] = 2000.f; p[gSwDepth] = 60.f; p[gSwGlide] = 100.f;
        p[gHiCut] = 7000.f; p[gMix] = 45.f;
    } },
    { "One-Way Dive (synced)", [](float* p) {
        presetDefaults(p);
        setAll(p, vSync, 1.f); setAll(p, vDiv, 8.f); setAll(p, vFeedback, 50.f);
        p[gSwMode] = 1.f; p[gSwOneWay] = 1.f; p[gSwSync] = 1.f; p[gSwDiv] = 0.f; p[gSwDepth] = 80.f; p[gSwGlide] = 100.f;
        p[gWarmth] = 30.f; p[gMix] = 45.f;
    } },
    { "Glitch Garden", [](float* p) {
        presetDefaults(p); setVoices(p, 3);
        p[voiceIndex(0, vTime)] = 240.f; p[voiceIndex(1, vTime)] = 360.f; p[voiceIndex(2, vTime)] = 480.f;
        setAll(p, vFeedback, 50.f);
        p[gGlProb] = 45.f; p[gSpPan] = 100.f; p[gTimeMode] = 1.f; p[gMix] = 45.f;
    } },
    { "Broken Machine", [](float* p) {
        presetDefaults(p); setVoices(p, 3);
        p[voiceIndex(0, vTime)] = 180.f; p[voiceIndex(1, vTime)] = 270.f; p[voiceIndex(2, vTime)] = 405.f;
        p[voiceIndex(1, vRev)] = 1.f; p[voiceIndex(2, vPitch)] = -12.f;
        setAll(p, vFeedback, 62.f);
        p[gGlProb] = 70.f; p[gBits] = 5.f; p[gCrRate] = 9000.f; p[gTimeMode] = 1.f;
        p[gSpPan] = 100.f; p[gMix] = 50.f;
    } },
};

enum { kPresetCount = int(sizeof(kPresets) / sizeof(kPresets[0])) };

} // namespace swd
