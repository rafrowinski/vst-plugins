// Swarm Synth - monophonic pitch detector tuned for low delay.
//
// Method: normalised square difference function (McLeod) on a signal decimated to about 12 kHz,
// but with a comparison window that grows with the lag: lag T is judged from the last `periods * T`
// samples only. A note is therefore recognised after roughly (1 + periods) of its own periods instead of
// after a fixed long window. The winning lag is then refined on the full-rate signal.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace swsyn {

class Tracker
{
    enum { kRing = 32768, kDRing = 4096, kMaxTau = 512, kLin = 2048 };

    struct Bq {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void set(bool highpass, float sr, float f, float q)
        {
            const float w = 2.f * 3.14159265f * f / sr, cs = std::cos(w), al = std::sin(w) / (2.f * q), a0 = 1.f + al;
            if (highpass) { b0 = (1.f + cs) * 0.5f / a0; b1 = -(1.f + cs) / a0; b2 = b0; }
            else          { b0 = (1.f - cs) * 0.5f / a0; b1 = (1.f - cs) / a0;  b2 = b0; }
            a1 = -2.f * cs / a0; a2 = (1.f - al) / a0;
        }
        inline float tick(float x)
        {
            const float y = b0 * x + z1;
            z1 = b1 * x - a1 * y + z2;
            z2 = b2 * x - a2 * y;
            return y;
        }
        void clear() { z1 = z2 = 0.f; }
    };

    double fSr = 48000.0;
    int fDec = 4, fDecCount = 0, fHop = 15, fHopCount = 0;
    float fSrd = 12000.f;
    int fTauMin = 8, fTauMax = 207, fWmin = 36;
    Bq fHp, fLp1, fLp2;
    float fFull[kRing];
    float fD[kDRing];
    uint32_t fFw = 0, fDw = 0;
    float fLinBuf[kLin];
    float fNsdf[kMaxTau + 3];

public:
    float freq = 0.f;     // Hz, 0 when nothing periodic was found
    float clarity = 0.f;  // 0..1, how periodic the signal is at that pitch

    Tracker() { setSampleRate(48000.0); }

    void setSampleRate(double sr)
    {
        fSr = sr;
        fDec = std::max(1, int(sr / 12000.0 + 0.5));
        fSrd = float(sr / fDec);
        fHp.set(true, float(sr), 55.f, 0.707f);
        fLp1.set(false, float(sr), 1800.f, 0.5412f);
        fLp2.set(false, float(sr), 1800.f, 1.3066f);
        fTauMin = std::max(2, int(fSrd / 1500.f));
        fTauMax = std::min(int(kMaxTau), int(fSrd / 58.f) + 1);
        fHop = std::max(1, int(fSrd * 0.00125f + 0.5f));
        fWmin = std::max(8, int(fSrd * 0.003f));
        reset();
    }

    void reset()
    {
        fHp.clear(); fLp1.clear(); fLp2.clear();
        std::memset(fFull, 0, sizeof(fFull));
        std::memset(fD, 0, sizeof(fD));
        fFw = fDw = 0; fDecCount = fHopCount = 0;
        freq = clarity = 0.f;
    }

    float analysisInterval() const { return float(fHop) / fSrd; } // seconds between two estimates

    // feeds one input sample; returns true when freq / clarity were just updated.
    // hintHz: the pitch currently being played (0 = none). A held note keeps its pitch unless another
    // one is clearly better, which stops the reading from hopping between octaves when a harmonic gets loud.
    inline bool push(float x, float periods, float hintHz = 0.f, float hintMin = 0.8f)
    {
        x = fLp2.tick(fLp1.tick(fHp.tick(x)));
        fFull[fFw] = x;
        fFw = (fFw + 1) & (kRing - 1);
        if (++fDecCount < fDec) return false;
        fDecCount = 0;
        fD[fDw] = x;
        fDw = (fDw + 1) & (kDRing - 1);
        if (++fHopCount < fHop) return false;
        fHopCount = 0;
        analyse(periods, hintHz, hintMin);
        return true;
    }

private:
    void analyse(float periods, float hintHz, float hintMin)
    {
        periods = std::min(2.f, std::max(0.5f, periods));
        const int maxLag = fTauMax;
        const int need = std::min(int(kLin), int((1.f + periods) * float(maxLag + 1)) + fWmin + 4);
        for (int i = 0; i < need; ++i)
            fLinBuf[i] = fD[(fDw - uint32_t(need) + uint32_t(i)) & (kDRing - 1)];
        const float* const e = fLinBuf + need; // e[-1] is the newest sample

        freq = 0.f; clarity = 0.f;
        float* const ns = fNsdf;
        ns[0] = 1.f;
        for (int tau = 1; tau <= maxLag + 1; ++tau)
        {
            int W = std::max(fWmin, int(periods * float(tau) + 0.5f));
            if (W + tau > need) W = need - tau;
            float acf = 0.f, m = 0.f;
            const float* a = e - W;
            const float* b = a - tau;
            for (int j = 0; j < W; ++j) { acf += a[j] * b[j]; m += a[j] * a[j] + b[j] * b[j]; }
            ns[tau] = m > 1e-9f ? 2.f * acf / m : 0.f;
        }

        // key maxima: the highest point of every positive lobe after the one around lag 0
        int tau = 1;
        while (tau <= maxLag && ns[tau] > 0.f) ++tau;
        int pt[64]; float pv[64]; int np = 0; float gmax = 0.f;
        while (tau <= maxLag)
        {
            while (tau <= maxLag && ns[tau] <= 0.f) ++tau;
            int bt = -1; float bv = 0.f;
            while (tau <= maxLag && ns[tau] > 0.f)
            {
                if (ns[tau] > bv && ns[tau] >= ns[tau - 1] && ns[tau] >= ns[tau + 1]) { bv = ns[tau]; bt = tau; }
                ++tau;
            }
            if (bt >= fTauMin && np < 64) { pt[np] = bt; pv[np] = bv; ++np; gmax = std::max(gmax, bv); }
        }
        if (np == 0) return;
        int pick = 0;
        while (pick < np - 1 && pv[pick] < 0.9f * gmax) ++pick;
        if (hintHz > 0.f)
        {
            const float tauH = fSrd / hintHz;
            int hi = -1;
            for (int k = 0; k < np; ++k)
                if (std::fabs(float(pt[k]) / tauH - 1.f) < 0.06f && (hi < 0 || pv[k] > pv[hi])) hi = k;
            if (hi >= 0 && pv[hi] >= hintMin * gmax)
            {
                // the played note still fits. Only a peak at half or a third of its period that fits just
                // as well (a real jump up by an octave or a twelfth) may replace it.
                bool better = false;
                for (int k = 0; k < hi; ++k)
                {
                    const float r = tauH / float(pt[k]), rr = std::floor(r + 0.5f);
                    if (rr >= 2.f && rr <= 4.f && std::fabs(r - rr) < 0.08f * rr && pv[k] >= pv[hi] - 0.015f) { better = true; pick = k; break; }
                }
                if (!better) pick = hi;
            }
        }
        const int t0 = pt[pick];
        clarity = pv[pick];

        // parabolic refinement on the decimated curve
        float tauD = float(t0);
        {
            const float y0 = ns[t0 - 1], y1 = ns[t0], y2 = ns[t0 + 1];
            const float den = y0 - 2.f * y1 + y2;
            if (den < -1e-9f) tauD += std::min(0.5f, std::max(-0.5f, 0.5f * (y0 - y2) / den));
        }
        float tauF = tauD * float(fDec);

        // refinement on the full-rate signal around that lag
        if (fDec > 1)
        {
            const int centre = int(tauF + 0.5f);
            const int span = fDec;
            const int stride = std::max(1, fDec / 4);
            const int W = std::max(fWmin, int(periods * float(t0) + 0.5f)) * fDec;
            float best = -2.f; int bi = 0;
            float vals[2 * 32 + 3];
            const int n = std::min(2 * span + 1, int(sizeof(vals) / sizeof(vals[0])));
            for (int k = 0; k < n; ++k)
            {
                const int lag = centre - n / 2 + k;
                float acf = 0.f, m = 0.f;
                if (lag > 0 && W + lag + 4 < int(kRing))
                    for (int j = 1; j <= W; j += stride)
                    {
                        const float a = fFull[(fFw - uint32_t(j)) & (kRing - 1)];
                        const float b = fFull[(fFw - uint32_t(j + lag)) & (kRing - 1)];
                        acf += a * b; m += a * a + b * b;
                    }
                vals[k] = m > 1e-9f ? 2.f * acf / m : -1.f;
                if (vals[k] > best) { best = vals[k]; bi = k; }
            }
            if (bi > 0 && bi < n - 1)
            {
                const float y0 = vals[bi - 1], y1 = vals[bi], y2 = vals[bi + 1];
                const float den = y0 - 2.f * y1 + y2;
                float off = 0.f;
                if (den < -1e-9f) off = std::min(0.5f, std::max(-0.5f, 0.5f * (y0 - y2) / den));
                tauF = float(centre - n / 2 + bi) + off;
            }
        }
        if (tauF > 1.f) freq = float(fSr) / tauF;
    }
};

} // namespace swsyn
