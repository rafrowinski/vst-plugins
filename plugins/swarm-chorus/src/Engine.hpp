// Swarm Chorus - DSP engine (no plugin-framework dependency, so it can be tested offline)
#pragma once

#include "Params.hpp"

#include <atomic>
#include <vector>
#if defined(__SSE__) || defined(_M_X64)
# include <xmmintrin.h>
#endif

namespace roj {

class Engine
{
    enum { kCtrl = 16 };

    struct Biquad {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
        float z1[2] = {0, 0}, z2[2] = {0, 0};
        bool active = false;

        void setPeak(float sr, float freq, float gainDb, float q)
        {
            active = std::fabs(gainDb) > 0.05f;
            if (!active) return;
            freq = std::min(freq, sr * 0.45f);
            const float A = std::pow(10.f, gainDb / 40.f);
            const float w = 2.f * 3.14159265f * freq / sr;
            const float alpha = std::sin(w) / (2.f * q);
            const float cs = std::cos(w);
            const float a0 = 1.f + alpha / A;
            b0 = (1.f + alpha * A) / a0;
            b1 = (-2.f * cs) / a0;
            b2 = (1.f - alpha * A) / a0;
            a1 = (-2.f * cs) / a0;
            a2 = (1.f - alpha / A) / a0;
        }
        inline float tick(int ch, float x)
        {
            const float y = b0 * x + z1[ch];
            z1[ch] = b1 * x - a1 * y + z2[ch];
            z2[ch] = b2 * x - a2 * y;
            return y;
        }
        void clear() { z1[0] = z1[1] = z2[0] = z2[1] = 0; }
    };

    struct Voice {
        std::vector<float> buf[2];  // one delay line per channel, same modulation
        double phaseAcc = 0.0;      // cycles since (re)start, without the phase offset
        float delaySm = -1.f;       // smoothed delay, samples
        float delayTarget = 480.f;
        float detPos = 0.f;         // pitch-shifter ramp position 0..1
        float detInc = 0.f;
        bool  detOn = false;
        float last[2] = {0, 0};     // last delay output (for feedback)
        float fb = 0.f;
        float lp[2] = {0, 0};       // warmth low-pass state
        float ic1[2] = {0, 0}, ic2[2] = {0, 0}; // SVF state
        float a1 = 0, a2 = 0, a3 = 0, kq = 1;
        float filt = 0.f, filtT = 0.f;
        float gL = 0.f, gR = 0.f, gLT = 0.f, gRT = 0.f;
        bool  audible = false;
    };

    double fSr = 48000.0;
    uint32_t fMask = 0, fW = 0;
    Voice fV[kNumVoices];
    Biquad fEq[3];
    float fEqLast[9];
    bool fEqOnOutput = false;

    double fT = 0.0;                 // seconds since LFO (re)start
    int fCtrlCount = 0;
    float fEnvFast = 0.f, fEnvSlow = 0.f;
    float fEnvFollow = 0.f, fEnvNorm = 0.f;
    bool fGate = false;
    float fSinceTrig = 10.f;
    float fDryG = 1.f, fWetG = 1.f, fOutG = 1.f, fDryT = 1.f, fWetT = 1.f, fOutT = 1.f;
    float fSmoothGain = 0.001f, fSmoothDelay = 0.003f;
    float fWinSamples = 1440.f;
    float fWarmC = 1.f, fWarmDrive = 1.f;
    bool fWarmOn = false;
    float fBpm = 120.f;

    // custom wave: two tables, the audio thread reads the active one
    float fWave[2][kWaveSize];
    std::atomic<int> fWaveActive { 0 };

    static inline float softClip(float x)
    {
        x = std::min(3.f, std::max(-3.f, x));
        return x * (27.f + x * x) / (27.f + 9.f * x * x);
    }

    static inline float readTap(const float* b, uint32_t mask, uint32_t w, float d)
    {
        const float pos = float(w) - d;
        const float fl = std::floor(pos);
        const int ip = int(fl);
        const float f = pos - fl;
        const float x0 = b[uint32_t(ip - 1) & mask];
        const float x1 = b[uint32_t(ip) & mask];
        const float x2 = b[uint32_t(ip + 1) & mask];
        const float x3 = b[uint32_t(ip + 2) & mask];
        const float c1 = 0.5f * (x2 - x0);
        const float c2 = x0 - 2.5f * x1 + 2.f * x2 - 0.5f * x3;
        const float c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
        return ((c3 * f + c2) * f + c1) * f + x1;
    }

    void restartLfos()
    {
        fT = 0.0;
        for (Voice& v : fV) v.phaseAcc = 0.0;
        fCtrlCount = 0;
    }

    void control(const float* p)
    {
        const float sr = float(fSr);
        Eff eff[kNumVoices];
        const int n = computeEff(p, eff, fBpm);
        const float norm = std::pow(float(std::max(1, n)), -0.75f);
        const float fade = p[gFade] * 0.001f;
        const float t = float(fT);
        const float dt = float(kCtrl) / sr;
        const float* const wave = fWave[fWaveActive.load(std::memory_order_acquire)];

        // envelope follower -> 0..1
        fEnvNorm = std::min(1.f, fEnvFollow * std::pow(10.f, p[gEnvSens] / 20.f));
        const float ed = p[gEnvDepth] * 0.01f;
        const float depthFactor = ed >= 0.f ? (1.f - ed) + ed * fEnvNorm : 1.f + ed * fEnvNorm;
        const float envWah = p[gEnvWah] * fEnvNorm;

        // warmth: darker and slightly saturated voices, like a bucket-brigade delay
        const float warm = p[gWarmth] * 0.01f;
        fWarmOn = warm > 0.005f;
        if (fWarmOn)
        {
            const float fc = std::min(sr * 0.45f, 18000.f * std::pow(2200.f / 18000.f, warm));
            fWarmC = 1.f - std::exp(-2.f * 3.14159265f * fc / sr);
            fWarmDrive = 1.f + 2.5f * warm;
        }

        for (int i = 0; i < kNumVoices; ++i)
        {
            Voice& v = fV[i];
            const float* vp = p + voiceIndex(i, 0);
            const Eff& e = eff[i];

            const float tt = t - e.start;
            const int shape = int(vp[vShape] + 0.5f);
            const float mod = fadeEnv(tt, fade) * depthFactor
                            * lfoShape(shape, double(e.phase) + v.phaseAcc, i, wave);
            if (tt >= 0.f) v.phaseAcc += double(e.rate) * double(dt);

            const float dms = delayFromMod(p, i, mod);
            v.delayTarget = std::max(2.f, dms * 0.001f * sr);

            // detune: constant pitch offset from a pair of crossfading, sliding taps
            v.detOn = std::fabs(e.detune) > 0.02f;
            v.detInc = v.detOn ? (1.f - std::pow(2.f, e.detune / 1200.f)) / fWinSamples : 0.f;

            v.fb = vp[vFeedback] * 0.01f;

            // hue / wah filter
            v.filtT = vp[vFilter] * 0.01f;
            const float fc = std::min(std::min(12000.f, sr * 0.45f),
                                      std::max(60.f, vp[vHue] * std::pow(2.f, vp[vWah] * mod + envWah)));
            const float g = std::tan(3.14159265f * fc / sr);
            v.kq = 1.f / std::max(0.5f, vp[vReso]);
            v.a1 = 1.f / (1.f + g * (g + v.kq));
            v.a2 = g * v.a1;
            v.a3 = g * v.a2;

            const float lvl = e.on ? vp[vLevel] * 0.01f * norm : 0.f;
            const float ang = (e.pan + 1.f) * 0.25f * 3.14159265f;
            v.gLT = lvl * std::cos(ang) * 1.41421356f;
            v.gRT = lvl * std::sin(ang) * 1.41421356f;
            v.audible = e.on || v.gL > 1e-5f || v.gR > 1e-5f;
            if (!v.audible)
            {
                v.last[0] = v.last[1] = 0.f; v.lp[0] = v.lp[1] = 0.f;
                v.ic1[0] = v.ic1[1] = v.ic2[0] = v.ic2[1] = 0.f;
                v.gL = v.gR = 0.f;
            }
        }

        // EQ
        bool eqChanged = false;
        for (int i = 0; i < 9; ++i)
            if (p[gEq1F + i] != fEqLast[i]) { fEqLast[i] = p[gEq1F + i]; eqChanged = true; }
        if (eqChanged)
            for (int b = 0; b < 3; ++b)
            {
                const bool was = fEq[b].active;
                fEq[b].setPeak(sr, p[gEq1F + b * 3], p[gEq1G + b * 3], p[gEq1Q + b * 3]);
                if (!was) fEq[b].clear();
            }
        fEqOnOutput = p[gEqTarget] > 0.5f;

        const float mix = p[gMix] * 0.01f;
        fDryT = std::cos(mix * 1.5707963f);
        fWetT = std::sin(mix * 1.5707963f);
        fOutT = std::pow(10.f, p[gOutput] / 20.f);
    }

public:
    Engine()
    {
        for (float& f : fEqLast) f = -1.f;
        compileWave(kDefaultWave, fWave[0]);
        std::memcpy(fWave[1], fWave[0], sizeof(fWave[0]));
        setSampleRate(48000.0);
    }

    // called from a non-audio thread with a compiled table of kWaveSize values
    void setWave(const float* table)
    {
        const int next = 1 - fWaveActive.load(std::memory_order_acquire);
        std::memcpy(fWave[next], table, sizeof(fWave[0]));
        fWaveActive.store(next, std::memory_order_release);
    }

    void setSampleRate(double sr)
    {
        fSr = sr;
        uint32_t need = uint32_t(sr * 0.2) + 64, size = 1;
        while (size < need) size <<= 1;
        fMask = size - 1;
        for (Voice& v : fV) { v.buf[0].assign(size, 0.f); v.buf[1].assign(size, 0.f); }
        fSmoothGain = 1.f - std::exp(-1.f / (0.02f * float(sr)));
        fSmoothDelay = 1.f - std::exp(-1.f / (0.008f * float(sr)));
        fWinSamples = 0.030f * float(sr);
        for (float& f : fEqLast) f = -1.f;
        reset();
    }

    void reset()
    {
        for (Voice& v : fV)
        {
            for (int c = 0; c < 2; ++c)
            {
                std::fill(v.buf[c].begin(), v.buf[c].end(), 0.f);
                v.last[c] = v.lp[c] = v.ic1[c] = v.ic2[c] = 0.f;
            }
            v.phaseAcc = 0.0; v.detPos = 0.f;
            v.gL = v.gR = 0.f; v.filt = 0.f;
            v.delaySm = -1.f;
        }
        for (Biquad& b : fEq) b.clear();
        fW = 0; fT = 0.0; fCtrlCount = 0;
        fEnvFast = fEnvSlow = fEnvFollow = fEnvNorm = 0.f; fGate = false; fSinceTrig = 10.f;
        fDryG = fDryT; fWetG = fWetT; fOutG = fOutT;
    }

    // p: kInputCount parameter values. bpm: host tempo (<= 0 if unknown).
    // outParams: kOutputCount values (current delay per voice in ms, trigger flag, envelope level, tempo)
    void process(const float* inL, const float* inR, float* outL, float* outR, uint32_t frames,
                 const float* p, float bpm, float* outParams)
    {
#if defined(__SSE__) || defined(_M_X64)
        const unsigned int oldCsr = _mm_getcsr();
        _mm_setcsr(oldCsr | 0x8040); // flush-to-zero + denormals-are-zero
#endif
        fBpm = bpm > 1.f ? bpm : 120.f;
        const float sr = float(fSr);
        const float invSr = 1.f / sr;
        const bool trigOn = p[gTrigOn] > 0.5f;
        const float thr = std::pow(10.f, p[gTrigThresh] / 20.f);
        const float atkF = 1.f - std::exp(-1.f / (0.001f * sr));
        const float relF = 1.f - std::exp(-1.f / (0.040f * sr));
        const float atkS = 1.f - std::exp(-1.f / (0.060f * sr));
        const float relS = 1.f - std::exp(-1.f / (0.150f * sr));
        const float atkE = 1.f - std::exp(-1.f / (0.004f * sr));
        const float relE = 1.f - std::exp(-1.f / (0.180f * sr));
        const float sg = fSmoothGain, sd = fSmoothDelay;
        const uint32_t mask = fMask;

        for (uint32_t i = 0; i < frames; ++i)
        {
            const float xin[2] = { inL[i], inR[i] };
            const float x = 0.5f * (xin[0] + xin[1]);

            // onset detector + envelope follower
            const float a = std::fabs(x);
            fEnvFast += (a > fEnvFast ? atkF : relF) * (a - fEnvFast);
            fEnvSlow += (fEnvFast > fEnvSlow ? atkS : relS) * (fEnvFast - fEnvSlow);
            fEnvFollow += (a > fEnvFollow ? atkE : relE) * (a - fEnvFollow);
            fSinceTrig += invSr;
            bool onset = false;
            if (!fGate) { if (fEnvFast > thr) { fGate = true; onset = true; } }
            else
            {
                if (fEnvFast < thr * 0.4f) fGate = false;
                else if (fSinceTrig > 0.12f && fEnvFast > thr * 2.f && fEnvFast > fEnvSlow * 2.2f) onset = true;
            }
            if (onset && trigOn) { fSinceTrig = 0.f; restartLfos(); }

            if (fCtrlCount <= 0) { control(p); fCtrlCount = kCtrl; }
            --fCtrlCount;

            float wet[2] = { 0.f, 0.f };
            for (int vi = 0; vi < kNumVoices; ++vi)
            {
                Voice& v = fV[vi];
                if (!v.audible) { v.buf[0][fW] = xin[0]; v.buf[1][fW] = xin[1]; continue; }

                if (v.delaySm < 0.f) v.delaySm = v.delayTarget;
                v.delaySm += sd * (v.delayTarget - v.delaySm);

                float w1 = 1.f, d1 = v.delaySm, d2 = 0.f;
                if (v.detOn)
                {
                    v.detPos += v.detInc;
                    if (v.detPos >= 1.f) v.detPos -= 1.f; else if (v.detPos < 0.f) v.detPos += 1.f;
                    float p2 = v.detPos + 0.5f; if (p2 >= 1.f) p2 -= 1.f;
                    const float s = std::sin(3.14159265f * v.detPos);
                    w1 = s * s;
                    d1 = v.delaySm + fWinSamples * v.detPos;
                    d2 = v.delaySm + fWinSamples * p2;
                }
                v.filt += sg * (v.filtT - v.filt);
                v.gL += sg * (v.gLT - v.gL);
                v.gR += sg * (v.gRT - v.gR);

                for (int c = 0; c < 2; ++c)
                {
                    float* const b = v.buf[c].data();
                    b[fW] = xin[c] + (v.fb != 0.f ? softClip(v.fb * v.last[c]) : 0.f);

                    float y = v.detOn ? w1 * readTap(b, mask, fW, d1) + (1.f - w1) * readTap(b, mask, fW, d2)
                                      : readTap(b, mask, fW, d1);
                    if (fWarmOn)
                    {
                        v.lp[c] += fWarmC * (y - v.lp[c]);
                        y = softClip(v.lp[c] * fWarmDrive) / fWarmDrive;
                    }
                    v.last[c] = y;

                    // hue / wah (state-variable band-pass, blended with the unfiltered voice)
                    if (v.filt > 0.0005f)
                    {
                        const float v3 = y - v.ic2[c];
                        const float v1 = v.a1 * v.ic1[c] + v.a2 * v3;
                        const float v2 = v.ic2[c] + v.a2 * v.ic1[c] + v.a3 * v3;
                        v.ic1[c] = 2.f * v1 - v.ic1[c];
                        v.ic2[c] = 2.f * v2 - v.ic2[c];
                        const float wah = v1 * v.kq * 2.f + v2 * 0.2f;
                        y += v.filt * (wah - y);
                    }
                    wet[c] += y * (c == 0 ? v.gL : v.gR);
                }
            }
            fW = (fW + 1) & mask;
            fT += double(invSr);

            if (!fEqOnOutput)
                for (Biquad& bq : fEq)
                    if (bq.active) { wet[0] = bq.tick(0, wet[0]); wet[1] = bq.tick(1, wet[1]); }

            fDryG += sg * (fDryT - fDryG);
            fWetG += sg * (fWetT - fWetG);
            fOutG += sg * (fOutT - fOutG);
            float ol = xin[0] * fDryG + wet[0] * fWetG;
            float orr = xin[1] * fDryG + wet[1] * fWetG;

            if (fEqOnOutput)
                for (Biquad& bq : fEq)
                    if (bq.active) { ol = bq.tick(0, ol); orr = bq.tick(1, orr); }

            outL[i] = ol * fOutG;
            outR[i] = orr * fOutG;
        }

        if (outParams != nullptr)
        {
            for (int vi = 0; vi < kNumVoices; ++vi)
                outParams[vi] = fV[vi].audible && fV[vi].delaySm > 0.f ? fV[vi].delaySm * 1000.f * invSr : 0.f;
            outParams[kOutTrig - kInputCount] = fSinceTrig < 0.15f ? 1.f : 0.f;
            outParams[kOutEnv - kInputCount] = fEnvNorm;
            outParams[kOutBpm - kInputCount] = fBpm;
        }
#if defined(__SSE__) || defined(_M_X64)
        _mm_setcsr(oldCsr);
#endif
    }
};

} // namespace roj
