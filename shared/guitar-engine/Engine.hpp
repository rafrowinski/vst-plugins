// Swarm Synth - DSP engine (no plugin-framework dependency, so it can be tested offline)
//
// Two ways of turning the guitar into a synth, sharing the same oscillators, filter and envelopes:
//  - Pitch tracking: a pitch detector (Tracker.hpp) finds the note, the oscillators play it. Clean and stable,
//    but a note can only start once the detector has seen about two of its periods.
//  - Reshape: the guitar signal is narrowed to its fundamental and turned into a rotating phase
//    (analytic signal). The oscillators advance by exactly that rotation, sample by sample, so they follow
//    the string with no detection delay - and with all of its wobble.
#pragma once

#include "Params.hpp"
#include "Tracker.hpp"

#include <atomic>
#include <complex>
#include <vector>
#if defined(__SSE__) || defined(_M_X64)
# include <xmmintrin.h>
#endif

namespace swsyn {

enum { kMips = 10 };

// band-limited copies of the custom wave: level L keeps the first (512 >> L) harmonics
struct WaveSet { float t[kMips][kWaveSize + 1]; };

inline void fftInPlace(std::vector<std::complex<double> >& a, bool inverse)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = 2.0 * 3.14159265358979323846 / double(len) * (inverse ? 1.0 : -1.0);
        const std::complex<double> wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const std::complex<double> u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v; a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
    if (inverse) for (std::complex<double>& c : a) c /= double(n);
}

// table: kWaveSize values 0..1 (as produced by compileWave). Not for the audio thread.
inline void buildWaveSet(const float* table, WaveSet& ws)
{
    std::vector<std::complex<double> > spec(kWaveSize), tmp(kWaveSize);
    for (int i = 0; i < kWaveSize; ++i) spec[i] = std::complex<double>(2.0 * table[i] - 1.0, 0.0);
    fftInPlace(spec, false);
    spec[0] = 0.0; // no DC
    double peak = 1e-9;
    for (int l = 0; l < kMips; ++l)
    {
        const int harm = (kWaveSize / 2) >> l;
        tmp = spec;
        for (int k = 0; k < kWaveSize; ++k)
        {
            const int h = k <= kWaveSize / 2 ? k : kWaveSize - k;
            if (h > harm || (h == kWaveSize / 2)) tmp[k] = 0.0;
        }
        fftInPlace(tmp, true);
        for (int i = 0; i < kWaveSize; ++i)
        {
            ws.t[l][i] = float(tmp[i].real());
            if (l == 0) peak = std::max(peak, std::fabs(tmp[i].real()));
        }
        ws.t[l][kWaveSize] = ws.t[l][0];
    }
    const float g = float(1.0 / peak);
    for (int l = 0; l < kMips; ++l)
        for (int i = 0; i <= kWaveSize; ++i) ws.t[l][i] *= g;
}

// legato: the note changed without a new pick. level: input peak of the pick that started the note (0..1)
struct MidiEv { uint32_t frame; uint8_t status, note, velocity, legato; float level; };

class Engine
{
    enum { kCtrl = 16, kMaxMidi = 32 };

    struct Voice {
        double phase = 0.0;
        float ratio = 1.f;
        float gL = 0.f, gR = 0.f, gLT = 0.f, gRT = 0.f;
        float width = 0.5f, widthT = 0.5f;
        int wave = wSaw, mip = 0;
        bool audible = false;
        uint32_t rng = 1u;
        float pk[3] = {0, 0, 0};
        float d1 = 0.f, d2 = 0.f, r1 = 0.1f, r2 = 0.4f;   // drift: phases and rates (Hz)
        float scOff = 0.f, scMul = 1.f, scAge = 1e9f;      // scatter: start offset (cents), own time, age (s)
    };

    struct Svf {   // TPT state-variable low-pass
        float ic1 = 0, ic2 = 0, a1 = 0, a2 = 0, a3 = 0;
        void set(float sr, float fc, float q)
        {
            const float g = std::tan(3.14159265f * std::min(fc, sr * 0.45f) / sr), k = 1.f / q;
            a1 = 1.f / (1.f + g * (g + k)); a2 = g * a1; a3 = g * a2;
        }
        inline float lp(float x)
        {
            const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.f * v1 - ic1; ic2 = 2.f * v2 - ic2;
            return v2;
        }
        void clear() { ic1 = ic2 = 0.f; }
    };

    struct Allpass2 {  // y[n] = c * (x[n] + y[n-2]) - x[n-2]
        float c = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
        inline float tick(float x)
        {
            const float y = c * (x + y2) - x2;
            x2 = x1; x1 = x; y2 = y1; y1 = y;
            return y;
        }
        void clear() { x1 = x2 = y1 = y2 = 0.f; }
    };

    struct Ladder {  // four one-pole stages with feedback, solved without a delay in the loop
        float s[4] = {0, 0, 0, 0};
        inline float tick(float x, float g, float k, float drive, int slope)
        {
            const float G = g / (1.f + g), i1 = 1.f / (1.f + g);
            const float S = (G * G * G * s[0] + G * G * s[1] + G * s[2] + s[3]) * i1;
            float u = (x * drive - k * S) / (1.f + k * G * G * G * G);
            u = softClip(u);
            float y[4];
            float in = u;
            for (int i = 0; i < 4; ++i)
            {
                const float v = (in - s[i]) * G;
                y[i] = v + s[i];
                s[i] = y[i] + v;
                in = y[i];
            }
            return y[slope];
        }
        void clear() { s[0] = s[1] = s[2] = s[3] = 0.f; }
    };

    double fSr = 48000.0;
    Voice fV[kNumVoices];
    Tracker fTrk;
    int fCtrlCount = 0;

    // input level, gate, onsets
    float fEnvFast = 0.f, fEnvSlow = 0.f, fEnvFollow = 0.f;
    bool fGate = false;
    float fSinceTrig = 10.f;   // seconds since the last pick
    float fVelPeak = 0.f, fVel = 0.3f;
    float fPeakRef = 0.f;      // loudest recent playing, fades over a few seconds
    float fNotePeak = 0.f;     // loudest point of the current note
    bool fCut = false;         // the current note has decayed below the Cut level
    enum { kEnvRing = 512 };
    float fEnvRing[kEnvRing];  // input level over the last 40 ms, to tell a muted string from a decaying one
    uint32_t fEnvW = 0;
    int fEnvDelay = 120, fMuteCount = 0, fMuteNeed = 45;
    enum { kPkRing = 512 };
    float fPkRing[kPkRing];    // input peak of every control block
    uint32_t fPkW = 0;
    float fBlkMax = 0.f;
    int fPkWin = 41, fPkDelay = 83, fQuickMuteCount = 0, fQuickMuteNeed = 8;
    bool fEndOnMute = false;   // MIDI use: a muted string ends the note at once (see setEndOnMute)
    bool fMuteCut = false, fQuickOnset = false, fFalling = false;
    float fMuteFloor = 1.f, fThr = 0.f;
    bool fHold = false, fMuted = true;
    float fHoldAmp = 0.f;      // Hold with "Follow guitar": the loudest the note has been
    float fQNote = 69.f;       // Quantize: the semitone currently played
    bool fQuant = false;
    float fScatter = 0.f, fConverge = 4.f;   // Scatter (cents) and Converge (s)
    uint32_t fScRng = 0x2545F491u;            // own generator, so nothing else changes when Scatter is used
    float fSinceNote = 10.f, fVibPh = 0.f, fVibMul = 1.f, fVelOct = 0.f;

    // note state from the pitch detector
    float fPitch = 110.f;      // last accepted pitch, Hz
    bool fHave = false;        // a pitch has been accepted for the current note
    bool fPending = true;      // a pick was heard and its pitch is not known yet
    float fCand = 0.f, fLowCand = 0.f; int fLowCount = 0, fAgree = 0, fSlowAgree = 32, fLowAgree = 24, fLegAgree = 10, fProbation = 48, fSinceAccept = 100000;
    float fNeedClar = 0.8f, fRepick = 1.5f, fRefract = 0.04f, fTailLv = 0.25f;
    bool fBridge = true;
    float fLatencyMs = 0.f;
    int fMidiNote = -1;
    bool fNoteOn = false;      // the synth envelope gate

    // oscillator frequency
    float fBaseLog = std::log(110.f); // tracking mode: glides towards log(fPitch)
    float fBaseHz = 110.f;
    float fInstHz = 110.f;     // reshape mode: smoothed instantaneous frequency
    float fGlideC = 1.f, fInstC = 1.f;
    bool fGlideOff = true;

    // reshape mode: a fixed 12 dB/octave tilt from 150 Hz (so the fundamental is the strongest partial whatever
    // the note is - a filter that moved with the note would bend the pitch while moving), then two all-pass
    // chains 90 degrees apart
    Svf fPre;
    Allpass2 fHa[4], fHb[4];
    float fHaDelay = 0.f, fIPrev = 0.f, fQPrev = 0.f;
    enum { kCs = 8192 };
    double fCs[kCs];
    double fCsum = 0.0;
    uint32_t fCw = 0;
    int fSinceOnset = 0;
    float fBoxHz = 110.f;

    // envelopes
    enum { stIdle, stAttack, stDecay, stRelease };
    int fAdsrStage = stIdle;
    float fAdsr = 0.f, fKA = 1.f, fKD = 0.f, fKR = 0.f, fSus = 0.7f;
    bool fFltRising = false;
    float fFltEnvV = 0.f, fKFA = 1.f, fKFD = 0.f;
    float fValid = 0.f;        // fades the synth out while a new note is being recognised
    float fAmp = 0.f;

    // filter
    Ladder fLad[2];
    float fLadG = 0.1f, fLadGT = 0.1f, fLadK = 0.f, fLadDrive = 1.f, fLadMakeup = 1.f;
    int fLadSlope = 3;
    bool fFltOn = true;

    float fDryG = 0.f, fWetG = 1.f, fOutG = 1.f, fDryT = 0.f, fWetT = 1.f, fOutT = 1.f;
    float fSmooth = 0.001f;
    bool fFollowMode = true, fReshape = false;

    // custom wave: two sets, the audio thread reads the active one
    std::vector<WaveSet> fWave;
    std::atomic<int> fWaveActive { 0 };

    MidiEv fMidi[kMaxMidi];
    int fMidiCount = 0;
    bool fMidiOn = false;

    static inline float softClip(float x)
    {
        x = std::min(3.f, std::max(-3.f, x));
        return x * (27.f + x * x) / (27.f + 9.f * x * x);
    }

    static inline float polyBlep(float t, float dt)
    {
        if (t < dt) { t /= dt; return t + t - t * t - 1.f; }
        if (t > 1.f - dt) { t = (t - 1.f) / dt; return t * t + t + t + 1.f; }
        return 0.f;
    }

    void midi(uint32_t frame, bool on, int note, int vel, bool legato = false)
    {
        if (fMidiCount >= kMaxMidi) return;
        fMidi[fMidiCount++] = { frame, uint8_t(on ? 0x90 : 0x80), uint8_t(note), uint8_t(vel), uint8_t(legato ? 1 : 0), fVelPeak };
    }

    // picked: the note starts with a new pick, so even the same note number is played again
    void midiNote(uint32_t frame, float hz, bool picked)
    {
        if (!fMidiOn) return;
        const int note = std::min(127, std::max(0, int(std::lround(69.f + 12.f * std::log2(hz / 440.f)))));
        if (note == fMidiNote && !picked) return;
        if (fMidiNote >= 0) midi(frame, false, fMidiNote, 0, !picked);
        const int vel = std::min(127, std::max(1, int(std::sqrt(std::min(1.f, fVelPeak * 2.5f)) * 127.f)));
        midi(frame, true, note, vel, !picked);
        fMidiNote = note;
    }

    // Quantize: nearest semitone, with some stickiness so a note on the border does not flicker
    inline float quantHz(float hz)
    {
        const float m = 69.f + 12.f * std::log2(std::max(10.f, hz) / 440.f);
        if (std::fabs(m - fQNote) > 0.6f) fQNote = std::floor(m + 0.5f);
        return 440.f * std::pow(2.f, (fQNote - 69.f) / 12.f);
    }

    void endNote(uint32_t frame)
    {
        fNoteOn = false;
        if (fAdsrStage != stIdle) fAdsrStage = stRelease;
        midiOff(frame);
    }

    void midiOff(uint32_t frame)
    {
        if (fMidiNote >= 0) midi(frame, false, fMidiNote, 0);
        fMidiNote = -1;
    }

    // the synth's own envelopes start here
    float scRand() { fScRng = fScRng * 1664525u + 1013904223u; return float(fScRng >> 8) * (1.f / 16777216.f); }

    void noteStart()
    {
        fNoteOn = true; fMuted = false; fSinceNote = 0.f;
        // Scatter: every voice starts somewhere else and finds the note in its own time
        // (like a wall of free-running oscillators whose tuning curves cross at one point)
        if (fScatter > 0.5f)
            for (int i = 0; i < kNumVoices; ++i)
            {
                fV[i].scOff = fScatter * (2.f * scRand() - 1.f);
                fV[i].scMul = 0.55f + 0.9f * scRand();
                fV[i].scAge = 0.f;
            }
        fAdsrStage = stAttack;
        fFltRising = true;
    }

    // a new estimate from the pitch detector
    void onAnalysis(uint32_t frame)
    {
        const float f = fTrk.freq, cl = fTrk.clarity;
        // a clear pitch is wanted; if a note stays unclear for a while, settle for less
        const float need = (fPending && fSinceTrig > 0.045f) ? fNeedClar - 0.25f : fNeedClar;
        if (!fGate || fCut || !(f > 0.f) || cl < need) { fCand = 0.f; fAgree = 0; return; }
        // how far the note has decayed since it was picked
        const float lv = fEnvFast / std::max(1e-6f, fNotePeak);

        if (fPending || !fHave)
        {
            // two estimates in a row have to agree before a new note is believed
            // ...and a note far below the one before it has to last longer: right after a pick the old and the
            // new string sound together and briefly look like one low note
            // (30 ms worth of readings, which need not be in a row - a few stray ones in between are normal there)
            bool accept;
            if (fHave && f < 0.66f * fPitch)
            {
                if (fLowCount > 0 && std::fabs(f / fLowCand - 1.f) < 0.03f) ++fLowCount;
                else if (--fLowCount <= 0) { fLowCand = f; fLowCount = 1; }
                accept = fLowCount >= fLowAgree;
            }
            else accept = fCand > 0.f && std::fabs(f / fCand - 1.f) < 0.025f;
            if (accept)
            {
                const bool fromSilence = !fHave || fAmp < 0.003f;
                fPitch = f; fHave = true; fPending = false; fCand = 0.f; fAgree = 0; fSinceAccept = 0; fLowCount = 0;
                fLatencyMs = fSinceTrig * 1000.f;
                if (fromSilence || fGlideOff) fBaseLog = std::log(f);
                if (!fReshape) noteStart();
                midiNote(frame, f, true);
            }
            else fCand = f;
            return;
        }

        // The first reading of a note is made from very little signal and is sometimes wrong. For the first
        // 60 ms it is therefore held loosely: a lower note that keeps showing up replaces it quickly.
        if (fSinceAccept < 100000) ++fSinceAccept;
        const bool probation = fSinceAccept < fProbation;
        const float r = f / fPitch;
        if (r > 0.955f && r < 1.047f)
        {
            if (cl < fNeedClar + 0.05f) return;   // unclear reading: keep the pitch as it is
            fPitch += 0.6f * (f - fPitch);   // same note: follow bends and vibrato
            fCand = 0.f; fAgree = 0;
        }
        else if (lv < fTailLv || r < 0.4f)
        {
            // Also: two strings ringing together repeat at a period far below both notes. That shows up as a
            // drop of well over an octave with no new pick, and it is not a note either. (Measured on a real
            // let-ring recording: this alone removed every false low note.)
            // In the tail of a note the fundamental fades first and harmonics, hum and neighbouring strings
            // take over. A different pitch that turns up there is not a new note: hold what was played.
            fCand = 0.f; fAgree = 0;
        }
        else if (r < 0.66f && !probation)
        {
            // far below the held note and no pick was heard (a missed pick, or a pull-off to an open string):
            // the same patient count as after a pick, 40 ms worth of readings
            if (fLowCount > 0 && std::fabs(f / fLowCand - 1.f) < 0.03f) ++fLowCount;
            else if (--fLowCount <= 0) { fLowCand = f; fLowCount = 1; }
            fCand = 0.f; fAgree = 0;
            if (fLowCount >= fSlowAgree) { fPitch = f; fLowCount = 0; fSinceAccept = 0; midiNote(frame, f, false); }
        }
        else if (fEndOnMute && fFalling)
        {
            // MIDI use: the level is dropping fast - a string being muted while the next note is already
            // fretted. Not a hammer-on; the next pick will bring the note.
            fCand = 0.f; fAgree = 0;
        }
        else
        {
            // another note without a new pick (hammer-on, pull-off, slide): believe it once it has held for
            // about 12 ms. (Shorter, and the moment where both notes overlap produces false in-between notes;
            // measured against a real recording.)
            if (fCand > 0.f && std::fabs(f / fCand - 1.f) < 0.025f) ++fAgree; else fAgree = 1;
            fCand = f;
            // ...unless it is an octave or a fifth away from the held note: that is far more often a loud
            // harmonic than a real note, so it has to last about 40 ms
            const float lr = std::fabs(std::log2(r));
            const float frac = lr - std::floor(lr);
            const bool harmonic = frac < 0.05f || frac > 0.95f || std::fabs(frac - 0.585f) < 0.05f || std::fabs(frac - 0.415f) < 0.05f;
            if (harmonic && lv < 0.5f) { fAgree = 0; return; }
            if (fAgree >= (harmonic ? fSlowAgree : fLegAgree)) { fPitch = f; fCand = 0.f; fAgree = 0; midiNote(frame, f, false); }
        }
    }

    void control(const float* p)
    {
        const float sr = float(fSr);
        Eff eff[kNumVoices];
        const int n = computeEff(p, eff);
        const float norm = 1.f / std::sqrt(float(std::max(1, n)));
        const float dtBase = fBaseHz / sr;
        const float drift = p[gDrift], tick = float(kCtrl) / sr;
        fScatter = p[gScatter];
        fConverge = std::max(0.05f, p[gConverge] * 0.001f);

        for (int i = 0; i < kNumVoices; ++i)
        {
            Voice& v = fV[i];
            const float* vp = p + voiceIndex(i, 0);
            v.ratio = eff[i].ratio;
            if (drift > 0.01f)
            {
                // every voice wanders on its own: two slow sines per voice, rates that never line up
                v.d1 += v.r1 * tick; if (v.d1 >= 1.f) v.d1 -= 1.f;
                v.d2 += v.r2 * tick; if (v.d2 >= 1.f) v.d2 -= 1.f;
                const float c = drift * (0.6f * std::sin(6.28318531f * v.d1) + 0.4f * std::sin(6.28318531f * v.d2));
                v.ratio *= std::pow(2.f, c / 1200.f);
            }
            if (v.scAge < 1e8f)
            {
                v.scAge += tick;
                const float x = v.scAge / (fConverge * v.scMul);
                if (x >= 1.f || fScatter <= 0.5f) v.scAge = 1e9f;
                else v.ratio *= std::pow(2.f, v.scOff * 0.5f * (1.f + std::cos(3.14159265f * x)) / 1200.f);
            }
            v.wave = std::min(int(kWaveCount) - 1, std::max(0, int(vp[vWave] + 0.5f)));
            v.widthT = vp[vWidth] * 0.01f;
            const float lvl = eff[i].on ? vp[vLevel] * 0.01f * norm : 0.f;
            const float ang = (eff[i].pan + 1.f) * 0.25f * 3.14159265f;
            v.gLT = lvl * std::cos(ang);
            v.gRT = lvl * std::sin(ang);
            v.audible = eff[i].on || v.gL > 1e-5f || v.gR > 1e-5f;
            // custom wave: pick the copy whose highest harmonic still fits below half the sample rate
            const float dt = std::max(1e-6f, dtBase * v.ratio);
            v.mip = std::min(int(kMips) - 1, std::max(0, int(std::ceil(std::log2(float(kWaveSize) * dt)))));
        }

        fReshape = p[gMode] > 0.5f;
        fFollowMode = p[gEnvMode] < 0.5f;
        // a muted string: the level falls to under half within 40 ms and keeps falling like that for 15 ms.
        // A string left to ring never decays that fast.
        fEnvRing[fEnvW] = fEnvFast;
        const float before = fEnvRing[(fEnvW - uint32_t(fEnvDelay)) & (kEnvRing - 1)];
        fEnvW = (fEnvW + 1) & (kEnvRing - 1);
        fMuteCount = (fEnvFast < 0.47f * before) ? fMuteCount + 1 : 0;
        // the same, faster: input peaks over the last 15 ms against the 15 ms that ended 30 ms ago
        fPkRing[fPkW] = fBlkMax; fBlkMax = 0.f;
        fPkW = (fPkW + 1) & (kPkRing - 1);
        {
            float now = 0.f, past = 0.f;
            for (int k = 1; k <= fPkWin; ++k)
            {
                now = std::max(now, fPkRing[(fPkW - uint32_t(k)) & (kPkRing - 1)]);
                past = std::max(past, fPkRing[(fPkW - uint32_t(k + fPkDelay)) & (kPkRing - 1)]);
            }
            fQuickMuteCount = (now < 0.3f * past) ? fQuickMuteCount + 1 : 0;
            fFalling = past > 1e-6f && now < 0.5f * past;
            // after a mute the next pick is a clear rise above the quiet that followed the mute
            if (fMuteCut)
            {
                fMuteFloor = std::min(fMuteFloor, now);
                if (now > 4.f * fMuteFloor && now > 2.f * fThr) fQuickOnset = true;
            }
        }
        fHold = p[gHold] > 0.5f;
        fQuant = p[gQuant] > 0.5f;

        // detector fine-tuning: times become a number of detector readings (one every 1.25 ms)
        const float per = fTrk.analysisInterval() * 1000.f;
        fNeedClar = p[gDetClarity] * 0.01f;
        fLegAgree = std::max(1, int(p[gDetLegato] / per + 0.5f));
        fLowAgree = std::max(1, int(p[gDetLow] / per + 0.5f));
        fSlowAgree = std::max(2, int(p[gDetHarm] / per + 0.5f));
        fProbation = int(p[gDetProbation] / per + 0.5f);
        fRepick = p[gDetRepick];
        fRefract = p[gDetRefract] * 0.001f;
        fTailLv = p[gDetTail] * 0.01f;
        fBridge = p[gDetBridge] > 0.5f;

        // vibrato comes in a moment after the note starts, like a player adding it to a held note
        fSinceNote += float(kCtrl) / sr;
        fVibPh += p[gVibRate] * float(kCtrl) / sr;
        if (fVibPh >= 1.f) fVibPh -= 1.f;
        const float vfade = std::min(1.f, std::max(0.f, (fSinceNote - 0.15f) / 0.35f));
        fVibMul = p[gVibDepth] > 0.01f ? std::pow(2.f, p[gVibDepth] * vfade * std::sin(6.28318531f * fVibPh) / 1200.f) : 1.f;
        fVelOct = p[gVelFlt] * std::min(1.f, fVel * 2.2f);
        fMidiOn = p[gMidiOut] > 0.5f;

        // glide
        const float glide = p[gGlide] * 0.001f;
        fGlideOff = glide < 0.0005f;
        fGlideC = fGlideOff ? 1.f : 1.f - std::exp(-1.f / (glide * sr));
        fInstC = 1.f - std::exp(-1.f / ((0.0004f + glide) * sr));

        // envelopes (time = time to cover about 95 % of the way)
        const auto coef = [sr](float ms) { return 1.f - std::exp(-3.f / (std::max(0.05f, ms) * 0.001f * sr)); };
        fKA = coef(p[gAttack]); fKD = coef(p[gDecay]); fKR = coef(p[gRelease]);
        fSus = p[gSustain] * 0.01f;
        fKFA = coef(p[gFltAtk]); fKFD = coef(p[gFltDec]);

        // filter
        fFltOn = p[gFltOn] > 0.5f;
        if (fFltOn)
        {
            const float oct = fVelOct + p[gFltEnv] * fFltEnvV + p[gKey] * 0.01f * std::log2(std::max(20.f, fBaseHz) / 261.63f);
            const float fc = std::min(std::min(20000.f, sr * 0.45f), std::max(20.f, p[gCutoff] * std::pow(2.f, oct)));
            fLadGT = std::tan(3.14159265f * fc / sr);
            fLadK = 3.9f * p[gReso] * 0.01f;
            fLadDrive = std::pow(10.f, p[gDrive] / 20.f);
            // the feedback lowers the level below the cutoff, and drive raises it: keep the loudness roughly even
            fLadMakeup = (1.f + 0.6f * fLadK) / std::pow(fLadDrive, 0.75f);
            fLadSlope = std::min(3, std::max(0, int(p[gSlope] + 0.5f)));
        }

        const float mix = p[gMix] * 0.01f;
        fDryT = std::cos(mix * 1.5707963f);
        fWetT = std::sin(mix * 1.5707963f);
        fOutT = std::pow(10.f, p[gOutput] / 20.f);
    }

public:
    Engine() : fWave(2)
    {
        static const float kA[4] = { 0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f };
        static const float kB[4] = { 0.4021921162426f, 0.8561710882420f, 0.9722909545651f, 0.9952884791278f };
        for (int i = 0; i < 4; ++i) { fHa[i].c = kA[i] * kA[i]; fHb[i].c = kB[i] * kB[i]; }
        for (int i = 0; i < kNumVoices; ++i)
        {
            fV[i].rng = 0x1234567u * uint32_t(i + 1) + 99u;
            fV[i].phase = double(hash01(uint32_t(i), 5u));
            fV[i].d1 = hash01(uint32_t(i), 11u); fV[i].d2 = hash01(uint32_t(i), 12u);
            fV[i].r1 = 0.05f + 0.22f * hash01(uint32_t(i), 13u);
            fV[i].r2 = 0.31f + 0.55f * hash01(uint32_t(i), 14u);
        }
        std::vector<float> table(kWaveSize);
        compileWave(kDefaultWave, table.data());
        buildWaveSet(table.data(), fWave[0]);
        fWave[1] = fWave[0];
        setSampleRate(48000.0);
    }

    // called from a non-audio thread with a compiled table of kWaveSize values (0..1)
    void setWave(const float* table)
    {
        const int next = 1 - fWaveActive.load(std::memory_order_acquire);
        buildWaveSet(table, fWave[next]);
        fWaveActive.store(next, std::memory_order_release);
    }

    void setSampleRate(double sr)
    {
        fSr = sr;
        fTrk.setSampleRate(sr);
        fSmooth = 1.f - std::exp(-1.f / (0.01f * float(sr)));
        fEnvDelay = std::min(int(kEnvRing) - 1, std::max(1, int(0.040 * sr / kCtrl)));
        fMuteNeed = std::max(2, int(0.015 * sr / kCtrl));
        fPkWin = std::max(4, int(0.015 * sr / kCtrl + 0.5));
        fPkDelay = std::min(int(kPkRing) - fPkWin - 2, std::max(fPkWin, int(0.030 * sr / kCtrl + 0.5)));
        fQuickMuteNeed = std::max(2, int(0.003 * sr / kCtrl + 0.5));
        fPre.set(float(sr), 150.f, 0.5f);
        reset();
    }

    void reset()
    {
        fTrk.reset();
        for (Voice& v : fV) { v.gL = v.gR = 0.f; v.pk[0] = v.pk[1] = v.pk[2] = 0.f; }
        fPre.clear();
        for (Allpass2& a : fHa) a.clear();
        for (Allpass2& a : fHb) a.clear();
        for (Ladder& l : fLad) l.clear();
        fHaDelay = fIPrev = fQPrev = 0.f;
        std::memset(fCs, 0, sizeof(fCs)); fCsum = 0.0; fCw = 0; fSinceOnset = 0;
        fEnvFast = fEnvSlow = fEnvFollow = 0.f; fPeakRef = fNotePeak = 0.f; fCut = false;
        std::memset(fEnvRing, 0, sizeof(fEnvRing)); fEnvW = 0; fMuteCount = 0;
        std::memset(fPkRing, 0, sizeof(fPkRing)); fPkW = 0; fBlkMax = 0.f; fQuickMuteCount = 0;
        fMuteCut = fQuickOnset = fFalling = false; fMuteFloor = 1.f; fMuted = true; fHoldAmp = 0.f; fSinceNote = 10.f; fVibMul = 1.f; fGate = false; fSinceTrig = 10.f;
        fHave = false; fPending = true; fCand = 0.f; fAgree = 0; fNoteOn = false;
        fAdsr = 0.f; fAdsrStage = stIdle; fFltEnvV = 0.f; fFltRising = false; fValid = 0.f; fAmp = 0.f;
        fCtrlCount = 0; fMidiNote = -1; fMidiCount = 0;
        fDryG = fDryT; fWetG = fWetT; fOutG = fOutT; fLadG = fLadGT;
    }

    // MIDI use: end a note as soon as the string is muted by hand (off by default; the synth does not need it)
    void setEndOnMute(bool on) { fEndOnMute = on; }

    // the note being played (MIDI number, -1 = none) and its exact tracked pitch, which follows bends
    int currentNote() const { return fMidiNote; }
    float trackedHz() const { return fPitch; }

    // MIDI notes produced by the last process() call (only when the MIDI out parameter is on)
    int midiCount() const { return fMidiCount; }
    const MidiEv& midiEvent(int i) const { return fMidi[i]; }

    // p: kInputCount parameter values. outParams: kOutputCount values.
    void process(const float* inL, const float* inR, float* outL, float* outR, uint32_t frames,
                 const float* p, float* outParams)
    {
#if defined(__SSE__) || defined(_M_X64)
        const unsigned int oldCsr = _mm_getcsr();
        _mm_setcsr(oldCsr | 0x8040); // flush-to-zero + denormals-are-zero
#endif
        fMidiCount = 0;
        if (!(p[gMidiOut] > 0.5f) && fMidiNote >= 0) { fMidiOn = true; midiOff(0); }

        const float sr = float(fSr);
        const float invSr = 1.f / sr;
        const float thr = std::pow(10.f, p[gGate] / 20.f);
        fThr = thr;
        const float periods = p[gTrack];
        const float dyn = p[gDyn] * 0.01f;
        const float cutRel = std::pow(10.f, p[gCut] / 20.f);
        const float comp = p[gComp] * 0.01f;
        const float compGain = std::pow(0.5f, comp);
        const float peakDecay = std::exp(-1.f / (3.f * sr));
        const float fallC = 1.f - std::exp(-1.f / (0.012f * sr));

        const float atkF = 1.f - std::exp(-1.f / (0.001f * sr));
        const float relF = 1.f - std::exp(-1.f / (0.040f * sr));
        // a lagging copy of the level: it drops with the string and rises slowly, so a new pick stands out
        // against it even when the previous note is still ringing
        const float atkS = 1.f - std::exp(-1.f / (0.020f * sr));
        const float relS = 1.f - std::exp(-1.f / (0.010f * sr));
        const float atkE = 1.f - std::exp(-1.f / (0.003f * sr));
        const float relE = 1.f - std::exp(-1.f / (0.060f * sr));
        const float validC = 1.f - std::exp(-1.f / (0.002f * sr));
        const float velC = 1.f - std::exp(-1.f / (0.008f * sr));
        const float sg = fSmooth;
        const float twoPiInv = sr / 6.28318531f;
        const WaveSet& ws = fWave[fWaveActive.load(std::memory_order_acquire)];

        for (uint32_t i = 0; i < frames; ++i)
        {
            const float xin[2] = { inL[i], inR[i] };
            const float x = 0.5f * (xin[0] + xin[1]);

            // input level, gate and pick detection
            const float a = std::fabs(x);
            fBlkMax = std::max(fBlkMax, a);
            fEnvFast += (a > fEnvFast ? atkF : relF) * (a - fEnvFast);
            fEnvSlow += (fEnvFast > fEnvSlow ? atkS : relS) * (fEnvFast - fEnvSlow);
            fEnvFollow += (a > fEnvFollow ? atkE : relE) * (a - fEnvFollow);
            fSinceTrig += invSr;
            bool onset = false;
            if (!fGate) { if (fEnvFast > thr) { fGate = true; onset = true; } }
            else
            {
                if (fEnvFast < thr * 0.5f)
                {
                    // with Hold the note carries on at its pitch after the string has died away
                    fGate = false;
                    if (!fHold) { fHave = false; endNote(i); }
                }
                // a new pick: a clear rise, and not just a wobble in the quiet tail of a note
                else if (fSinceTrig > fRefract && fEnvFast > thr * 2.f && fEnvFast > fEnvSlow * fRepick && fEnvFast > 0.15f * fPeakRef) onset = true;
                else if (!fCut && fSinceTrig > 0.03f && fEnvFast < fNotePeak * cutRel)
                {
                    // decayed below the Cut level: end the note here instead of chasing what is left of it
                    fCut = true;
                    if (!fHold) endNote(i);
                }
                else if (fQuickOnset && fSinceTrig > fRefract) onset = true;
                // picked but never recognised (noise, two strings): go back to the note that was playing
                if (fPending && fHave && fSinceTrig > (fLowCount > 2 ? 0.15f : 0.07f)) fPending = false;
            }
            // Hold: the note ends when the strings are muted (the level collapses much faster than a string
            // decays - see control()), or when Hold is switched off after the string has gone quiet
            if (fNoteOn || (fFollowMode && !fMuted))
            {
                const bool gone = !fGate || fCut;
                if ((fHold && fGate && fSinceTrig > 0.12f && fMuteCount >= fMuteNeed) || (!fHold && gone))
                {
                    fMuted = true; fCut = true;
                    if (!fGate) fHave = false;
                    endNote(i);
                }
            }
            // MIDI: a string muted by hand (the level collapses by more than 10 dB within 30 ms) ends the note
            // right there. Otherwise the note would hang on until the next pick, and the new note that is
            // already fretted but not picked yet could be taken for a hammer-on.
            if (fEndOnMute && fHave && !fCut && fGate && fSinceTrig > 0.05f && fQuickMuteCount >= fQuickMuteNeed)
            {
                fCut = true; fHave = false;
                fMuteCut = true; fMuteFloor = 1.f; fQuickOnset = false;
                endNote(i);
            }
            fPeakRef = std::max(fEnvFast, fPeakRef * peakDecay);
            fNotePeak = std::max(fNotePeak, fEnvFast);
            if (onset)
            {
                fSinceTrig = 0.f; fPending = true; fCand = 0.f; fAgree = 0; fLowCount = 0; fVelPeak = fEnvFast;
                fNotePeak = fEnvFast; fCut = false; fMuteCut = false; fQuickOnset = false; fHoldAmp = 0.f;
                if (fFollowMode) fMuted = false;
                if (fReshape) { noteStart(); fLatencyMs = 0.f; }
                // start the one-period average afresh (entry "0 samples ago" must read as zero)
                fCsum = 0.0; fSinceOnset = 0; fCs[(fCw - 1u) & (kCs - 1)] = 0.0;
                fBoxHz = fBaseHz;
            }
            if (fSinceTrig < 0.02f) fVelPeak = std::max(fVelPeak, fEnvFast);
            fVel += velC * (fVelPeak - fVel);

            if (fTrk.push(x, periods, (fHave && !fPending) ? fPitch : 0.f, fSinceAccept < fProbation ? 0.9f : 0.8f)) onAnalysis(i);

            if (fCtrlCount <= 0) { control(p); fCtrlCount = kCtrl; }
            --fCtrlCount;

            // frequency of the oscillators
            if (fReshape)
            {
                const float y = fPre.lp(x);
                float ia = y, qb = y;
                for (int k = 0; k < 4; ++k) { ia = fHa[k].tick(ia); qb = fHb[k].tick(qb); }
                const float iS = fHaDelay; fHaDelay = ia;
                const float cross = fIPrev * qb - fQPrev * iS, dot = fIPrev * iS + fQPrev * qb;
                fIPrev = iS; fQPrev = qb;
                if (fGate && !fCut && (cross * cross + dot * dot) > 1e-18f)
                {
                    // the rotation may briefly run backwards when a harmonic is strong; it has to be averaged
                    // as it is, otherwise the pitch comes out sharp
                    const float hz = std::min(4000.f, std::max(-4000.f, -std::atan2(cross, dot) * twoPiInv));
                    // Harmonics that get through make the rotation speed swing once per period of the note.
                    // Averaging over exactly one period removes that swing. Right after a pick only the
                    // samples since the pick are averaged, so the old note never leaks into the new one.
                    fCsum += double(hz);
                    fCs[fCw] = fCsum;
                    fCw = (fCw + 1) & (kCs - 1);
                    if (fSinceOnset < kCs - 2) ++fSinceOnset;
                    // the first 2 ms after a pick are mostly pick noise and are left out
                    const int skip = int(0.002f * sr);
                    if (fSinceOnset >= skip + 8)
                    {
                        const float per = (fHave && !fPending) ? fPitch : fBoxHz;
                        const int want = std::min(int(kCs) - 2, std::max(8, int(sr / std::max(40.f, per) + 0.5f)));
                        const int L = std::min(want, fSinceOnset - skip);
                        const double older = fCs[(fCw - 1u - uint32_t(L)) & (kCs - 1)];
                        fBoxHz = std::min(4000.f, std::max(30.f, float((fCsum - older) / double(L))));
                    }
                    // Once the detector knows the note it keeps this reading on a leash: a reading near a whole
                    // multiple of the note means a harmonic is winning and is divided back; a reading that fits
                    // nothing (two partials of equal strength) is replaced by the detected pitch. Within about a
                    // semitone of the note the instant reading is used as it is, so bends and vibrato stay immediate.
                    float target = fBoxHz;
                    if (fHave && !fPending)
                    {
                        const float r = target / fPitch, rr = std::max(1.f, std::floor(r + 0.5f));
                        if (rr <= 6.f && std::fabs(r - rr) < 0.06f * rr) target /= rr; else target = fPitch;
                        // in the tail of a note the fundamental is too weak to steer by: use the detected pitch
                        if (fEnvFast < 0.35f * fNotePeak) target = fPitch;
                    }
                    fInstHz += fInstC * (target - fInstHz);
                }
                fBaseHz = std::min(4000.f, std::max(20.f, fInstHz));
                if (fQuant) fBaseHz = quantHz(fBaseHz);
                fBaseLog = std::log(fBaseHz);
            }
            else
            {
                const float target = std::log(fQuant ? quantHz(fPitch) : fPitch);
                fBaseLog += fGlideC * (target - fBaseLog);
                fBaseHz = std::exp(fBaseLog);
                fInstHz = fBaseHz;
            }
            const float dtBase = fBaseHz * invSr * fVibMul;

            // oscillators
            float wet[2] = { 0.f, 0.f };
            for (int vi = 0; vi < kNumVoices; ++vi)
            {
                Voice& v = fV[vi];
                const float dt = std::min(0.45f, dtBase * v.ratio);
                v.phase += double(dt);
                if (v.phase >= 1.0) v.phase -= 1.0;
                if (!v.audible) continue;
                v.gL += sg * (v.gLT - v.gL);
                v.gR += sg * (v.gRT - v.gR);
                v.width += sg * (v.widthT - v.width);
                const float t = float(v.phase);
                float s;
                switch (v.wave)
                {
                default:
                case wSine: s = std::sin(6.28318531f * t); break;
                case wTriangle: s = t < 0.25f ? 4.f * t : (t < 0.75f ? 2.f - 4.f * t : 4.f * t - 4.f); break;
                case wSaw: s = 2.f * t - 1.f - polyBlep(t, dt); break;
                case wPulse: {
                    const float w = v.width;
                    float t2 = t + 1.f - w; if (t2 >= 1.f) t2 -= 1.f;
                    s = (t < w ? 1.f : -1.f) + polyBlep(t, dt) - polyBlep(t2, dt) - (2.f * w - 1.f);
                    break;
                }
                case wWhite:
                case wPink: {
                    v.rng = v.rng * 1664525u + 1013904223u;
                    const float wn = float(int32_t(v.rng)) * (1.f / 2147483648.f);
                    if (v.wave == wWhite) { s = wn * 0.7f; break; }
                    v.pk[0] = 0.99765f * v.pk[0] + wn * 0.0990460f;
                    v.pk[1] = 0.96300f * v.pk[1] + wn * 0.2965164f;
                    v.pk[2] = 0.57000f * v.pk[2] + wn * 1.0526913f;
                    s = (v.pk[0] + v.pk[1] + v.pk[2] + wn * 0.1848f) * 0.22f;
                    break;
                }
                case wCustom: {
                    const float* const tb = ws.t[v.mip];
                    const float pos = t * float(kWaveSize);
                    const int ip = int(pos);
                    s = tb[ip] + (tb[ip + 1] - tb[ip]) * (pos - float(ip));
                    break;
                }
                }
                wet[0] += s * v.gL;
                wet[1] += s * v.gR;
            }

            // envelopes
            switch (fAdsrStage)
            {
            case stAttack: fAdsr += fKA * (1.25f - fAdsr); if (fAdsr >= 1.f) { fAdsr = 1.f; fAdsrStage = stDecay; } break;
            case stDecay: fAdsr += fKD * (fSus - fAdsr); break;
            case stRelease: fAdsr += fKR * (0.f - fAdsr); if (fAdsr < 1e-5f) { fAdsr = 0.f; fAdsrStage = stIdle; } break;
            default: break;
            }
            if (fFltRising) { fFltEnvV += fKFA * (1.25f - fFltEnvV); if (fFltEnvV >= 1.f) { fFltEnvV = 1.f; fFltRising = false; } }
            else fFltEnvV += fKFD * (0.f - fFltEnvV);

            // From silence the synth waits until the first note is recognised. Between notes it keeps playing
            // the previous one until the new one is known (legato), so there is no hole at every pick.
            const bool valid = fReshape || (fHave && (fBridge || !fPending));
            fValid += validC * ((valid ? 1.f : 0.f) - fValid);

            float amp;
            if (fFollowMode)
            {
                // Compress evens out the loudness: at 100 % the synth holds one level for as long as the note lasts
                amp = std::min(1.5f, fEnvFollow * 2.5f);
                if (comp > 0.001f) amp = std::pow(std::max(1e-5f, amp), 1.f - comp) * compGain;
                if (!fGate || fCut) amp = 0.f;
                if (fHold)
                {
                    // Hold: stay at the loudest point of the note until the next pick or a mute
                    fHoldAmp = std::max(fHoldAmp, amp);
                    amp = fMuted ? 0.f : fHoldAmp;
                }
            }
            else amp = fAdsr * ((1.f - dyn) * 0.45f + dyn * std::min(1.f, fVel * 2.2f));
            amp *= fValid;
            fAmp += (amp > fAmp ? validC : fallC) * (amp - fAmp);

            if (fFltOn)
            {
                fLadG += sg * 8.f * (fLadGT - fLadG);
                wet[0] = fLad[0].tick(wet[0], fLadG, fLadK, fLadDrive, fLadSlope) * fLadMakeup;
                wet[1] = fLad[1].tick(wet[1], fLadG, fLadK, fLadDrive, fLadSlope) * fLadMakeup;
            }

            fDryG += sg * (fDryT - fDryG);
            fWetG += sg * (fWetT - fWetG);
            fOutG += sg * (fOutT - fOutG);
            const float wg = fWetG * fAmp;
            outL[i] = (xin[0] * fDryG + wet[0] * wg) * fOutG;
            outR[i] = (xin[1] * fDryG + wet[1] * wg) * fOutG;
        }

        if (outParams != nullptr)
        {
            const bool sounding = fAmp > 0.002f;
            outParams[kOutFreq - kInputCount] = sounding ? fBaseHz : 0.f;
            outParams[kOutEnv - kInputCount] = std::min(1.f, fEnvFollow * 2.5f);
            outParams[kOutLatency - kInputCount] = fLatencyMs;
            outParams[kOutGate - kInputCount] = fGate ? 1.f : 0.f;
        }
#if defined(__SSE__) || defined(_M_X64)
        _mm_setcsr(oldCsr);
#endif
    }
};

} // namespace swsyn
