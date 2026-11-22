// Swarm Chorus - shared parameter definitions, LFO shapes, presets.
// Used by both the DSP engine and the UI (so the graph shows exactly what the DSP does).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "Wave.hpp"

namespace roj {

enum { kNumVoices = 6 };

enum GlobalParam {
    gMix, gOutput, gRateMul, gDepthMul,
    gSpPhase, gSpStart, gSpRate, gSpDetune, gSpPan,
    gTrigOn, gTrigThresh, gFade,
    gEqTarget,
    gEq1F, gEq1G, gEq1Q,
    gEq2F, gEq2G, gEq2Q,
    gEq3F, gEq3G, gEq3Q,
    gWarmth, gSync, gEnvSens, gEnvDepth, gEnvWah,
    kGlobalCount
};

enum VoiceParam {
    vOn, vLevel, vPan,
    vDelayMin, vDelayMax, vRate, vShape, vPhase, vStart,
    vDetune, vFeedback,
    vHue, vWah, vFilter, vReso,
    vDiv,
    kVoiceParamCount
};

enum {
    kInputCount = kGlobalCount + kNumVoices * kVoiceParamCount,
    kOutNow     = kInputCount,            // one per voice: current delay time in ms
    kOutTrig    = kOutNow + kNumVoices,   // 1 right after a trigger
    kOutEnv,                              // envelope follower level 0..1
    kOutBpm,                              // host tempo
    kParamCount,
    kOutputCount = kParamCount - kInputCount
};

enum Shape { shSine, shTriangle, shLamp, shRampUp, shRampDown, shSquare, shRandom, shCustom, kShapeCount };

static const char* const kShapeNames[kShapeCount] = {
    "Sine", "Triangle", "Lamp (Vibe)", "Ramp up", "Ramp down", "Square", "Random", "Custom wave"
};

enum { kDivCount = 16 };
static const char* const kDivNames[kDivCount] = {
    "8 bars", "4 bars", "2 bars", "1 bar", "1/2.", "1/2", "1/2T", "1/4.", "1/4", "1/4T", "1/8.", "1/8", "1/8T", "1/16", "1/16T", "1/32"
};
// length of one LFO cycle in quarter notes
static const float kDivBeats[kDivCount] = {
    32.f, 16.f, 8.f, 4.f, 3.f, 2.f, 4.f / 3.f, 1.5f, 1.f, 2.f / 3.f, 0.75f, 0.5f, 1.f / 3.f, 0.25f, 1.f / 6.f, 0.125f
};

enum Flags { fLog = 1, fInt = 2, fBool = 4 };

struct Def {
    const char* name;
    const char* sym;
    const char* unit;
    float min, max, def;
    int flags;
};

static const Def kGlobalDefs[kGlobalCount] = {
    { "Mix",            "mix",        "%",   0.f,    100.f,   50.f,  0 },
    { "Output",         "output",     "dB",  -24.f,  12.f,    0.f,   0 },
    { "Rate x",         "rate_mul",   "x",   0.25f,  4.f,     1.f,   fLog },
    { "Depth x",        "depth_mul",  "%",   0.f,    200.f,   100.f, 0 },
    { "Phase spread",   "sp_phase",   "deg", 0.f,    360.f,   360.f, 0 },
    { "Start stagger",  "sp_start",   "ms",  0.f,    2000.f,  0.f,   0 },
    { "Rate spread",    "sp_rate",    "%",   0.f,    100.f,   0.f,   0 },
    { "Detune spread",  "sp_detune",  "ct",  0.f,    50.f,    0.f,   0 },
    { "Pan spread",     "sp_pan",     "%",   0.f,    100.f,   100.f, 0 },
    { "Trigger",        "trig_on",    "",    0.f,    1.f,     0.f,   fBool },
    { "Threshold",      "trig_thr",   "dB",  -70.f,  -6.f,    -40.f, 0 },
    { "Fade in",        "fade",       "ms",  0.f,    3000.f,  0.f,   0 },
    { "EQ on output",   "eq_target",  "",    0.f,    1.f,     0.f,   fBool },
    { "EQ1 Freq",       "eq1_freq",   "Hz",  40.f,   1000.f,  150.f, fLog },
    { "EQ1 Gain",       "eq1_gain",   "dB",  -15.f,  15.f,    0.f,   0 },
    { "EQ1 Q",          "eq1_q",      "",    0.3f,   6.f,     0.8f,  fLog },
    { "EQ2 Freq",       "eq2_freq",   "Hz",  150.f,  5000.f,  800.f, fLog },
    { "EQ2 Gain",       "eq2_gain",   "dB",  -15.f,  15.f,    0.f,   0 },
    { "EQ2 Q",          "eq2_q",      "",    0.3f,   6.f,     0.8f,  fLog },
    { "EQ3 Freq",       "eq3_freq",   "Hz",  1000.f, 16000.f, 4000.f, fLog },
    { "EQ3 Gain",       "eq3_gain",   "dB",  -15.f,  15.f,    0.f,   0 },
    { "EQ3 Q",          "eq3_q",      "",    0.3f,   6.f,     0.8f,  fLog },
    { "Warmth",         "warmth",     "%",   0.f,    100.f,   0.f,   0 },
    { "Tempo sync",     "sync",       "",    0.f,    1.f,     0.f,   fBool },
    { "Env sens",       "env_sens",   "dB",  0.f,    40.f,    12.f,  0 },
    { "Env > depth",    "env_depth",  "%",   -100.f, 100.f,   0.f,   0 },
    { "Env > wah",      "env_wah",    "oct", -4.f,   4.f,     0.f,   0 },
};

static const Def kVoiceDefs[kVoiceParamCount] = {
    { "On",        "on",       "",    0.f,    1.f,    0.f,   fBool },
    { "Level",     "level",    "%",   0.f,    150.f,  100.f, 0 },
    { "Pan",       "pan",      "",    -100.f, 100.f,  0.f,   0 },
    { "Delay min", "dmin",     "ms",  0.1f,   60.f,   6.f,   fLog },
    { "Delay max", "dmax",     "ms",  0.1f,   60.f,   11.f,  fLog },
    { "Rate",      "rate",     "Hz",  0.02f,  12.f,   0.6f,  fLog },
    { "Shape",     "shape",    "",    0.f,    float(kShapeCount - 1), 0.f, fInt },
    { "Phase",     "phase",    "deg", 0.f,    360.f,  0.f,   0 },
    { "Start",     "start",    "ms",  0.f,    5000.f, 0.f,   0 },
    { "Detune",    "detune",   "ct",  -50.f,  50.f,   0.f,   0 },
    { "Feedback",  "feedback", "%",   -95.f,  95.f,   0.f,   0 },
    { "Hue",       "hue",      "Hz",  150.f,  4000.f, 700.f, fLog },
    { "Wah",       "wah",      "oct", -4.f,   4.f,    0.f,   0 },
    { "Filter",    "filter",   "%",   0.f,    100.f,  0.f,   0 },
    { "Reso",      "reso",     "",    0.7f,   12.f,   4.f,   fLog },
    { "Division",  "div",      "",    0.f,    float(kDivCount - 1), 3.f, fInt },
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
    if (index < kGlobalCount) return kGlobalDefs[index].def;
    const int v = (index - kGlobalCount) / kVoiceParamCount;
    const int p = (index - kGlobalCount) % kVoiceParamCount;
    if (p == vOn) return v < 3 ? 1.f : 0.f;
    return kVoiceDefs[p].def;
}

// "v3_rate" style symbol, also used as the key in exported preset files
inline void symbolOf(int index, char* buf, size_t n)
{
    if (index < kGlobalCount) { std::snprintf(buf, n, "%s", kGlobalDefs[index].sym); return; }
    const int v = (index - kGlobalCount) / kVoiceParamCount;
    const int p = (index - kGlobalCount) % kVoiceParamCount;
    std::snprintf(buf, n, "v%d_%s", v + 1, kVoiceDefs[p].sym);
}

// --------------------------------------------------------------------------------------------------------------------
// LFO shapes. phase is unbounded (cycles), output 0..1. All shapes start at 0 for phase 0.

inline float hash01(uint32_t voice, uint32_t n)
{
    uint32_t x = (voice + 1u) * 0x9E3779B1u ^ (n + 1u) * 0x85EBCA77u;
    x ^= x >> 15; x *= 0x2C1B3C6Du;
    x ^= x >> 12; x *= 0x297A2D39u;
    x ^= x >> 15;
    return float(x >> 8) * (1.0f / 16777216.0f);
}

inline float smooth01(float x) { x = std::min(1.f, std::max(0.f, x)); return x * x * (3.f - 2.f * x); }

// wave: the compiled custom wave, kWaveSize values (only used by shCustom)
inline float lfoShape(int shape, double phase, int voice, const float* wave)
{
    static const float kPi = 3.14159265358979f;
    if (phase < 0.0) phase = 0.0;
    const double fl = std::floor(phase);
    const float p = float(phase - fl);

    switch (shape)
    {
    default:
    case shSine:
        return 0.5f - 0.5f * std::cos(2.f * kPi * p);
    case shTriangle:
        return p < 0.5f ? 2.f * p : 2.f - 2.f * p;
    case shLamp: {
        // incandescent bulb + photoresistor: quick to light up, slow to fade, squashed near the dark end
        const float q = p < 0.38f ? p / 0.38f * 0.5f : 0.5f + (p - 0.38f) / 0.62f * 0.5f;
        const float x = 0.5f - 0.5f * std::cos(2.f * kPi * q);
        return std::pow(x, 1.9f);
    }
    case shRampUp:
        return p < 0.92f ? p / 0.92f : 1.f - smooth01((p - 0.92f) / 0.08f);
    case shRampDown:
        return p < 0.08f ? smooth01(p / 0.08f) : 1.f - (p - 0.08f) / 0.92f;
    case shSquare:
        if (p < 0.08f) return smooth01(p / 0.08f);
        if (p < 0.5f) return 1.f;
        if (p < 0.58f) return 1.f - smooth01((p - 0.5f) / 0.08f);
        return 0.f;
    case shRandom: {
        const uint32_t n = uint32_t(int64_t(fl));
        const float a = n == 0 ? 0.f : hash01(uint32_t(voice), n);
        const float b = hash01(uint32_t(voice), n + 1u);
        return a + (b - a) * (0.5f - 0.5f * std::cos(kPi * p));
    }
    case shCustom: {
        if (wave == nullptr) return 0.f;
        const float x = p * float(kWaveSize);
        const int i = int(x) & (kWaveSize - 1);
        const float f = x - std::floor(x);
        return wave[i] + (wave[(i + 1) & (kWaveSize - 1)] - wave[i]) * f;
    }
    }
}

// --------------------------------------------------------------------------------------------------------------------
// Effective per-voice values after the global "spread" macros are applied

struct Eff {
    bool  on;
    float start;   // seconds
    float rate;    // Hz
    float phase;   // cycles
    float detune;  // cents
    float pan;     // -1..1
};

inline int computeEff(const float* p, Eff* e, float bpm)
{
    const bool sync = p[gSync] > 0.5f;
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
        const float frac = (on && n > 0) ? float(k) / float(n) : 0.f;
        const float rank = on ? float(k) : 0.f;

        e[v].on     = on;
        e[v].start  = (vp[vStart] + p[gSpStart] * rank) * 0.001f;
        const int div = std::min(int(kDivCount) - 1, std::max(0, int(vp[vDiv] + 0.5f)));
        const float base = sync ? bpm / 60.f / kDivBeats[div] : vp[vRate];
        e[v].rate   = base * p[gRateMul] * std::pow(2.f, p[gSpRate] * 0.01f * pos);
        e[v].phase  = vp[vPhase] / 360.f + p[gSpPhase] / 360.f * frac;
        e[v].detune = vp[vDetune] + p[gSpDetune] * pos;
        e[v].pan    = std::min(1.f, std::max(-1.f, vp[vPan] * 0.01f + p[gSpPan] * 0.01f * pos));
        if (on) ++k;
    }
    return n;
}

// depth envelope: with a fade time, 0 until the voice's start and then fading in
inline float fadeEnv(float sinceStart, float fadeSec)
{
    // without a fade the voice just waits, frozen at its phase, so nothing jumps when it starts
    if (fadeSec <= 0.0005f) return 1.f;
    if (sinceStart < 0.f) return 0.f;
    return smooth01(sinceStart / fadeSec);
}

inline float delayFromMod(const float* p, int voice, float mod)
{
    const float* vp = p + voiceIndex(voice, 0);
    const float d = vp[vDelayMin] + (vp[vDelayMax] - vp[vDelayMin]) * p[gDepthMul] * 0.01f * mod;
    return std::min(150.f, std::max(0.05f, d));
}

// closed-form delay time (ms) of a voice, t seconds after the LFOs were (re)started. Used by the graph.
inline float voiceDelayAt(const float* p, const Eff& e, int voice, float t, const float* wave)
{
    const float tt = t - e.start;
    const double ph = double(e.phase) + double(e.rate) * double(std::max(0.f, tt));
    const int shape = int(p[voiceIndex(voice, vShape)] + 0.5f);
    const float mod = fadeEnv(tt, p[gFade] * 0.001f) * lfoShape(shape, ph, voice, wave);
    return delayFromMod(p, voice, mod);
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

// group: heading shown above this preset in the list (only set on the first preset of a group)
// wave: custom wave text (see Wave.hpp); null = the default one
struct Preset { const char* name; void (*fill)(float*); const char* group; const char* wave; };

// rise, hold, fall, hold - the LFO shape of the Dimension units
static const char* const kTrapezoidWave = "l:0,0;0.2,1;0.5,1;0.7,0";

// one voice, all the values that usually differ between classic units
inline void setVoice(float* p, int v, float dmin, float dmax, float rate, int shape, float pan, float detune = 0.f)
{
    float* const vp = p + voiceIndex(v, 0);
    vp[vOn] = 1.f; vp[vDelayMin] = dmin; vp[vDelayMax] = dmax; vp[vRate] = rate;
    vp[vShape] = float(shape); vp[vPan] = pan; vp[vDetune] = detune;
}

static const Preset kPresets[] = {
    { "Tri-Chorus", [](float* p) {
        presetDefaults(p);
    }, "Swarm" },
    { "Dimension", [](float* p) {
        presetDefaults(p); setVoices(p, 2);
        setAll(p, vShape, shTriangle); setAll(p, vRate, 0.45f);
        setAll(p, vDelayMin, 8.5f); setAll(p, vDelayMax, 11.f);
        p[gMix] = 42.f; p[gEq3F] = 5000.f; p[gEq3G] = 2.f;
    } },
    { "Cascade (triggered)", [](float* p) {
        presetDefaults(p); setVoices(p, 4);
        setAll(p, vRate, 0.8f); setAll(p, vDelayMin, 5.f); setAll(p, vDelayMax, 14.f);
        p[gSpPhase] = 0.f; p[gSpStart] = 350.f; p[gFade] = 600.f; p[gTrigOn] = 1.f;
    } },
    { "Micro Detune", [](float* p) {
        presetDefaults(p); setVoices(p, 2);
        p[voiceIndex(0, vDelayMin)] = 9.f;  p[voiceIndex(0, vDelayMax)] = 9.f;
        p[voiceIndex(1, vDelayMin)] = 14.f; p[voiceIndex(1, vDelayMax)] = 14.f;
        p[gSpDetune] = 9.f; p[gSpPhase] = 0.f;
    } },
    { "Detuned Chorus", [](float* p) {
        presetDefaults(p); setVoices(p, 4);
        setAll(p, vRate, 0.35f); setAll(p, vDelayMin, 7.f); setAll(p, vDelayMax, 12.f);
        p[gSpDetune] = 7.f; p[gSpRate] = 25.f;
    } },
    { "Vibe Lamp", [](float* p) {
        presetDefaults(p); setVoices(p, 2);
        setAll(p, vShape, shLamp); setAll(p, vRate, 1.5f);
        setAll(p, vDelayMin, 1.5f); setAll(p, vDelayMax, 6.f);
        setAll(p, vFilter, 70.f); setAll(p, vHue, 450.f); setAll(p, vWah, 2.2f); setAll(p, vReso, 2.5f);
        p[gSpPhase] = 90.f; p[gSpPan] = 60.f; p[gMix] = 60.f;
    } },
    { "Jet Flanger", [](float* p) {
        presetDefaults(p); setVoices(p, 1);
        setAll(p, vShape, shTriangle); setAll(p, vRate, 0.18f);
        setAll(p, vDelayMin, 0.4f); setAll(p, vDelayMax, 6.f); setAll(p, vFeedback, 75.f);
    } },
    { "Swarm", [](float* p) {
        presetDefaults(p); setVoices(p, 6);
        setAll(p, vShape, shRandom); setAll(p, vRate, 0.5f);
        setAll(p, vDelayMin, 8.f); setAll(p, vDelayMax, 22.f);
        p[gSpRate] = 60.f; p[gSpDetune] = 6.f; p[gMix] = 55.f;
    } },
    { "Wah Chorus", [](float* p) {
        presetDefaults(p); setVoices(p, 3);
        setAll(p, vRate, 0.9f);
        setAll(p, vFilter, 100.f); setAll(p, vHue, 420.f); setAll(p, vWah, 2.5f); setAll(p, vReso, 5.f);
        p[gMix] = 60.f;
    } },
    { "Cocked Hue", [](float* p) {
        presetDefaults(p); setVoices(p, 1);
        setAll(p, vShape, shLamp); setAll(p, vRate, 0.7f);
        setAll(p, vFilter, 85.f); setAll(p, vHue, 950.f); setAll(p, vWah, 0.f); setAll(p, vReso, 4.f);
    } },
    { "Analog Warm", [](float* p) {
        presetDefaults(p); setVoices(p, 2);
        setAll(p, vShape, shTriangle); setAll(p, vRate, 0.5f);
        setAll(p, vDelayMin, 5.f); setAll(p, vDelayMax, 9.f);
        p[gWarmth] = 65.f;
    } },
    { "Touch Wah", [](float* p) {
        presetDefaults(p); setVoices(p, 3);
        setAll(p, vFilter, 100.f); setAll(p, vHue, 350.f); setAll(p, vWah, 0.5f); setAll(p, vReso, 5.f);
        p[gEnvWah] = 2.5f; p[gEnvSens] = 14.f; p[gMix] = 60.f;
    } },
    { "Tempo Sweep (synced)", [](float* p) {
        presetDefaults(p); setVoices(p, 2);
        setAll(p, vShape, shTriangle); setAll(p, vDiv, 2.f);
        setAll(p, vDelayMin, 1.f); setAll(p, vDelayMax, 8.f); setAll(p, vFeedback, 45.f);
        p[gSync] = 1.f; p[gWarmth] = 30.f;
    } },
    { "Vibrato", [](float* p) {
        presetDefaults(p); setVoices(p, 1);
        setAll(p, vRate, 5.f); setAll(p, vDelayMin, 4.f); setAll(p, vDelayMax, 5.5f);
        p[gMix] = 100.f;
    } },

    // ---- classic units. Numbers marked "doc" come from published measurements or manuals,
    //      the rest is set by reasoning from the circuit type and has not been measured.
    { "CE-1 Chorus style", [](float* p) {
        // doc: delay sweeps roughly 3-5 ms up to 3-7 ms, triangle LFO, chorus rate about 1-3 Hz
        presetDefaults(p); setVoices(p, 0);
        setVoice(p, 0, 3.f, 6.f, 1.2f, shTriangle, 0.f);
        p[gSpPan] = 0.f; p[gSpPhase] = 0.f; p[gWarmth] = 50.f;
    }, "Classic units" },
    { "CE-1 Vibrato style", [](float* p) {
        // doc: vibrato rate 3.2-11.6 Hz, pitch-only signal
        presetDefaults(p); setVoices(p, 0);
        setVoice(p, 0, 3.f, 5.5f, 5.5f, shTriangle, 0.f);
        p[gSpPan] = 0.f; p[gSpPhase] = 0.f; p[gWarmth] = 50.f; p[gMix] = 100.f;
    } },
    { "CE-2 style", [](float* p) {
        // doc: triangle LFO up to 3.5 Hz, 50/50 mix, filters around 6.6 kHz; sweep width is my estimate
        presetDefaults(p); setVoices(p, 0);
        setVoice(p, 0, 5.f, 9.f, 0.8f, shTriangle, 0.f);
        p[gSpPan] = 0.f; p[gSpPhase] = 0.f; p[gWarmth] = 48.f;
    } },
    { "Small Clone style (deep)", [](float* p) {
        // same BBD family as the CE-2, depth switch up; values are my estimate
        presetDefaults(p); setVoices(p, 0);
        setVoice(p, 0, 3.5f, 11.f, 0.9f, shTriangle, 0.f);
        p[gSpPan] = 0.f; p[gSpPhase] = 0.f; p[gWarmth] = 40.f;
    } },
    { "Dimension style (subtle)", [](float* p) {
        // doc: two delay lines modulated out of phase by a trapezoid LFO, very little pitch wobble
        presetDefaults(p); setVoices(p, 0);
        setVoice(p, 0, 8.5f, 10.f, 0.25f, shCustom, 0.f);
        setVoice(p, 1, 8.5f, 10.f, 0.25f, shCustom, 0.f);
        p[gSpPhase] = 360.f; p[gSpPan] = 100.f; p[gWarmth] = 35.f; p[gMix] = 40.f;
    }, nullptr, kTrapezoidWave },
    { "Dimension style (strong)", [](float* p) {
        presetDefaults(p); setVoices(p, 0);
        setVoice(p, 0, 7.5f, 11.5f, 0.5f, shCustom, 0.f);
        setVoice(p, 1, 7.5f, 11.5f, 0.5f, shCustom, 0.f);
        setAll(p, vFeedback, -8.f);
        p[gSpPhase] = 360.f; p[gSpPan] = 100.f; p[gWarmth] = 35.f; p[gMix] = 50.f;
    }, nullptr, kTrapezoidWave },
    { "Juno Chorus I style", [](float* p) {
        // doc: triangle 0.5 Hz, left and right 180 degrees apart, wet a little below dry; delay range from memory
        presetDefaults(p); setVoices(p, 0);
        setVoice(p, 0, 1.7f, 5.4f, 0.5f, shTriangle, 0.f);
        setVoice(p, 1, 1.7f, 5.4f, 0.5f, shTriangle, 0.f);
        p[gSpPhase] = 360.f; p[gSpPan] = 100.f; p[gWarmth] = 30.f; p[gMix] = 45.f;
    } },
    { "Juno Chorus II style", [](float* p) {
        // doc: triangle 0.83 Hz
        presetDefaults(p); setVoices(p, 0);
        setVoice(p, 0, 1.7f, 5.4f, 0.83f, shTriangle, 0.f);
        setVoice(p, 1, 1.7f, 5.4f, 0.83f, shTriangle, 0.f);
        p[gSpPhase] = 360.f; p[gSpPan] = 100.f; p[gWarmth] = 30.f; p[gMix] = 45.f;
    } },
    { "TC stereo chorus style", [](float* p) {
        // doc: 1210 delay range 0.65-12 ms; a published user setting: 0.5 Hz, 2.6 ms, two voices 180 degrees apart
        presetDefaults(p); setVoices(p, 0);
        setVoice(p, 0, 2.6f, 5.f, 0.5f, shSine, 0.f);
        setVoice(p, 1, 2.6f, 5.f, 0.5f, shSine, 0.f);
        p[gSpPhase] = 360.f; p[gSpPan] = 100.f; p[gWarmth] = 8.f;
    } },

    // ---- 80s rack and hair-metal sounds
    { "Tri-Stereo Rack (80s)", [](float* p) {
        // doc: three BBD lines left / centre / right, 120 degrees apart, several LFOs per line
        presetDefaults(p); setVoices(p, 0);
        for (int v = 0; v < 3; ++v) setVoice(p, v, 8.f, 15.f, 0.4f, shSine, 0.f);
        p[gSpPhase] = 360.f; p[gSpPan] = 100.f; p[gSpRate] = 18.f; p[gWarmth] = 35.f; p[gMix] = 55.f;
    }, "80s rack / hair metal" },
    { "Rockman Chorus style", [](float* p) {
        // doc: delay around 20 ms swept about 20-24 ms, rate 0.25-1 Hz
        presetDefaults(p); setVoices(p, 0);
        setVoice(p, 0, 20.f, 24.f, 0.5f, shSine, 0.f);
        setVoice(p, 1, 20.f, 24.f, 0.5f, shSine, 0.f);
        p[gSpPhase] = 360.f; p[gSpPan] = 100.f; p[gWarmth] = 30.f;
    } },
    { "Rockman Long Chorus style", [](float* p) {
        // doc: Long Chorus switch doubles the delay to about 40 ms
        presetDefaults(p); setVoices(p, 0);
        setVoice(p, 0, 40.f, 44.f, 0.5f, shSine, 0.f);
        setVoice(p, 1, 40.f, 44.f, 0.5f, shSine, 0.f);
        p[gSpPhase] = 360.f; p[gSpPan] = 100.f; p[gWarmth] = 30.f;
    } },
    { "Micro Pitch +9/-9 (H3000 style)", [](float* p) {
        // doc: left up 9 cents, right down 9 cents, a slight extra delay on the right, no sweep
        presetDefaults(p); setVoices(p, 0);
        setVoice(p, 0, 1.f, 1.f, 0.5f, shSine, 0.f, 9.f);
        setVoice(p, 1, 12.f, 12.f, 0.5f, shSine, 0.f, -9.f);
        p[gSpPhase] = 0.f; p[gSpPan] = 100.f;
    } },
    { "Hair Metal Clean (wide shimmer)", [](float* p) {
        // my own: tri-chorus plus detune, bright, for clean arpeggios
        presetDefaults(p); setVoices(p, 0);
        for (int v = 0; v < 3; ++v) setVoice(p, v, 7.f, 14.f, 0.6f, shSine, 0.f);
        p[gSpPhase] = 360.f; p[gSpPan] = 100.f; p[gSpDetune] = 8.f; p[gWarmth] = 20.f; p[gMix] = 60.f;
        p[gEq1F] = 140.f; p[gEq1G] = -2.5f; p[gEq3F] = 5200.f; p[gEq3G] = 3.5f;
    } },
    { "Hair Metal Rhythm (thick)", [](float* p) {
        // my own: slow and deep so it stays clear under distortion (published CE-3 advice: low rate, high depth)
        presetDefaults(p); setVoices(p, 0);
        setVoice(p, 0, 5.f, 16.f, 0.3f, shTriangle, 0.f);
        setVoice(p, 1, 5.f, 16.f, 0.3f, shTriangle, 0.f);
        p[gSpPhase] = 360.f; p[gSpPan] = 100.f; p[gWarmth] = 40.f; p[gMix] = 45.f;
        p[gEq1F] = 120.f; p[gEq1G] = -3.f;
    } },
    { "Power Ballad Arpeggio", [](float* p) {
        // my own: four voices, long delays, detune, lots of effect
        presetDefaults(p); setVoices(p, 0);
        for (int v = 0; v < 4; ++v) setVoice(p, v, 12.f, 22.f, 0.45f, shSine, 0.f);
        p[gSpPhase] = 360.f; p[gSpPan] = 100.f; p[gSpDetune] = 8.f; p[gSpRate] = 12.f;
        p[gWarmth] = 28.f; p[gMix] = 62.f; p[gEq3F] = 6000.f; p[gEq3G] = 2.5f;
    } },
    { "Glam Lead Doubler", [](float* p) {
        // my own: micro pitch with a slow sweep, for leads
        presetDefaults(p); setVoices(p, 0);
        setVoice(p, 0, 11.f, 14.f, 0.2f, shSine, 0.f, 12.f);
        setVoice(p, 1, 20.f, 24.f, 0.2f, shSine, 0.f, -12.f);
        p[gSpPhase] = 360.f; p[gSpPan] = 100.f; p[gWarmth] = 15.f; p[gMix] = 42.f;
    } },
};

enum { kPresetCount = int(sizeof(kPresets) / sizeof(kPresets[0])) };

inline const char* presetWave(int i) { return kPresets[i].wave != nullptr ? kPresets[i].wave : kDefaultWave; }

} // namespace roj
