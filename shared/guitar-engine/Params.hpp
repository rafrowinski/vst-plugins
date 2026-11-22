// Swarm Synth - shared parameter definitions, oscillator shapes, presets.
// Used by both the DSP engine and the UI.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "Wave.hpp"

namespace swsyn {

enum { kNumVoices = 8 };

enum GlobalParam {
    gMode, gMix, gOutput, gGate, gCut, gTrack, gGlide, gMidiOut, gQuant, gHold, gVibDepth, gVibRate,
    gSpDetune, gSpPan, gDrift,
    gEnvMode, gAttack, gDecay, gSustain, gRelease, gDyn, gComp,
    gFltOn, gCutoff, gReso, gSlope, gDrive, gFltEnv, gFltAtk, gFltDec, gKey, gVelFlt,
    // detector fine-tuning (the "Detector..." window)
    gDetClarity, gDetLegato, gDetLow, gDetHarm, gDetProbation, gDetRepick, gDetRefract, gDetTail, gDetBridge,
    kGlobalCount
};

enum VoiceParam {
    vOn, vLevel, vPan, vWave, vOct, vSemi, vDetune, vWidth,
    kVoiceParamCount
};

// parameters added later sit after the voices, so the host indices of older ones stay the same
enum LateParam {
    kVoicesEnd = kGlobalCount + kNumVoices * kVoiceParamCount,
    gScatter = kVoicesEnd,   // each new note starts with the voices scattered by up to this many cents...
    gConverge,               // ...and they come together over this time
    kLateEnd
};
enum { kLateCount = kLateEnd - kVoicesEnd };

enum {
    kInputCount = kLateEnd,
    kOutFreq    = kInputCount,   // pitch the synth is playing, Hz (0 = nothing)
    kOutEnv,                     // input level 0..1
    kOutLatency,                 // how long the last note took to be recognised, ms
    kOutGate,                    // 1 while a note is held
    kParamCount,
    kOutputCount = kParamCount - kInputCount
};

enum Mode { mTrack, mReshape, kModeCount };
static const char* const kModeNames[kModeCount] = { "Pitch tracking", "Reshape (instant)" };

enum EnvMode { eFollow, eAdsr, kEnvModeCount };
static const char* const kEnvModeNames[kEnvModeCount] = { "Follow guitar", "Synth ADSR" };

enum Wave { wSine, wTriangle, wSaw, wPulse, wWhite, wPink, wCustom, kWaveCount };
static const char* const kWaveNames[kWaveCount] = {
    "Sine", "Triangle", "Saw", "Square / pulse", "White noise", "Pink noise", "Custom wave"
};

static const char* const kSlopeNames[4] = { "6 dB", "12 dB", "18 dB", "24 dB" };

enum Flags { fLog = 1, fInt = 2, fBool = 4 };

struct Def {
    const char* name;
    const char* sym;
    const char* unit;
    float min, max, def;
    int flags;
};

static const Def kGlobalDefs[kGlobalCount] = {
    { "Mode",           "mode",       "",    0.f,    1.f,     0.f,    fInt },
    { "Mix",            "mix",        "%",   0.f,    100.f,   100.f,  0 },
    { "Output",         "output",     "dB",  -24.f,  12.f,    0.f,    0 },
    { "Gate",           "gate",       "dB",  -80.f,  -20.f,   -52.f,  0 },
    { "Cut",            "cut",        "dB",  -60.f,  -6.f,    -30.f,  0 },
    { "Tracking window","track",      "per", 0.5f,   2.f,     1.f,    0 },
    { "Glide",          "glide",      "ms",  0.f,    500.f,   0.f,    0 },
    { "MIDI out",       "midi_out",   "",    0.f,    1.f,     0.f,    fBool },
    { "Quantize",       "quantize",   "",    0.f,    1.f,     0.f,    fBool },
    { "Hold",           "hold",       "",    0.f,    1.f,     0.f,    fBool },
    { "Vibrato depth",  "vib_depth",  "ct",  0.f,    100.f,   0.f,    0 },
    { "Vibrato rate",   "vib_rate",   "Hz",  0.5f,   12.f,    5.5f,   0 },
    { "Detune spread",  "sp_detune",  "ct",  0.f,    50.f,    12.f,   0 },
    { "Pan spread",     "sp_pan",     "%",   0.f,    100.f,   80.f,   0 },
    { "Drift",          "drift",      "ct",  0.f,    50.f,    0.f,    0 },
    { "Envelope mode",  "env_mode",   "",    0.f,    1.f,     0.f,    fInt },
    { "Attack",         "attack",     "ms",  0.5f,   2000.f,  3.f,    fLog },
    { "Decay",          "decay",      "ms",  5.f,    5000.f,  400.f,  fLog },
    { "Sustain",        "sustain",    "%",   0.f,    100.f,   70.f,   0 },
    { "Release",        "release",    "ms",  5.f,    5000.f,  150.f,  fLog },
    { "Dynamics",       "dynamics",   "%",   0.f,    100.f,   50.f,   0 },
    { "Compress",       "compress",   "%",   0.f,    100.f,   30.f,   0 },
    { "Filter",         "flt_on",     "",    0.f,    1.f,     1.f,    fBool },
    { "Cutoff",         "cutoff",     "Hz",  40.f,   18000.f, 1800.f, fLog },
    { "Resonance",      "reso",       "%",   0.f,    100.f,   25.f,   0 },
    { "Slope",          "slope",      "",    0.f,    3.f,     3.f,    fInt },
    { "Drive",          "drive",      "dB",  0.f,    36.f,    0.f,    0 },
    { "Filter env",     "flt_env",    "oct", -5.f,   5.f,     2.f,    0 },
    { "Filter attack",  "flt_atk",    "ms",  0.5f,   2000.f,  2.f,    fLog },
    { "Filter decay",   "flt_dec",    "ms",  5.f,    5000.f,  350.f,  fLog },
    { "Key tracking",   "key",        "%",   0.f,    100.f,   50.f,   0 },
    { "Velocity to cutoff", "vel_flt", "oct", 0.f,   4.f,     0.f,    0 },
    { "Det clarity",    "det_clarity","%",   50.f,   95.f,    80.f,   0 },
    { "Det legato time","det_legato", "ms",  2.f,    60.f,    12.5f,  0 },
    { "Det low note time","det_low",  "ms",  2.f,    80.f,    30.f,   0 },
    { "Det octave time","det_harm",   "ms",  10.f,   120.f,   40.f,   0 },
    { "Det first-reading time","det_probation","ms", 0.f, 150.f, 60.f, 0 },
    { "Det pick sensitivity","det_repick","x", 1.15f, 3.f,    1.5f,   0 },
    { "Det pick spacing","det_refract","ms", 15.f,   100.f,   40.f,   0 },
    { "Det tail hold",  "det_tail",   "%",   0.f,    60.f,    25.f,   0 },
    { "Det bridge notes","det_bridge","",    0.f,    1.f,     1.f,    fBool },
};

static const Def kVoiceDefs[kVoiceParamCount] = {
    { "On",        "on",       "",    0.f,    1.f,    0.f,   fBool },
    { "Level",     "level",    "%",   0.f,    150.f,  100.f, 0 },
    { "Pan",       "pan",      "",    -100.f, 100.f,  0.f,   0 },
    { "Wave",      "wave",     "",    0.f,    float(kWaveCount - 1), float(wSaw), fInt },
    { "Octave",    "oct",      "oct", -2.f,   2.f,    0.f,   fInt },
    { "Semitones", "semi",     "st",  -12.f,  12.f,   0.f,   fInt },
    { "Detune",    "detune",   "ct",  -50.f,  50.f,   0.f,   0 },
    { "Width",     "width",    "%",   5.f,    95.f,   50.f,  0 },
};

static const Def kLateDefs[kLateCount] = {
    { "Scatter",   "scatter",  "ct",  0.f,    1200.f, 0.f,    0 },
    { "Converge",  "converge", "ms",  50.f,   20000.f, 4000.f, fLog },
};

inline int voiceIndex(int voice, int param) { return kGlobalCount + voice * kVoiceParamCount + param; }
inline bool isVoiceParam(int index) { return index >= kGlobalCount && index < kVoicesEnd; }

inline const Def& defOf(int index)
{
    if (index < kGlobalCount) return kGlobalDefs[index];
    if (index >= kVoicesEnd) return kLateDefs[index - kVoicesEnd];
    return kVoiceDefs[(index - kGlobalCount) % kVoiceParamCount];
}

inline float defaultOf(int index)
{
    if (index < kGlobalCount) return kGlobalDefs[index].def;
    if (index >= kVoicesEnd) return kLateDefs[index - kVoicesEnd].def;
    const int v = (index - kGlobalCount) / kVoiceParamCount;
    const int p = (index - kGlobalCount) % kVoiceParamCount;
    if (p == vOn) return v < 3 ? 1.f : 0.f;
    return kVoiceDefs[p].def;
}

// "v3_detune" style symbol, also used as the key in exported preset files
inline void symbolOf(int index, char* buf, size_t n)
{
    if (index < kGlobalCount) { std::snprintf(buf, n, "%s", kGlobalDefs[index].sym); return; }
    if (index >= kVoicesEnd) { std::snprintf(buf, n, "%s", kLateDefs[index - kVoicesEnd].sym); return; }
    const int v = (index - kGlobalCount) / kVoiceParamCount;
    const int p = (index - kGlobalCount) % kVoiceParamCount;
    std::snprintf(buf, n, "v%d_%s", v + 1, kVoiceDefs[p].sym);
}

// --------------------------------------------------------------------------------------------------------------------
// Plain (not band-limited) oscillator shapes, -1..+1. The UI draws these; the engine uses band-limited versions.
// ph: 0..1, width: 0..1 (pulse only), table: compiled custom wave (kWaveSize values 0..1)

inline float hash01(uint32_t a, uint32_t n)
{
    uint32_t x = (a + 1u) * 0x9E3779B1u ^ (n + 1u) * 0x85EBCA77u;
    x ^= x >> 15; x *= 0x2C1B3C6Du;
    x ^= x >> 12; x *= 0x297A2D39u;
    x ^= x >> 15;
    return float(x >> 8) * (1.0f / 16777216.0f);
}

inline float oscNaive(int wave, float ph, float width, const float* table, int seed = 0)
{
    ph -= std::floor(ph);
    switch (wave)
    {
    default:
    case wSine:     return std::sin(6.28318531f * ph);
    case wTriangle: return ph < 0.25f ? 4.f * ph : (ph < 0.75f ? 2.f - 4.f * ph : 4.f * ph - 4.f);
    case wSaw:      return 2.f * ph - 1.f;
    case wPulse:    return ph < width ? 1.f : -1.f;
    case wWhite:    return 2.f * hash01(uint32_t(seed), uint32_t(ph * 4096.f)) - 1.f;
    case wPink: {
        // a rough picture of pink noise: a few octaves of held random values
        float s = 0.f;
        for (int o = 0; o < 5; ++o) s += (2.f * hash01(uint32_t(seed + 7 * o), uint32_t(ph * float(8 << o))) - 1.f) / float(o + 1);
        return s * 0.45f;
    }
    case wCustom: {
        if (table == nullptr) return 0.f;
        const float x = ph * float(kWaveSize);
        const int i = int(x) & (kWaveSize - 1);
        const float f = x - std::floor(x);
        return 2.f * (table[i] + (table[(i + 1) & (kWaveSize - 1)] - table[i]) * f) - 1.f;
    }
    }
}

// --------------------------------------------------------------------------------------------------------------------
// Effective per-voice values after the global "spread" macros are applied

struct Eff {
    bool  on;
    float ratio;   // frequency relative to the played note
    float cents;   // detune after spread
    float pan;     // -1..1
};

inline int computeEff(const float* p, Eff* e)
{
    int n = 0;
    for (int v = 0; v < kNumVoices; ++v)
        if (p[voiceIndex(v, vOn)] > 0.5f) ++n;

    int k = 0;
    for (int v = 0; v < kNumVoices; ++v)
    {
        const float* vp = p + voiceIndex(v, 0);
        const bool on = vp[vOn] > 0.5f;
        const float pos = (on && n > 1) ? -1.f + 2.f * float(k) / float(n - 1) : 0.f;
        // detune is dealt out alternately from both ends (lowest, highest, 2nd lowest...),
        // so voices next to each other in the stereo field are far apart in pitch
        float dpos = 0.f;
        if (on && n > 1)
        {
            const int j = (k % 2 == 0) ? k / 2 : n - 1 - (k - 1) / 2;
            dpos = -1.f + 2.f * float(j) / float(n - 1);
        }
        e[v].on    = on;
        e[v].cents = vp[vDetune] + p[gSpDetune] * dpos;
        e[v].ratio = std::pow(2.f, std::round(vp[vOct]) + std::round(vp[vSemi]) / 12.f + e[v].cents / 1200.f);
        e[v].pan   = std::min(1.f, std::max(-1.f, vp[vPan] * 0.01f + p[gSpPan] * 0.01f * pos));
        if (on) ++k;
    }
    return n;
}

inline const char* noteName(float hz, float* centsOff, char* buf, size_t n)
{
    static const char* const kNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (!(hz > 10.f)) { std::snprintf(buf, n, "--"); if (centsOff) *centsOff = 0.f; return buf; }
    const float m = 69.f + 12.f * std::log2(hz / 440.f);
    const int mi = int(std::lround(m));
    if (centsOff) *centsOff = (m - float(mi)) * 100.f;
    std::snprintf(buf, n, "%s%d", kNames[((mi % 12) + 12) % 12], mi / 12 - 1);
    return buf;
}

// --------------------------------------------------------------------------------------------------------------------
// Detector profiles: starting points for the fine-tuning window. They set the detector parameters and Window only.

struct DetProfile { const char* name; float window, clarity, legato, low, harm, probation, repick, refract, tail; };

static const DetProfile kDetProfiles[] = {
    //                         window clarity legato low  harm  probation repick refract tail
    { "Fast solo",              1.0f,  75.f,   5.f,  12.f, 25.f, 40.f,     1.35f, 25.f,   25.f },
    { "Balanced (default)",     1.0f,  80.f,  12.5f, 30.f, 40.f, 60.f,     1.5f,  40.f,   25.f },
    { "Careful / let ring",     1.5f,  85.f,  25.f,  45.f, 60.f, 60.f,     1.6f,  50.f,   35.f },
};
enum { kDetProfileCount = int(sizeof(kDetProfiles) / sizeof(kDetProfiles[0])) };

inline void applyDetProfile(float* p, const DetProfile& d)
{
    p[gTrack] = d.window; p[gDetClarity] = d.clarity; p[gDetLegato] = d.legato; p[gDetLow] = d.low; p[gDetHarm] = d.harm;
    p[gDetProbation] = d.probation; p[gDetRepick] = d.repick; p[gDetRefract] = d.refract; p[gDetTail] = d.tail;
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

inline void setVoice(float* p, int v, int wave, float oct, float semi, float level = 100.f, float detune = 0.f, float pan = 0.f)
{
    float* const vp = p + voiceIndex(v, 0);
    vp[vOn] = 1.f; vp[vWave] = float(wave); vp[vOct] = oct; vp[vSemi] = semi;
    vp[vLevel] = level; vp[vDetune] = detune; vp[vPan] = pan;
}

inline void setAdsr(float* p, float a, float d, float s, float r)
{
    p[gEnvMode] = float(eAdsr); p[gAttack] = a; p[gDecay] = d; p[gSustain] = s; p[gRelease] = r;
}

inline void setFilter(float* p, float cutoff, float reso, float env, float dec, float key = 50.f)
{
    p[gFltOn] = 1.f; p[gCutoff] = cutoff; p[gReso] = reso; p[gFltEnv] = env; p[gFltDec] = dec; p[gKey] = key;
}

// group: heading shown above this preset in the list (only set on the first preset of a group)
// wave: custom wave text (see Wave.hpp); null = the default one
struct Preset { const char* name; void (*fill)(float*); const char* group; const char* wave; };

static const char* const kOrganWave  = "f:sin(2*pi*x) + 0.5*sin(4*pi*x) + 0.35*sin(6*pi*x) + 0.25*sin(8*pi*x) + 0.15*sin(12*pi*x)";
static const char* const kFormantWave = "f:sin(2*pi*x) * exp(-3*x) + 0.6*sin(10*pi*x) * exp(-5*x)";
static const char* const kStairWave  = "f:floor(sin(2*pi*x)*4)/4";

static const Preset kPresets[] = {
    { "Swarm Saws", [](float* p) {
        presetDefaults(p);
    }, "Pitch tracking" },
    { "Big Swarm (8 saws)", [](float* p) {
        presetDefaults(p); setVoices(p, 8);
        p[gSpDetune] = 22.f; p[gSpPan] = 100.f;
        setFilter(p, 2600.f, 15.f, 1.5f, 600.f);
    } },
    { "Mono Saw Lead", [](float* p) {
        presetDefaults(p); setVoices(p, 1);
        p[gGlide] = 35.f;
        setFilter(p, 1400.f, 45.f, 2.5f, 250.f, 70.f);
    } },
    { "Square Lead", [](float* p) {
        presetDefaults(p); setVoices(p, 2); setAll(p, vWave, float(wPulse));
        p[gSpDetune] = 6.f; p[gSpPan] = 40.f;
        setFilter(p, 2200.f, 30.f, 1.5f, 300.f, 60.f);
    } },
    { "Pulse Swarm", [](float* p) {
        presetDefaults(p); setVoices(p, 5); setAll(p, vWave, float(wPulse));
        for (int v = 0; v < 5; ++v) p[voiceIndex(v, vWidth)] = 18.f + 14.f * float(v);
        p[gSpDetune] = 14.f; p[gSpPan] = 100.f;
        setFilter(p, 3000.f, 20.f, 1.f, 500.f);
    } },
    { "Octave Stack", [](float* p) {
        presetDefaults(p); setVoices(p, 0);
        setVoice(p, 0, wSaw, -1.f, 0.f, 100.f);
        setVoice(p, 1, wSaw, 0.f, 0.f, 100.f, -7.f, -60.f);
        setVoice(p, 2, wSaw, 0.f, 0.f, 100.f, 7.f, 60.f);
        setVoice(p, 3, wPulse, 1.f, 0.f, 55.f);
        p[gSpDetune] = 0.f; p[gSpPan] = 0.f;
        setFilter(p, 2000.f, 25.f, 2.f, 400.f);
    } },
    { "Power Fifths", [](float* p) {
        presetDefaults(p); setVoices(p, 0);
        setVoice(p, 0, wSaw, 0.f, 0.f, 100.f, -6.f, -70.f);
        setVoice(p, 1, wSaw, 0.f, 0.f, 100.f, 6.f, 70.f);
        setVoice(p, 2, wSaw, 0.f, 7.f, 80.f, -4.f, -30.f);
        setVoice(p, 3, wSaw, 0.f, 7.f, 80.f, 4.f, 30.f);
        setVoice(p, 4, wPulse, -1.f, 0.f, 80.f);
        p[gSpDetune] = 0.f; p[gSpPan] = 0.f;
        setFilter(p, 1800.f, 20.f, 2.f, 500.f); p[gDrive] = 12.f;
    } },
    { "Sub Bass", [](float* p) {
        presetDefaults(p); setVoices(p, 0);
        setVoice(p, 0, wSine, -1.f, 0.f, 120.f);
        setVoice(p, 1, wPulse, -1.f, 0.f, 50.f);
        p[gSpDetune] = 0.f; p[gSpPan] = 0.f;
        setFilter(p, 500.f, 20.f, 2.f, 200.f, 30.f);
    } },
    { "Acid Bass", [](float* p) {
        presetDefaults(p); setVoices(p, 1);
        p[voiceIndex(0, vOct)] = -1.f; p[gGlide] = 45.f;
        setFilter(p, 280.f, 78.f, 3.5f, 220.f, 30.f); p[gDrive] = 10.f;
        setAdsr(p, 1.f, 250.f, 60.f, 80.f);
    } },
    { "Soft Pad", [](float* p) {
        presetDefaults(p); setVoices(p, 6);
        p[gSpDetune] = 16.f; p[gSpPan] = 100.f;
        setFilter(p, 900.f, 10.f, 1.5f, 2500.f); p[gFltAtk] = 600.f;
        setAdsr(p, 450.f, 1500.f, 85.f, 1800.f); p[gDyn] = 20.f;
    } },
    { "Brass Sweep", [](float* p) {
        presetDefaults(p); setVoices(p, 3);
        p[gSpDetune] = 8.f;
        setFilter(p, 420.f, 20.f, 3.f, 900.f, 60.f); p[gFltAtk] = 70.f;
        setAdsr(p, 25.f, 600.f, 80.f, 200.f);
    } },
    { "Pluck", [](float* p) {
        presetDefaults(p); setVoices(p, 3);
        p[gSpDetune] = 9.f;
        setFilter(p, 500.f, 30.f, 4.f, 140.f, 70.f);
        setAdsr(p, 1.f, 320.f, 0.f, 250.f); p[gDyn] = 80.f;
    } },
    { "Organ (custom wave)", [](float* p) {
        presetDefaults(p); setVoices(p, 0);
        setVoice(p, 0, wCustom, 0.f, 0.f, 100.f, -3.f, -40.f);
        setVoice(p, 1, wCustom, 0.f, 0.f, 100.f, 3.f, 40.f);
        setVoice(p, 2, wSine, -1.f, 0.f, 70.f);
        p[gSpDetune] = 0.f; p[gSpPan] = 0.f; p[gFltOn] = 0.f;
        setAdsr(p, 4.f, 100.f, 100.f, 90.f); p[gDyn] = 0.f;
    }, nullptr, kOrganWave },
    { "Vocal Formant (custom wave)", [](float* p) {
        presetDefaults(p); setVoices(p, 4); setAll(p, vWave, float(wCustom));
        p[gSpDetune] = 10.f;
        setFilter(p, 3500.f, 10.f, 0.5f, 400.f);
    }, nullptr, kFormantWave },
    { "8-bit Stairs (custom wave)", [](float* p) {
        presetDefaults(p); setVoices(p, 1); setAll(p, vWave, float(wCustom));
        p[gFltOn] = 0.f;
        setAdsr(p, 1.f, 100.f, 100.f, 60.f); p[gDyn] = 0.f;
    }, nullptr, kStairWave },
    { "Breath (noise + sine)", [](float* p) {
        presetDefaults(p); setVoices(p, 0);
        setVoice(p, 0, wSine, 0.f, 0.f, 100.f);
        setVoice(p, 1, wSine, 1.f, 0.f, 40.f);
        setVoice(p, 2, wPink, 0.f, 0.f, 45.f);
        p[gSpDetune] = 0.f; p[gSpPan] = 0.f;
        setFilter(p, 1500.f, 55.f, 1.f, 500.f, 100.f);
    } },
    { "Synth + Guitar (50/50)", [](float* p) {
        presetDefaults(p); setVoices(p, 4);
        p[gMix] = 50.f; p[gSpDetune] = 10.f;
        setFilter(p, 1600.f, 20.f, 2.f, 400.f);
    } },

    // ---- held like a key: the note keeps its level and brightness until the string is muted or the next pick
    { "Keys: Saw Lead", [](float* p) {
        presetDefaults(p); setVoices(p, 2);
        p[gSpDetune] = 5.f; p[gSpPan] = 40.f; p[gCut] = -45.f;
        setFilter(p, 4200.f, 10.f, 0.f, 300.f, 100.f);
        setAdsr(p, 2.f, 200.f, 100.f, 120.f); p[gDyn] = 30.f;
    }, "Keyboard-like (held notes)" },
    { "Keys: Square Lead", [](float* p) {
        presetDefaults(p); setVoices(p, 1); setAll(p, vWave, float(wPulse)); p[gCut] = -45.f;
        setFilter(p, 3500.f, 15.f, 0.f, 300.f, 100.f);
        setAdsr(p, 2.f, 200.f, 100.f, 100.f); p[gDyn] = 20.f;
    } },
    { "Keys: Organ", [](float* p) {
        presetDefaults(p); setVoices(p, 0);
        setVoice(p, 0, wCustom, 0.f, 0.f, 100.f, -3.f, -40.f);
        setVoice(p, 1, wCustom, 0.f, 0.f, 100.f, 3.f, 40.f);
        setVoice(p, 2, wSine, -1.f, 0.f, 70.f);
        p[gSpDetune] = 0.f; p[gSpPan] = 0.f; p[gFltOn] = 0.f; p[gCut] = -45.f;
        setAdsr(p, 3.f, 100.f, 100.f, 80.f); p[gDyn] = 0.f;
    }, nullptr, kOrganWave },
    { "Keys: Electric Piano", [](float* p) {
        presetDefaults(p); setVoices(p, 0);
        setVoice(p, 0, wSine, 0.f, 0.f, 100.f);
        setVoice(p, 1, wTriangle, 0.f, 0.f, 45.f, 4.f, 30.f);
        setVoice(p, 2, wSine, 2.f, 0.f, 14.f, 0.f, -30.f);
        p[gSpDetune] = 0.f; p[gSpPan] = 0.f; p[gCut] = -45.f;
        setFilter(p, 2600.f, 5.f, 1.5f, 450.f, 80.f);
        setAdsr(p, 1.f, 1400.f, 20.f, 300.f); p[gDyn] = 85.f;
    } },
    { "Keys: String Pad", [](float* p) {
        presetDefaults(p); setVoices(p, 6);
        p[gSpDetune] = 14.f; p[gSpPan] = 100.f; p[gCut] = -45.f;
        setFilter(p, 2400.f, 5.f, 0.f, 300.f, 60.f);
        setAdsr(p, 140.f, 300.f, 100.f, 900.f); p[gDyn] = 10.f;
    } },

    // ---- quantize / hold / vibrato / pick strength to brightness
    { "Keys+: Lead (quantize, hold, vibrato)", [](float* p) {
        presetDefaults(p); setVoices(p, 2);
        p[gSpDetune] = 5.f; p[gSpPan] = 40.f; p[gCut] = -45.f;
        p[gQuant] = 1.f; p[gHold] = 1.f; p[gVibDepth] = 14.f; p[gVibRate] = 5.5f;
        setFilter(p, 1500.f, 15.f, 0.f, 300.f, 100.f); p[gVelFlt] = 2.f;
        setAdsr(p, 2.f, 200.f, 100.f, 150.f); p[gDyn] = 30.f;
    }, "Keyboard-like with quantize / hold / vibrato" },
    { "Keys+: Organ (quantize, hold)", [](float* p) {
        presetDefaults(p); setVoices(p, 0);
        setVoice(p, 0, wCustom, 0.f, 0.f, 100.f, -3.f, -40.f);
        setVoice(p, 1, wCustom, 0.f, 0.f, 100.f, 3.f, 40.f);
        setVoice(p, 2, wSine, -1.f, 0.f, 70.f);
        p[gSpDetune] = 0.f; p[gSpPan] = 0.f; p[gFltOn] = 0.f; p[gCut] = -45.f;
        p[gQuant] = 1.f; p[gHold] = 1.f; p[gVibDepth] = 6.f; p[gVibRate] = 6.5f;
        setAdsr(p, 3.f, 100.f, 100.f, 80.f); p[gDyn] = 0.f;
    }, nullptr, kOrganWave },
    { "Keys+: Velocity Clav (quantize)", [](float* p) {
        presetDefaults(p); setVoices(p, 2); setAll(p, vWave, float(wPulse)); setAll(p, vWidth, 28.f);
        p[gSpDetune] = 4.f; p[gSpPan] = 30.f; p[gQuant] = 1.f;
        setFilter(p, 500.f, 30.f, 1.5f, 180.f, 80.f); p[gVelFlt] = 3.f;
        setAdsr(p, 1.f, 500.f, 0.f, 200.f); p[gDyn] = 80.f;
    } },
    { "Keys+: Vibrato Strings (hold)", [](float* p) {
        presetDefaults(p); setVoices(p, 6);
        p[gSpDetune] = 14.f; p[gSpPan] = 100.f; p[gCut] = -45.f;
        p[gQuant] = 1.f; p[gHold] = 1.f; p[gVibDepth] = 10.f; p[gVibRate] = 5.f;
        setFilter(p, 2000.f, 5.f, 0.f, 300.f, 60.f); p[gVelFlt] = 1.f;
        setAdsr(p, 140.f, 300.f, 100.f, 900.f); p[gDyn] = 10.f;
    } },

    // ---- drones: many voices a little apart, each wandering on its own, held until the next note
    { "Drone: Wide Swarm", [](float* p) {
        presetDefaults(p); setVoices(p, 8);
        p[gSpDetune] = 9.f; p[gSpPan] = 100.f; p[gDrift] = 14.f; p[gHold] = 1.f; p[gQuant] = 1.f; p[gGlide] = 180.f; p[gCut] = -50.f;
        setFilter(p, 1400.f, 10.f, 0.8f, 3000.f, 40.f); p[gFltAtk] = 900.f;
        setAdsr(p, 700.f, 500.f, 100.f, 3500.f); p[gDyn] = 0.f;
    }, "Drone (hold + drift)" },
    { "Drone: Octaves and Fifths", [](float* p) {
        presetDefaults(p); setVoices(p, 0);
        setVoice(p, 0, wSaw, -1.f, 0.f, 100.f, -5.f, -80.f);
        setVoice(p, 1, wSaw, -1.f, 0.f, 100.f, 5.f, 80.f);
        setVoice(p, 2, wTriangle, 0.f, 0.f, 100.f, -8.f, -40.f);
        setVoice(p, 3, wSaw, 0.f, 0.f, 80.f, 8.f, 40.f);
        setVoice(p, 4, wSaw, 0.f, 0.f, 80.f, 0.f, 0.f);
        setVoice(p, 5, wSaw, 0.f, 7.f, 55.f, -4.f, -60.f);
        setVoice(p, 6, wTriangle, 1.f, 0.f, 50.f, 6.f, 60.f);
        setVoice(p, 7, wSine, 1.f, 7.f, 30.f, -6.f, 0.f);
        p[gSpDetune] = 0.f; p[gSpPan] = 0.f; p[gDrift] = 10.f; p[gHold] = 1.f; p[gQuant] = 1.f; p[gGlide] = 250.f; p[gCut] = -50.f;
        setFilter(p, 1800.f, 15.f, 0.5f, 4000.f, 30.f); p[gFltAtk] = 1500.f;
        setAdsr(p, 1000.f, 500.f, 100.f, 4000.f); p[gDyn] = 0.f;
    } },
    { "Drone: Uneven Beating", [](float* p) {
        presetDefaults(p); setVoices(p, 6); setAll(p, vWave, float(wTriangle));
        p[gSpDetune] = 3.f; p[gSpPan] = 100.f; p[gDrift] = 30.f; p[gHold] = 1.f; p[gQuant] = 1.f; p[gGlide] = 120.f; p[gCut] = -50.f;
        p[voiceIndex(4, vOct)] = -1.f; p[voiceIndex(5, vOct)] = -1.f;
        p[gFltOn] = 0.f;
        setAdsr(p, 500.f, 500.f, 100.f, 3000.f); p[gDyn] = 0.f;
    } },
    { "Drone: Dark Pulse", [](float* p) {
        presetDefaults(p); setVoices(p, 7); setAll(p, vWave, float(wPulse));
        for (int v = 0; v < 7; ++v) p[voiceIndex(v, vWidth)] = 12.f + 12.f * float(v);
        p[voiceIndex(0, vOct)] = -1.f; p[voiceIndex(6, vOct)] = -1.f;
        p[gSpDetune] = 12.f; p[gSpPan] = 100.f; p[gDrift] = 18.f; p[gHold] = 1.f; p[gQuant] = 1.f; p[gGlide] = 300.f; p[gCut] = -50.f;
        setFilter(p, 420.f, 40.f, 1.5f, 5000.f, 20.f); p[gFltAtk] = 2000.f; p[gDrive] = 8.f;
        setAdsr(p, 1200.f, 500.f, 100.f, 4500.f); p[gDyn] = 0.f;
    } },
    { "Drone: Organ Cloud (custom wave)", [](float* p) {
        presetDefaults(p); setVoices(p, 8); setAll(p, vWave, float(wCustom));
        p[voiceIndex(6, vOct)] = -1.f; p[voiceIndex(7, vOct)] = 1.f; p[voiceIndex(7, vLevel)] = 45.f;
        p[gSpDetune] = 7.f; p[gSpPan] = 100.f; p[gDrift] = 12.f; p[gHold] = 1.f; p[gQuant] = 1.f; p[gGlide] = 200.f; p[gCut] = -50.f;
        setFilter(p, 2600.f, 5.f, 0.f, 300.f, 30.f);
        setAdsr(p, 900.f, 500.f, 100.f, 4000.f); p[gDyn] = 0.f;
    }, nullptr, kOrganWave },
    // ---- a wall of free-running oscillators: scattered at every pick, each finds the note in its own time
    { "Drone: Oscillator Wall (scatter to unison)", [](float* p) {
        presetDefaults(p); setVoices(p, 8);
        for (int v = 0; v < 8; v += 2) { p[voiceIndex(v, vWave)] = float(wPulse); p[voiceIndex(v, vWidth)] = 30.f + 5.f * float(v); }
        p[gSpDetune] = 22.f; p[gSpPan] = 100.f; p[gDrift] = 16.f; p[gHold] = 1.f; p[gQuant] = 1.f; p[gGlide] = 0.f; p[gCut] = -50.f;
        p[gScatter] = 700.f; p[gConverge] = 6000.f;
        p[gFltOn] = 0.f;   // free-running oscillators straight to the mixer: bright
        setAdsr(p, 600.f, 500.f, 100.f, 5000.f); p[gDyn] = 0.f;
    } },
    { "Drone: Oscillator Wall (slow chaos)", [](float* p) {
        presetDefaults(p); setVoices(p, 8);
        for (int v = 0; v < 8; v += 2) { p[voiceIndex(v, vWave)] = float(wPulse); p[voiceIndex(v, vWidth)] = 20.f + 8.f * float(v); }
        p[gSpDetune] = 30.f; p[gSpPan] = 100.f; p[gDrift] = 25.f; p[gHold] = 1.f; p[gQuant] = 1.f; p[gGlide] = 0.f; p[gCut] = -50.f;
        p[gScatter] = 1200.f; p[gConverge] = 16000.f;
        setFilter(p, 3500.f, 10.f, 1.f, 5000.f, 50.f); p[gFltAtk] = 2000.f;
        setAdsr(p, 1500.f, 500.f, 100.f, 5000.f); p[gDyn] = 0.f;
    } },
    { "Drone: Oscillator Wall (in tune)", [](float* p) {
        presetDefaults(p); setVoices(p, 8);
        for (int v = 0; v < 8; v += 2) { p[voiceIndex(v, vWave)] = float(wPulse); p[voiceIndex(v, vWidth)] = 30.f + 5.f * float(v); }
        p[gSpDetune] = 35.f; p[gSpPan] = 100.f; p[gDrift] = 30.f; p[gHold] = 1.f; p[gQuant] = 1.f; p[gGlide] = 200.f; p[gCut] = -50.f;
        p[gFltOn] = 0.f;   // free-running oscillators straight to the mixer: bright
        setAdsr(p, 900.f, 500.f, 100.f, 5000.f); p[gDyn] = 0.f;
    } },

    // ---- in the manner of a late-70s two-oscillator mono synth with a 24 dB ladder filter
    { "Prodigy style: Bass", [](float* p) {
        presetDefaults(p); setVoices(p, 0);
        setVoice(p, 0, wSaw, -1.f, 0.f, 100.f, -4.f);
        setVoice(p, 1, wPulse, -1.f, 0.f, 90.f, 5.f);
        p[gSpDetune] = 0.f; p[gSpPan] = 0.f; p[gGlide] = 25.f; p[gQuant] = 1.f; p[gCut] = -40.f;
        setFilter(p, 240.f, 45.f, 3.2f, 260.f, 40.f); p[gSlope] = 3.f; p[gDrive] = 9.f; p[gVelFlt] = 1.f;
        setAdsr(p, 1.f, 350.f, 65.f, 90.f); p[gDyn] = 30.f;
    }, "Prodigy style" },
    { "Prodigy style: Lead", [](float* p) {
        presetDefaults(p); setVoices(p, 0);
        setVoice(p, 0, wSaw, 0.f, 0.f, 100.f, -6.f);
        setVoice(p, 1, wSaw, 0.f, 0.f, 100.f, 7.f);
        p[gSpDetune] = 0.f; p[gSpPan] = 0.f; p[gGlide] = 45.f; p[gQuant] = 1.f; p[gCut] = -45.f;
        p[gVibDepth] = 12.f; p[gVibRate] = 5.8f;
        setFilter(p, 900.f, 55.f, 2.5f, 450.f, 70.f); p[gSlope] = 3.f; p[gDrive] = 6.f;
        setAdsr(p, 3.f, 300.f, 85.f, 180.f); p[gDyn] = 20.f;
    } },
    { "Prodigy style: Fifth Lead", [](float* p) {
        presetDefaults(p); setVoices(p, 0);
        setVoice(p, 0, wSaw, 0.f, 0.f, 100.f);
        setVoice(p, 1, wPulse, 0.f, 7.f, 85.f);
        p[gSpDetune] = 0.f; p[gSpPan] = 0.f; p[gGlide] = 60.f; p[gQuant] = 1.f; p[gCut] = -45.f;
        setFilter(p, 700.f, 65.f, 3.f, 600.f, 60.f); p[gSlope] = 3.f; p[gDrive] = 12.f;
        setAdsr(p, 2.f, 400.f, 80.f, 150.f); p[gDyn] = 20.f;
    } },

    // ---- the guitar signal itself drives the oscillators: no detection delay, more "alive" and less clean
    { "Instant Lead", [](float* p) {
        presetDefaults(p); setVoices(p, 2); p[gMode] = float(mReshape);
        p[gSpDetune] = 7.f; p[gSpPan] = 50.f; p[gGlide] = 4.f;
        setFilter(p, 2000.f, 30.f, 2.f, 300.f, 60.f);
    }, "Reshape (instant)" },
    { "Instant Swarm", [](float* p) {
        presetDefaults(p); setVoices(p, 6); p[gMode] = float(mReshape);
        p[gSpDetune] = 18.f; p[gSpPan] = 100.f; p[gGlide] = 6.f;
        setFilter(p, 2400.f, 15.f, 1.5f, 500.f);
    } },
    { "Instant Square Fuzz", [](float* p) {
        presetDefaults(p); setVoices(p, 0); p[gMode] = float(mReshape);
        setVoice(p, 0, wPulse, 0.f, 0.f, 100.f);
        setVoice(p, 1, wPulse, -1.f, 0.f, 70.f);
        p[gSpDetune] = 0.f; p[gSpPan] = 0.f; p[gGlide] = 0.f;
        setFilter(p, 3200.f, 10.f, 1.f, 300.f); p[gDrive] = 9.f;
    } },
    { "Instant Octaver", [](float* p) {
        presetDefaults(p); setVoices(p, 0); p[gMode] = float(mReshape);
        setVoice(p, 0, wTriangle, -1.f, 0.f, 110.f);
        setVoice(p, 1, wSaw, 1.f, 0.f, 45.f);
        p[gSpDetune] = 0.f; p[gSpPan] = 0.f; p[gMix] = 50.f; p[gGlide] = 3.f;
        setFilter(p, 2500.f, 10.f, 1.f, 300.f);
    } },
    { "Instant Organ", [](float* p) {
        presetDefaults(p); setVoices(p, 0); p[gMode] = float(mReshape);
        setVoice(p, 0, wCustom, 0.f, 0.f, 100.f, -3.f, -40.f);
        setVoice(p, 1, wCustom, 0.f, 0.f, 100.f, 3.f, 40.f);
        p[gSpDetune] = 0.f; p[gSpPan] = 0.f; p[gFltOn] = 0.f; p[gGlide] = 8.f;
    }, nullptr, kOrganWave },
};

enum { kPresetCount = int(sizeof(kPresets) / sizeof(kPresets[0])) };

inline const char* presetWave(int i) { return kPresets[i].wave != nullptr ? kPresets[i].wave : kDefaultWave; }

} // namespace swsyn
