// Swarm Delay - DSP engine (no plugin-framework dependency, so it can be tested offline)
//
// Every voice is its own stereo delay line with its own feedback loop. A voice reads its line in one of
// three ways:
//   - plain tap            (forward, no pitch change)
//   - two crossfading taps (reverse and/or pitch shift: the taps slide through the line)
//   - loop                 (freeze / stutter: a slice ending at an anchor point is repeated)
// Switching between them, and "Jump" time changes, go through a 3 ms gate so nothing clicks.
#pragma once

#include "Params.hpp"

#include <vector>
#if defined(__SSE__) || defined(_M_X64)
# include <xmmintrin.h>
#endif

namespace swd {

class Engine
{
    enum { kCtrl = 16 };

    struct Voice {
        std::vector<float> buf[2];
        // time
        double dBase = -1.0;        // delay in samples before the glitch multiplier
        double dTarget = 480.0;
        bool   tape = true;
        bool   jumpPending = false;
        // what the voice is doing now / what it should be doing (applied through the gate)
        bool   revActive = false, revWant = false;
        bool   loopActive = false, loopWant = false;
        float  mulActive = 1.f, mulWant = 1.f;
        float  fracActive = 1.f, fracWant = 1.f;
        bool   swKick = false;      // time sweep: jump to the start offset at the next gate
        double swMul = 1.0;
        float  gate = 1.f;
        int    gateDir = 0;         // -1 closing, +1 opening
        // loop reader
        uint32_t anchor = 0;
        double loopPos = 0.0;
        // sliding taps
        double shPos = 0.0;
        float  ratio = 1.f;
        bool   pitched = false;
        // chorus on the repeats
        double chPh = 0.0;
        // random glitches
        double glCount = 0.0;
        int    glState = 0;
        float  stutFrac = 0.25f;
        // per-channel state
        float last[2] = {0, 0};     // what goes back into the line
        float lc[2] = {0, 0}, hc[2] = {0, 0}, lp[2] = {0, 0};
        float crPh[2] = {1, 1}, hold[2] = {0, 0};
        float fb = 0.f, crushAmt = 1.f;
        float gL = 0.f, gR = 0.f, gLT = 0.f, gRT = 0.f;
        bool  audible = false;
        double dNow = 0.0;
    };

    double fSr = 48000.0;
    uint32_t fMask = 0, fW = 0;
    Voice fV[kNumVoices];
    int fCtrlCount = 0;
    uint32_t fRng = 0x1234ABCDu;

    double fMaxD = 192000.0, fWin = 2400.0;
    float fGateStep = 0.01f, fTapeC = 0.0001f, fSmoothGain = 0.001f;
    bool fFrozen = false, fPingPong = false;
    bool fWarmOn = false; float fWarmC = 1.f, fWarmDrive = 1.f;
    bool fLoOn = false, fHiOn = false; float fLoC = 0.f, fHiC = 1.f;
    bool fChOn = false; float fChA = 0.f, fChMix = 0.5f; double fChInc = 0.0;
    bool fCrOn = false, fCrLoop = true; float fCrSteps = 32768.f, fCrInc = 1.f;
    float fEnv = 0.f, fDuckAmt = 0.f, fDuckSens = 1.f, fDuckRelC = 0.001f, fDuckNow = 0.f;
    float fDryG = 1.f, fWetG = 0.f, fOutG = 1.f, fDryT = 1.f, fWetT = 0.f, fOutT = 1.f;
    float fBpm = 120.f;
    double fSwCount = 0.0, fSwStart = 1.0, fSwC = 0.001;
    bool fSwOneWay = false; double fSwLin = 0.0, fSwLo = 1.0, fSwHi = 1.0; // one-way sweep: linear ramp away from the voice's own time

    static inline float softClip(float x)
    {
        x = std::min(3.f, std::max(-3.f, x));
        return x * (27.f + x * x) / (27.f + 9.f * x * x);
    }

    // sample d (>= 1) samples before write position w, cubic interpolation
    static inline float readTap(const float* b, uint32_t mask, uint32_t w, double d)
    {
        const double fl = std::floor(d);
        const float f = 1.f - float(d - fl);
        const uint32_t i1 = w - uint32_t(int(fl)) - 1u;
        const float x0 = b[(i1 - 1u) & mask];
        const float x1 = b[i1 & mask];
        const float x2 = b[(i1 + 1u) & mask];
        const float x3 = b[(i1 + 2u) & mask];
        const float c1 = 0.5f * (x2 - x0);
        const float c2 = x0 - 2.5f * x1 + 2.f * x2 - 0.5f * x3;
        const float c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
        return ((c3 * f + c2) * f + c1) * f + x1;
    }

    inline float rnd()
    {
        fRng ^= fRng << 13; fRng ^= fRng >> 17; fRng ^= fRng << 5;
        return float(fRng >> 8) * (1.0f / 16777216.0f);
    }

    void applyPending(Voice& v)
    {
        v.revActive = v.revWant;
        v.mulActive = v.mulWant;
        if (!v.tape) v.dBase = v.dTarget;
        if (v.loopWant && (!v.loopActive || v.fracWant != v.fracActive)) { v.anchor = fW; v.loopPos = 0.0; }
        v.loopActive = v.loopWant;
        v.fracActive = v.fracWant;
        if (v.swKick) { v.swMul = fSwStart; v.swKick = false; }
        v.shPos = 0.0;
    }

    void control(const float* p)
    {
        const float sr = float(fSr);
        static const float kTwoPi = 6.2831853f;
        Eff eff[kNumVoices];
        const int n = computeEff(p, eff, fBpm);
        const float norm = std::pow(float(std::max(1, n)), -0.5f);
        const bool jump = p[gTimeMode] > 0.5f;

        fFrozen = p[gFreeze] > 0.5f;
        fPingPong = p[gPingPong] > 0.5f;

        // warmth: darker and slightly saturated repeats, like a bucket-brigade or tape delay
        const float warm = p[gWarmth] * 0.01f;
        fWarmOn = warm > 0.005f;
        if (fWarmOn)
        {
            const float fc = std::min(sr * 0.45f, 18000.f * std::pow(2200.f / 18000.f, warm));
            fWarmC = 1.f - std::exp(-kTwoPi * fc / sr);
            fWarmDrive = 1.f + 2.5f * warm;
        }

        // filters in the feedback loop
        fLoOn = p[gLoCut] > 21.f;
        fLoC = 1.f - std::exp(-kTwoPi * p[gLoCut] / sr);
        fHiOn = p[gHiCut] < 19900.f;
        fHiC = 1.f - std::exp(-kTwoPi * std::min(p[gHiCut], sr * 0.45f) / sr);

        // chorus on the repeats: a sine wobble of the read position, deep enough to detune by "ct" cents
        fChOn = p[gChDetune] > 0.05f;
        if (fChOn)
        {
            const float dev = std::pow(2.f, p[gChDetune] / 1200.f) - 1.f;
            fChA = std::min(0.02f * sr, sr * dev / (kTwoPi * p[gChRate]));
            fChInc = double(p[gChRate]) / fSr;
            fChMix = p[gChMix] * 0.01f;
        }

        // crusher
        const bool rateOn = p[gCrRate] < 47900.f && p[gCrRate] < sr * 0.98f;
        fCrOn = p[gBits] < 15.95f || rateOn;
        fCrSteps = std::pow(2.f, p[gBits] - 1.f);
        fCrInc = rateOn ? p[gCrRate] / sr : 1.f;
        fCrLoop = p[gCrLoop] > 0.5f;

        // random glitches
        const float prob = p[gGlProb] * 0.01f;
        int types[3], nTypes = 0;
        if (p[gGlRev] > 0.5f)  types[nTypes++] = 1;
        if (p[gGlJump] > 0.5f) types[nTypes++] = 2;
        if (p[gGlStut] > 0.5f) types[nTypes++] = 3;

        // time sweep: once per cycle every voice jumps to a shorter (Up) or longer (Down) time and glides back
        const int swMode = int(p[gSwMode] + 0.5f);
        bool kick = false;
        if (swMode != 0)
        {
            const double period = std::max(0.02, double(sweepPeriod(p, fBpm))) * fSr;
            const double depth = double(p[gSwDepth]) * 0.01;
            const double glide = double(p[gSwGlide]) * 0.01;
            fSwOneWay = p[gSwOneWay] > 0.5f;
            fSwCount -= double(kCtrl);
            if (fSwCount <= 0.0) { fSwCount = fSwCount < -period ? period : fSwCount + period; kick = depth > 0.001; }
            if (fSwOneWay)
            {
                // start on the voice's own time, travel one way for "glide" of the cycle, hold, start again
                fSwStart = 1.0;
                fSwLo = std::min(1.0, 1.0 + (swMode == 1 ? depth : -depth));
                fSwHi = std::max(1.0, 1.0 + (swMode == 1 ? depth : -depth));
                const bool moving = (period - fSwCount) < glide * period;
                fSwLin = moving ? (swMode == 1 ? depth : -depth) / (glide * period) : 0.0;
            }
            else
            {
                fSwStart = swMode == 1 ? 1.0 - depth : 1.0 + depth;
                fSwC = 1.0 - std::exp(-1.0 / (period * 0.25 * glide));
            }
        }
        else { fSwOneWay = false; fSwCount = 0.0; fSwC = 1.0 - std::exp(-1.0 / (0.05 * fSr)); }

        for (int i = 0; i < kNumVoices; ++i)
        {
            Voice& v = fV[i];
            const float* vp = p + voiceIndex(i, 0);
            const Eff& e = eff[i];
            if (kick) v.swKick = true; else if (swMode == 0) v.swKick = false;

            v.dTarget = std::min(fMaxD, std::max(8.0, double(e.timeMs) * 0.001 * fSr));
            if (v.dBase < 0.0) v.dBase = v.dTarget;
            v.tape = !jump;

            // once per repeat, roll the dice
            if (e.on)
            {
                v.glCount -= double(kCtrl);
                if (v.glCount <= 0.0)
                {
                    v.glCount = std::max(0.03 * fSr, v.dBase);
                    int st = 0;
                    if (prob > 0.f && nTypes > 0 && rnd() < prob)
                        st = types[std::min(nTypes - 1, int(rnd() * float(nTypes)))];
                    if (st == 3) v.stutFrac = rnd() < 0.5f ? 0.25f : 0.125f;
                    v.glState = st;
                }
            }
            else { v.glState = 0; v.glCount = 0.0; }

            v.revWant  = e.rev != (v.glState == 1);
            v.mulWant  = v.glState == 2 ? 0.5f : 1.f;
            v.loopWant = fFrozen || v.glState == 3;
            v.fracWant = v.glState == 3 ? v.stutFrac : 1.f;
            v.jumpPending = jump && std::fabs(v.dTarget - v.dBase) > 0.5;

            const bool differ = v.revWant != v.revActive || v.mulWant != v.mulActive || v.loopWant != v.loopActive
                             || (v.loopActive && v.fracWant != v.fracActive) || v.jumpPending || v.swKick;
            if (differ && v.gateDir == 0) v.gateDir = -1;

            v.pitched = std::fabs(e.cents) > 0.5f;
            v.ratio = v.pitched ? std::pow(2.f, e.cents / 1200.f) : 1.f;
            v.fb = e.fb;
            v.crushAmt = vp[vCrush] * 0.01f;

            const float lvl = e.on ? e.level * norm : 0.f;
            const float ang = (e.pan + 1.f) * 0.25f * 3.14159265f;
            v.gLT = lvl * std::cos(ang) * 1.41421356f;
            v.gRT = lvl * std::sin(ang) * 1.41421356f;
            v.audible = e.on || v.gL > 1e-5f || v.gR > 1e-5f;
            if (!v.audible)
            {
                for (int c = 0; c < 2; ++c) v.last[c] = v.lc[c] = v.hc[c] = v.lp[c] = v.hold[c] = 0.f;
                v.gL = v.gR = 0.f;
                // an idle voice follows its settings directly, so it starts clean when switched on
                v.dBase = v.dTarget; v.revActive = v.revWant; v.mulActive = 1.f;
                v.loopActive = false; v.fracActive = 1.f; v.gate = 1.f; v.gateDir = 0;
                v.swMul = 1.0; v.swKick = false;
            }
        }

        // ducking
        fDuckAmt = p[gDuck] * 0.01f;
        fDuckSens = std::pow(10.f, p[gDuckSens] / 20.f);
        fDuckRelC = 1.f - std::exp(-1.f / (p[gDuckRel] * 0.001f * sr));

        const float mix = p[gMix] * 0.01f;
        fDryT = std::min(1.f, 2.f * (1.f - mix));
        fWetT = std::min(1.f, 2.f * mix);
        fOutT = std::pow(10.f, p[gOutput] / 20.f);
    }

public:
    Engine() { setSampleRate(48000.0); }

    void setSampleRate(double sr)
    {
        fSr = sr;
        fMaxD = double(kMaxTimeMs) * 0.001 * sr;
        fWin = 0.050 * sr;
        // reverse reads up to twice the delay time back, plus the pitch window and the chorus wobble
        uint32_t need = uint32_t(2.0 * fMaxD + fWin + 0.05 * sr) + 64, size = 1;
        while (size < need) size <<= 1;
        fMask = size - 1;
        for (Voice& v : fV) { v.buf[0].assign(size, 0.f); v.buf[1].assign(size, 0.f); }
        fSmoothGain = 1.f - std::exp(-1.f / (0.02f * float(sr)));
        fGateStep = 1.f / (0.003f * float(sr));
        fTapeC = 1.f - std::exp(-1.f / (0.15f * float(sr)));
        reset();
    }

    void reset()
    {
        for (int i = 0; i < kNumVoices; ++i)
        {
            Voice& v = fV[i];
            for (int c = 0; c < 2; ++c)
            {
                std::fill(v.buf[c].begin(), v.buf[c].end(), 0.f);
                v.last[c] = v.lc[c] = v.hc[c] = v.lp[c] = v.hold[c] = 0.f;
                v.crPh[c] = 1.f;
            }
            v.dBase = -1.0; v.mulActive = 1.f; v.fracActive = 1.f; v.swMul = 1.0; v.swKick = false;
            v.revActive = v.revWant; v.loopActive = false;
            v.gate = 1.f; v.gateDir = 0;
            v.shPos = 0.0; v.loopPos = 0.0; v.chPh = double(i) * 0.37;
            v.glCount = 0.0; v.glState = 0;
            v.gL = v.gR = 0.f;
        }
        fW = 0; fCtrlCount = 0; fSwCount = 0.0;
        fEnv = 0.f; fDuckNow = 0.f;
        fDryG = fDryT; fWetG = fWetT; fOutG = fOutT;
    }

    // p: kInputCount parameter values. bpm: host tempo (<= 0 if unknown).
    // outParams: kOutputCount values (delay now and glitch state per voice, ducking amount, tempo)
    void process(const float* inL, const float* inR, float* outL, float* outR, uint32_t frames,
                 const float* p, float bpm, float* outParams)
    {
#if defined(__SSE__) || defined(_M_X64)
        const unsigned int oldCsr = _mm_getcsr();
        _mm_setcsr(oldCsr | 0x8040); // flush-to-zero + denormals-are-zero
#endif
        fBpm = bpm > 1.f ? bpm : 120.f;
        const float sr = float(fSr);
        const float atkD = 1.f - std::exp(-1.f / (0.005f * sr));
        const float sg = fSmoothGain;
        const uint32_t mask = fMask;
        const double fade = 0.003 * fSr;

        for (uint32_t i = 0; i < frames; ++i)
        {
            const float xin[2] = { inL[i], inR[i] };

            if (fCtrlCount <= 0) { control(p); fCtrlCount = kCtrl; }
            --fCtrlCount;

            // ducking: the repeats step back while you play
            const float a = std::fabs(0.5f * (xin[0] + xin[1]));
            fEnv += (a > fEnv ? atkD : fDuckRelC) * (a - fEnv);
            fDuckNow = fDuckAmt * std::min(1.f, fEnv * fDuckSens);

            float wet[2] = { 0.f, 0.f };
            for (int vi = 0; vi < kNumVoices; ++vi)
            {
                Voice& v = fV[vi];
                float* const b0 = v.buf[0].data();
                float* const b1 = v.buf[1].data();
                if (!v.audible)
                {
                    if (!fFrozen) { b0[fW] = xin[0]; b1[fW] = xin[1]; }
                    continue;
                }

                // gate: close, switch, open
                if (v.gateDir != 0)
                {
                    v.gate += float(v.gateDir) * fGateStep;
                    if (v.gate <= 0.f) { v.gate = 0.f; applyPending(v); v.gateDir = 1; }
                    else if (v.gate >= 1.f) { v.gate = 1.f; v.gateDir = 0; }
                }

                if (v.tape) v.dBase += double(fTapeC) * (v.dTarget - v.dBase);
                if (fSwOneWay) v.swMul = std::min(fSwHi, std::max(fSwLo, v.swMul + fSwLin));
                else v.swMul += fSwC * (1.0 - v.swMul);
                const double dEff = std::min(fMaxD, std::max(8.0, v.dBase * double(v.mulActive) * v.swMul));
                v.dNow = dEff;

                double extra = 0.0;
                if (fChOn)
                {
                    v.chPh += fChInc;
                    if (v.chPh >= 1.0) v.chPh -= 1.0;
                    extra = double(fChA) * (1.0 + std::sin(6.283185307179586 * v.chPh));
                }

                // where to read
                int mode;               // 0 plain tap, 1 two sliding taps, 2 loop
                double d1 = dEff, d2 = 0.0;
                float w1 = 1.f, g = v.gate;
                uint32_t wpos = fW;
                if (v.loopActive)
                {
                    mode = 2;
                    const double len = std::max(32.0, dEff * double(v.fracActive));
                    v.loopPos += double(v.ratio);
                    while (v.loopPos >= len) v.loopPos -= len;
                    const double edge = std::min(v.loopPos, len - v.loopPos);
                    const double fd = std::min(len * 0.25, fade);
                    if (edge < fd) g *= float(edge / fd);
                    d1 = (v.revActive ? v.loopPos : len - v.loopPos) + 2.0;
                    wpos = v.anchor;
                }
                else if (v.revActive || v.pitched)
                {
                    mode = 1;
                    double win, base, inc;
                    if (v.revActive) { win = 2.0 * dEff; base = 2.0; inc = (1.0 + double(v.ratio)) / win; }
                    else { win = fWin; base = std::max(2.0, dEff - 0.5 * win); inc = (1.0 - double(v.ratio)) / win; }
                    v.shPos += inc;
                    if (v.shPos >= 1.0) v.shPos -= 1.0; else if (v.shPos < 0.0) v.shPos += 1.0;
                    double p2 = v.shPos + 0.5; if (p2 >= 1.0) p2 -= 1.0;
                    const float s = std::sin(3.14159265f * float(v.shPos));
                    w1 = s * s;
                    d1 = base + win * v.shPos;
                    d2 = base + win * p2;
                }
                else
                    mode = 0;

                // write: input plus feedback (crossed between the channels for ping-pong)
                if (!fFrozen)
                {
                    const float f0 = v.fb > 0.f ? softClip(v.fb * v.last[fPingPong ? 1 : 0]) : 0.f;
                    const float f1 = v.fb > 0.f ? softClip(v.fb * v.last[fPingPong ? 0 : 1]) : 0.f;
                    if (fPingPong) { b0[fW] = 0.5f * (xin[0] + xin[1]) + f0; b1[fW] = f1; }
                    else { b0[fW] = xin[0] + f0; b1[fW] = xin[1] + f1; }
                }

                v.gL += sg * (v.gLT - v.gL);
                v.gR += sg * (v.gRT - v.gR);
                const bool crush = fCrOn && v.crushAmt > 0.001f;

                for (int c = 0; c < 2; ++c)
                {
                    const float* const b = c == 0 ? b0 : b1;
                    float y;
                    if (mode == 1)
                    {
                        const float s0 = (!fChOn || fChMix < 0.999f)
                            ? w1 * readTap(b, mask, wpos, d1) + (1.f - w1) * readTap(b, mask, wpos, d2) : 0.f;
                        y = s0;
                        if (fChOn)
                        {
                            const float m = w1 * readTap(b, mask, wpos, d1 + extra) + (1.f - w1) * readTap(b, mask, wpos, d2 + extra);
                            y = s0 + fChMix * (m - s0);
                        }
                    }
                    else
                    {
                        const float s0 = (!fChOn || fChMix < 0.999f) ? readTap(b, mask, wpos, d1) : 0.f;
                        y = s0;
                        if (fChOn) y = s0 + fChMix * (readTap(b, mask, wpos, d1 + extra) - s0);
                    }
                    y *= g;

                    if (fLoOn) { v.lc[c] += fLoC * (y - v.lc[c]); y -= v.lc[c]; }
                    if (fHiOn) { v.hc[c] += fHiC * (y - v.hc[c]); y = v.hc[c]; }
                    if (fWarmOn)
                    {
                        v.lp[c] += fWarmC * (y - v.lp[c]);
                        y = softClip(v.lp[c] * fWarmDrive) / fWarmDrive;
                    }
                    const float clean = y;

                    if (crush)
                    {
                        v.crPh[c] += fCrInc;
                        if (v.crPh[c] >= 1.f)
                        {
                            v.crPh[c] -= 1.f;
                            v.hold[c] = std::round(y * fCrSteps) / fCrSteps;
                        }
                        y = v.crushAmt >= 0.999f ? v.hold[c] : y + v.crushAmt * (v.hold[c] - y);
                    }
                    v.last[c] = fCrLoop ? y : clean;
                    wet[c] += y * (c == 0 ? v.gL : v.gR);
                }
            }
            if (!fFrozen) fW = (fW + 1) & mask;

            const float duck = 1.f - fDuckNow;
            fDryG += sg * (fDryT - fDryG);
            fWetG += sg * (fWetT - fWetG);
            fOutG += sg * (fOutT - fOutG);
            outL[i] = (xin[0] * fDryG + wet[0] * fWetG * duck) * fOutG;
            outR[i] = (xin[1] * fDryG + wet[1] * fWetG * duck) * fOutG;
        }

        if (outParams != nullptr)
        {
            const float toMs = 1000.f / sr;
            for (int vi = 0; vi < kNumVoices; ++vi)
            {
                outParams[kOutNow - kInputCount + vi] = fV[vi].audible ? float(fV[vi].dNow) * toMs : 0.f;
                outParams[kOutState - kInputCount + vi] = fV[vi].audible ? float(fV[vi].glState) : 0.f;
            }
            outParams[kOutDuck - kInputCount] = fDuckNow;
            outParams[kOutBpm - kInputCount] = fBpm;
        }
#if defined(__SSE__) || defined(_M_X64)
        _mm_setcsr(oldCsr);
#endif
    }
};

} // namespace swd
