// Cleaver Slicer - DSP engine (framework independent).
// Signal path of the effect sound:  in -> pitch (per step) -> filter (per step) -> gate -> pan
// then mixed with the direct sound (Balance) and scaled by Output.
#pragma once

#include "SlicerShared.hpp"

#include <atomic>
#include <cstdint>
#include <vector>

namespace cleaver {

struct HostTime {
    bool valid = false;   // tempo information available
    bool playing = false; // transport rolling
    double bpm = 120.0;
    double ppq = 0.0;     // position in quarter notes at the start of the block
};

class Engine {
public:
    Engine()
    {
        for (int i = 0; i < kNumParams; ++i) par[i] = kParams[i].def;
        setSampleRate(48000.0);
    }

    void setSampleRate(double newSr)
    {
        sr = newSr > 1000.0 ? newSr : 48000.0;
        shLen = 1;
        while (shLen < (uint32_t)(sr * 0.1)) shLen <<= 1; // >= 100 ms
        for (int c = 0; c < 2; ++c) shBuf[c].assign(shLen, 0.f);
        shWin = (float)(sr * 0.035);
        reset();
    }

    void reset()
    {
        for (int c = 0; c < 2; ++c) {
            std::fill(shBuf[c].begin(), shBuf[c].end(), 0.f);
            ic1[c] = ic2[c] = 0.f;
        }
        shW = 0;
        shPhase = 0.f;
        pos = 0.0;
        gate = 0.f;
        panSm = 0.f;
        pitchMix = 0.f;
        cutSm = 1.f;
        envFast = envSlow = 0.f;
        holdoff = 0;
        lastStep = -1;
        rndPan = 0.f;
        outSm = 1.f;
        mixSm = par[pMix] * 0.01f;
    }

    void setParam(int i, float v)
    {
        if (i >= 0 && i < kNumInputParams) par[i] = v;
    }
    float getParam(int i) const
    {
        if (i == oStep) return (float)curStep.load(std::memory_order_relaxed);
        if (i == oGate) return curGate.load(std::memory_order_relaxed);
        return (i >= 0 && i < kNumInputParams) ? par[i] : 0.f;
    }

    // May be called from a non-audio thread.
    void setPattern(const Pattern& p)
    {
        const int next = 1 - active.load(std::memory_order_acquire);
        pat[next] = p;
        active.store(next, std::memory_order_release);
    }

    void process(const float* inL, const float* inR, float* outL, float* outR, uint32_t frames, const HostTime& ht)
    {
        const Pattern& P = pat[active.load(std::memory_order_acquire)];

        const bool sync = par[pSync] > 0.5f;
        const double bpm = clampd((sync && ht.valid && ht.bpm > 1.0) ? ht.bpm : (double)par[pBpm], 20.0, 999.0);
        const int rate = clampi((int)std::lround(par[pRate]), 0, kNumRates - 1);
        const int steps = clampi((int)std::lround(par[pSteps]), 1, kMaxSteps);
        const double stepQN = kRateQN[rate];
        const double stepSec = 60.0 / bpm * stepQN;
        const double inc = 1.0 / (stepSec * sr);
        const bool locked = sync && ht.valid && ht.playing;

        if (locked) {
            // lock to the host timeline so slices always land on the grid
            double p = ht.ppq / stepQN;
            const double len = (double)steps;
            p = std::fmod(p, len);
            if (p < 0.0) p += len;
            pos = p;
        }

        const float duty = clampf(par[pDuty] * 0.01f, 0.05f, 1.f);
        const float depth = clampf(par[pDepth] * 0.01f, 0.f, 1.f);
        const float swing = clampf(par[pSwing] * 0.01f, 0.f, 1.f);
        const double bnd = 1.0 + 0.5 * swing; // where the off-beat step starts inside a pair
        const float att = clampf(par[pAttack] * 0.01f, 0.f, 1.f);
        const float attSec = 0.0004f + att * att * 0.6f * (float)stepSec;
        const float relSec = clampf(par[pRelease], 1.f, 200.f) * 0.001f;
        const float aCoef = 1.f - std::exp(-1.f / (attSec * 0.35f * (float)sr));
        const float rCoef = 1.f - std::exp(-1.f / (relSec * 0.35f * (float)sr));
        const float sm3 = 1.f - std::exp(-1.f / (0.003f * (float)sr));
        const float sm6 = 1.f - std::exp(-1.f / (0.006f * (float)sr));
        const float sm20 = 1.f - std::exp(-1.f / (0.020f * (float)sr));

        const int stereo = clampi((int)std::lround(par[pStereo]), 0, kNumStereo - 1);
        const float width = clampf(par[pWidth] * 0.01f, 0.f, 1.f);
        const int fmode = clampi((int)std::lround(par[pFiltMode]), 0, kNumFiltModes - 1);
        const float reso = clampf(par[pReso] * 0.01f, 0.f, 1.f);
        const float k = 1.f / (0.6f + reso * reso * 11.f); // Q 0.6 .. 11.6
        const float harm = clampf(par[pHarm] * 0.01f, 0.f, 1.f);
        const float mixT = clampf(par[pMix] * 0.01f, 0.f, 1.f);
        const float outT = std::pow(10.f, clampf(par[pOutput], -24.f, 12.f) * 0.05f);

        const bool retrig = par[pRetrig] > 0.5f && !locked;
        const float thr = std::pow(10.f, clampf(par[pSens], -60.f, -6.f) * 0.05f);
        const float efA = 1.f - std::exp(-1.f / (0.001f * (float)sr));
        const float efR = 1.f - std::exp(-1.f / (0.030f * (float)sr));
        const float esC = 1.f - std::exp(-1.f / (0.150f * (float)sr));

        const uint32_t mask = shLen - 1;
        const float nyq = (float)sr * 0.45f;
        float gNow = gate;

        for (uint32_t n = 0; n < frames; ++n) {
            const float xl = inL[n];
            const float xr = inR[n];

            // ---- retrigger on picked note
            if (retrig) {
                const float a = std::fmax(std::fabs(xl), std::fabs(xr));
                envFast += (a > envFast ? efA : efR) * (a - envFast);
                envSlow += esC * (a - envSlow);
                if (holdoff > 0) --holdoff;
                else if (envFast > thr && envFast > envSlow * 2.2f) {
                    pos = 0.0;
                    holdoff = (uint32_t)(0.09 * sr);
                }
            }

            // ---- sequencer position -> step index + position inside the step
            int idx;
            float frac;
            {
                const double pair = std::floor(pos * 0.5);
                const double pp = pos - pair * 2.0;
                if (pp < bnd) {
                    idx = (int)pair * 2;
                    frac = (float)(pp / bnd);
                } else {
                    idx = (int)pair * 2 + 1;
                    frac = (float)((pp - bnd) / (2.0 - bnd));
                }
                if (idx >= steps) idx = steps - 1;
            }
            const Step& S = P.s[idx];
            if (idx != lastStep) {
                lastStep = idx;
                rng = rng * 1664525u + 1013904223u;
                rndPan = (float)((rng >> 8) & 0xFFFF) / 32767.5f - 1.f;
                if (S.level > 0.001f) ++activeCount;
            }

            // ---- gate envelope
            const float target = 1.f - depth * (1.f - (frac < duty ? S.level : 0.f));
            gNow += (target > gNow ? aCoef : rCoef) * (target - gNow);

            // ---- pitch (two crossfading delay taps)
            shBuf[0][shW] = xl;
            shBuf[1][shW] = xr;
            float wl = xl, wr = xr;
            const float pmT = (S.pitch != 0) ? harm : 0.f;
            pitchMix += sm3 * (pmT - pitchMix);
            if (pitchMix > 0.0005f) {
                if (S.pitch != 0) ratio = std::exp2((float)S.pitch / 12.f);
                shPhase += (1.f - ratio) / shWin;
                shPhase -= std::floor(shPhase);
                const float ph2 = shPhase + 0.5f - std::floor(shPhase + 0.5f);
                const float w1 = 0.5f - 0.5f * std::cos(6.2831853f * shPhase);
                const float w2 = 1.f - w1;
                const float d1 = 1.f + shPhase * shWin;
                const float d2 = 1.f + ph2 * shWin;
                const float sl = tap(0, d1, mask) * w1 + tap(0, d2, mask) * w2;
                const float sr_ = tap(1, d1, mask) * w1 + tap(1, d2, mask) * w2;
                wl += pitchMix * (sl - xl);
                wr += pitchMix * (sr_ - xr);
            }
            shW = (shW + 1) & mask;

            // ---- filter (TPT state variable)
            if (fmode != fmOff) {
                cutSm += sm6 * (S.filt - cutSm);
                float fc;
                if (fmode == fmLP) fc = 120.f * std::pow(166.7f, cutSm);         // 120 Hz .. 20 kHz
                else if (fmode == fmBP) fc = 250.f * std::pow(16.f, cutSm);      // 250 Hz .. 4 kHz
                else fc = 20.f * std::pow(300.f, 1.f - cutSm);                   // 6 kHz .. 20 Hz
                if (fc > nyq) fc = nyq;
                const float g = std::tan(3.14159265f * fc / (float)sr);
                const float a1 = 1.f / (1.f + g * (g + k));
                const float a2 = g * a1;
                const float a3 = g * a2;
                float* io[2] = {&wl, &wr};
                for (int c = 0; c < 2; ++c) {
                    const float v0 = *io[c];
                    const float v3 = v0 - ic2[c];
                    const float v1 = a1 * ic1[c] + a2 * v3;
                    const float v2 = ic2[c] + a2 * ic1[c] + a3 * v3;
                    ic1[c] = 2.f * v1 - ic1[c] + 1e-20f;
                    ic2[c] = 2.f * v2 - ic2[c] + 1e-20f;
                    *io[c] = fmode == fmLP ? v2 : fmode == fmBP ? v1 * std::sqrt(k) * 1.5f : v0 - k * v1 - v2;
                }
            }

            // ---- pan
            float panT = 0.f;
            switch (stereo) {
            case stPattern: panT = S.pan; break;
            case stPingPong: panT = (activeCount & 1) ? 1.f : -1.f; break;
            case stRandom: panT = rndPan; break;
            case stAuto: panT = (float)std::sin(6.283185307179586 * pos / (double)steps); break;
            default: break;
            }
            panSm += sm6 * (panT * width - panSm);
            const float ang = (panSm + 1.f) * 0.78539816f;
            const float gl = std::cos(ang) * 1.41421356f;
            const float gr = std::sin(ang) * 1.41421356f;

            // ---- mix
            mixSm += sm20 * (mixT - mixSm);
            outSm += sm20 * (outT - outSm);
            outL[n] = (xl * (1.f - mixSm) + wl * gNow * gl * mixSm) * outSm;
            outR[n] = (xr * (1.f - mixSm) + wr * gNow * gr * mixSm) * outSm;

            pos += inc;
            if (pos >= (double)steps) pos -= (double)steps * std::floor(pos / (double)steps);
        }

        gate = gNow;
        curStep.store(lastStep < 0 ? 0 : lastStep, std::memory_order_relaxed);
        curGate.store(gNow, std::memory_order_relaxed);
    }

private:
    static float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
    static double clampd(double v, double a, double b) { return v < a ? a : v > b ? b : v; }
    static int clampi(int v, int a, int b) { return v < a ? a : v > b ? b : v; }

    float tap(int c, float delay, uint32_t mask) const
    {
        const float rp = (float)shW - delay;
        const float fl = std::floor(rp);
        const float fr = rp - fl;
        const uint32_t i0 = (uint32_t)((int32_t)fl) & mask;
        const uint32_t i1 = (i0 + 1) & mask;
        return shBuf[c][i0] + fr * (shBuf[c][i1] - shBuf[c][i0]);
    }

    double sr = 48000.0;
    float par[kNumParams];

    Pattern pat[2];
    std::atomic<int> active{0};
    std::atomic<int> curStep{0};
    std::atomic<float> curGate{0.f};

    double pos = 0.0;
    int lastStep = -1;
    uint32_t activeCount = 0;
    float gate = 0.f, panSm = 0.f, mixSm = 1.f, outSm = 1.f;
    uint32_t rng = 0x1234567u;
    float rndPan = 0.f;

    // pitch shifter
    std::vector<float> shBuf[2];
    uint32_t shLen = 8192, shW = 0;
    float shWin = 2160.f, shPhase = 0.f, ratio = 1.f, pitchMix = 0.f;

    // filter
    float ic1[2] = {0, 0}, ic2[2] = {0, 0};
    float cutSm = 1.f;

    // retrigger detector
    float envFast = 0.f, envSlow = 0.f;
    uint32_t holdoff = 0;
};

} // namespace cleaver
