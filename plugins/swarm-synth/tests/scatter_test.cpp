// Scatter / Converge: after a pick the voices start spread out and come together into the note.
// Measures how wide the note is (cents, -10 dB) over time; writes scatter_demo.wav (stereo, 44.1 kHz).
//   g++ -O2 -std=c++17 tests/scatter_test.cpp -o scatter_test && ./scatter_test
#include <cmath>
#include <cstdio>
#include <cstring>
#include <random>
#include <vector>
static std::mt19937 rng(7);
#include "../../../shared/guitar-engine/Engine.hpp"
using namespace swsyn;

static void pluck(std::vector<float>& out, int at, int len, double f, double sr, double amp)
{
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    for (int i = 0; i < len && at + i < int(out.size()); ++i)
    {
        const double t = i / sr;
        double s = 0.0;
        for (int h = 1; h <= 12; ++h)
            s += (h == 1 ? 1.0 : 1.0 / h) * std::exp(-t * (1.5 + 0.9 * h)) * std::sin(2 * M_PI * f * h * t + 0.7 * h);
        s += 0.6 * u(rng) * std::exp(-t / 0.002);
        out[at + i] += float(amp * 0.5 * std::min(1.0, t / 0.0005) * s);
    }
}

// -10 dB width (cents) of the strongest component near fc, from a Hann-windowed DFT of x[at..at+n)
static float width(const std::vector<float>& x, int at, int n, double fc, double sr)
{
    std::vector<double> m;
    const double f0 = fc * std::pow(2.0, -1.2), f1 = fc * std::pow(2.0, 1.2), step = 0.5;
    for (double f = f0; f < f1; f += step)
    {
        double re = 0, im = 0;
        for (int i = 0; i < n; i += 2)
        {
            const double w = 0.5 - 0.5 * std::cos(2 * M_PI * i / n), ph = 2 * M_PI * f * i / sr;
            re += w * x[at + i] * std::cos(ph); im += w * x[at + i] * std::sin(ph);
        }
        m.push_back(10 * std::log10(re * re + im * im + 1e-20));
    }
    size_t pk = 0; for (size_t i = 1; i < m.size(); ++i) if (m[i] > m[pk]) pk = i;
    size_t lo = pk, hi = pk;
    // the outermost bins still within 10 dB of the peak, anywhere in the window
    for (size_t i = 0; i < m.size(); ++i) if (m[i] > m[pk] - 10) { lo = std::min(lo, i); hi = std::max(hi, i); }
    return float(1200.0 * std::log2((f0 + (hi + 0.5) * step) / (f0 + (lo - 0.5) * step)));
}

int main()
{
    const double sr = 44100, f = 110;
    const int N = int(sr * 12);
    int fails = 0;
    for (int run = 0; run < 2; ++run)
    {
        std::vector<float> in(N, 0.f), L(N), R(N), mono(N);
        pluck(in, 2000, N, f, sr, 0.6);
        float p[kInputCount];
        int pr = 0; while (std::strcmp(kPresets[pr].name, "Drone: Oscillator Wall (scatter to unison)") != 0) ++pr;
        kPresets[pr].fill(p);
        if (run == 1) p[gScatter] = 0.f;
        // for the measurement only: sines, all at the same octave, so the spectrum shows just how the voices are spread
        std::vector<float> keep(p, p + kInputCount);
        for (int v = 0; v < kNumVoices; ++v) { p[voiceIndex(v, vWave)] = float(wSine); p[voiceIndex(v, vOct)] = 0.f; }
        Engine e; e.setSampleRate(sr); float outp[kOutputCount];
        for (int o = 0; o < N; o += 64) e.process(&in[o], &in[o], &L[o], &R[o], uint32_t(std::min(64, N - o)), p, outp);
        for (int i = 0; i < N; ++i) mono[i] = 0.5f * (L[i] + R[i]);
        std::printf(run == 0 ? "Scatter 700 ct, Converge 6 s:\n" : "Scatter off (same preset):\n");
        float first = 0, last = 0;
        (void)keep;
        for (double t : { 0.3, 1.0, 2.0, 3.0, 4.0, 5.0, 6.0, 8.0, 10.0 })
        {
            const float w = width(mono, int(t * sr), 8192, f, sr);
            if (t == 0.3) first = w;
            last = w;
            std::printf("  %4.1f s: note %5.0f ct wide\n", t, w);
        }
        if (run == 0)
        {
            const bool ok = first > 1000 && last < 260;   // ~215 ct = what the measurement shows for Spread + Drift alone
            std::printf("%s scattered at the start, together at the end\n", ok ? "ok  " : "FAIL");
            fails += !ok;
            FILE* fo = std::fopen("scatter_demo.wav", "wb");
            const uint32_t data = uint32_t(N) * 4, rate = 44100, br = rate * 4, sz = 36 + data; const uint16_t ch = 2, ba = 4, bits = 16, fmt = 1; const uint32_t fl = 16;
            std::fwrite("RIFF", 1, 4, fo); std::fwrite(&sz, 4, 1, fo); std::fwrite("WAVEfmt ", 1, 8, fo); std::fwrite(&fl, 4, 1, fo);
            std::fwrite(&fmt, 2, 1, fo); std::fwrite(&ch, 2, 1, fo); std::fwrite(&rate, 4, 1, fo); std::fwrite(&br, 4, 1, fo);
            std::fwrite(&ba, 2, 1, fo); std::fwrite(&bits, 2, 1, fo); std::fwrite("data", 1, 4, fo); std::fwrite(&data, 4, 1, fo);
            for (int i = 0; i < N; ++i) { int16_t a = int16_t(std::max(-1.f, std::min(1.f, L[i])) * 32000), b = int16_t(std::max(-1.f, std::min(1.f, R[i])) * 32000); std::fwrite(&a, 2, 1, fo); std::fwrite(&b, 2, 1, fo); }
            std::fclose(fo);
        }
        else
        {
            const bool ok = first < 260;
            std::printf("%s without Scatter the start is not spread\n", ok ? "ok  " : "FAIL");
            fails += !ok;
        }
    }
    std::printf(fails ? "FAILED\n" : "ALL OK\n");
    return fails;
}
