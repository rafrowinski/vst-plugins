// Guitar to MIDI Live - parameter definitions shared by the DSP side and the UI.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace swmidi {

enum Param {
    // detector (passed on to the Swarm Synth detector)
    pGate, pCut, pWindow, pHold,
    pDetClarity, pDetLegato, pDetLow, pDetHarm, pDetProbation, pDetRepick, pDetRefract, pDetTail, pDetBridge,
    // MIDI out
    pChannel, pOctave, pSemi, pBends, pBendRange, pLegato,
    pVelFull, pVelRange, pVelCurve, pVelMin, pVelMax,
    pExpr, pExprGain,
    pThru,
    kInputCount,
    // read-only values the plugin reports to the UI
    oNote = kInputCount,  // note being sent, after transpose (-1 = none)
    oBend,                // pitch relative to that note, semitones (what pitch bend carries)
    oVelocity,            // velocity of the last note-on
    oHz,                  // tracked pitch
    oLevel,               // input level 0..1
    oGate,                // 1 while the input is above the gate
    oLatency,             // how long the last note took to be recognised, ms
    oSerial,              // counts note-ons, so the UI can see a repeated note
    oActivity,            // counts all MIDI messages sent
    kParamCount,
    kOutputCount = kParamCount - kInputCount
};

enum Bends { bPitchBend, bSteps, bOff, kBendsCount };
static const char* const kBendNames[kBendsCount] = { "Pitch bend", "Note steps", "Ignore" };

enum Legato { lRetrigger, lOverlap, kLegatoCount };
static const char* const kLegatoNames[kLegatoCount] = { "Retrigger", "Overlap (glide)" };

static const int kBendRanges[] = { 1, 2, 3, 4, 5, 7, 12, 24, 48 };
enum { kBendRangeCount = int(sizeof(kBendRanges) / sizeof(kBendRanges[0])) };
static const char* const kBendRangeNames[kBendRangeCount] = {
    "+/- 1 st", "+/- 2 st", "+/- 3 st", "+/- 4 st", "+/- 5 st", "+/- 7 st", "+/- 12 st", "+/- 24 st", "+/- 48 st (MPE)"
};

static const int kExprCC[] = { -1, 1, 2, 7, 11, 74 };
enum { kExprCount = int(sizeof(kExprCC) / sizeof(kExprCC[0])) };
static const char* const kExprNames[kExprCount] = {
    "Off", "CC 1 (mod wheel)", "CC 2 (breath)", "CC 7 (volume)", "CC 11 (expression)", "CC 74 (brightness)"
};

static const char* const kChannelNames[16] = { "1", "2", "3", "4", "5", "6", "7", "8", "9", "10", "11", "12", "13", "14", "15", "16" };

enum Flags { fLog = 1, fInt = 2, fBool = 4 };

struct Def {
    const char* name;
    const char* sym;
    const char* unit;
    float min, max, def;
    int flags;
};

static const Def kDefs[kInputCount] = {
    { "Gate",                 "gate",          "dB",  -80.f, -20.f, -52.f, 0 },
    { "Cut",                  "cut",           "dB",  -60.f, -6.f,  -30.f, 0 },
    { "Tracking window",      "track",         "per", 0.5f,  2.f,   1.f,   0 },
    { "Hold",                 "hold",          "",    0.f,   1.f,   0.f,   fBool },
    { "Det clarity",          "det_clarity",   "%",   50.f,  95.f,  80.f,  0 },
    { "Det legato time",      "det_legato",    "ms",  2.f,   60.f,  20.f,  0 },
    { "Det low note time",    "det_low",       "ms",  2.f,   80.f,  30.f,  0 },
    { "Det octave time",      "det_harm",      "ms",  10.f,  120.f, 40.f,  0 },
    { "Det first-reading time","det_probation","ms",  0.f,   150.f, 60.f,  0 },
    { "Det pick sensitivity", "det_repick",    "x",   1.15f, 3.f,   1.5f,  0 },
    { "Det pick spacing",     "det_refract",   "ms",  15.f,  100.f, 40.f,  0 },
    { "Det tail hold",        "det_tail",      "%",   0.f,   60.f,  25.f,  0 },
    { "Det bridge notes",     "det_bridge",    "",    0.f,   1.f,   1.f,   fBool },
    { "MIDI channel",         "channel",       "",    0.f,   15.f,  0.f,   fInt },
    { "Octave",               "octave",        "oct", -3.f,  3.f,   0.f,   fInt },
    { "Semitones",            "semi",          "st",  -12.f, 12.f,  0.f,   fInt },
    { "Bends",                "bends",         "",    0.f,   2.f,   0.f,   fInt },
    { "Bend range",           "bend_range",    "",    0.f,   float(kBendRangeCount - 1), 1.f, fInt },
    { "Legato",               "legato",        "",    0.f,   1.f,   0.f,   fInt },
    { "Velocity full at",     "vel_full",      "dB",  -40.f, 0.f,   -12.f, 0 },
    { "Velocity range",       "vel_range",     "dB",  6.f,   48.f,  24.f,  0 },
    { "Velocity curve",       "vel_curve",     "%",   -100.f, 100.f, 0.f,  0 },
    { "Velocity min",         "vel_min",       "",    1.f,   127.f, 20.f,  fInt },
    { "Velocity max",         "vel_max",       "",    1.f,   127.f, 127.f, fInt },
    { "Expression CC",        "expr_cc",       "",    0.f,   float(kExprCount - 1), 0.f, fInt },
    { "Expression gain",      "expr_gain",     "dB",  0.f,   36.f,  12.f,  0 },
    { "Guitar through",       "thru",          "",    0.f,   1.f,   1.f,   fBool },
};

inline const Def& defOf(int index) { return kDefs[index]; }
inline float defaultOf(int index) { return kDefs[index].def; }
inline void symbolOf(int index, char* buf, size_t n) { std::snprintf(buf, n, "%s", kDefs[index].sym); }
inline void presetDefaults(float* p) { for (int i = 0; i < kInputCount; ++i) p[i] = kDefs[i].def; }

// starting points for the detector settings (also sets Window). Legato is longer than in Swarm Synth: in MIDI a
// note fretted just before it is picked would otherwise be sent twice (measured on a real recording: 18 ms needed).
struct DetProfile { const char* name; float window, clarity, legato, low, harm, probation, repick, refract, tail; };

static const DetProfile kDetProfiles[] = {
    //                         window clarity legato low  harm  probation repick refract tail
    { "Fast solo",              1.0f,  75.f,   8.f,  12.f, 25.f, 40.f,     1.35f, 25.f,   25.f },
    { "Balanced (default)",     1.0f,  80.f,  20.f,  30.f, 40.f, 60.f,     1.5f,  40.f,   25.f },
    { "Careful / let ring",     1.5f,  85.f,  25.f,  45.f, 60.f, 60.f,     1.6f,  50.f,   35.f },
};
enum { kDetProfileCount = int(sizeof(kDetProfiles) / sizeof(kDetProfiles[0])) };

inline void applyDetProfile(float* p, const DetProfile& d)
{
    p[pWindow] = d.window; p[pDetClarity] = d.clarity; p[pDetLegato] = d.legato; p[pDetLow] = d.low; p[pDetHarm] = d.harm;
    p[pDetProbation] = d.probation; p[pDetRepick] = d.repick; p[pDetRefract] = d.refract; p[pDetTail] = d.tail;
}

inline const char* noteName(int note, char* buf, size_t n)
{
    static const char* const kNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    if (note < 0 || note > 127) std::snprintf(buf, n, "--");
    else std::snprintf(buf, n, "%s%d", kNames[note % 12], note / 12 - 1);
    return buf;
}

} // namespace swmidi
