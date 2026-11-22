#include "Engine.hpp"
#include <chrono>
using namespace swd;
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { ++fails; std::printf("FAIL: "); std::printf(__VA_ARGS__); std::printf("\n"); } else { std::printf("ok:   "); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)
static const int SR = 48000;
struct Run {
    Engine e; float p[kInputCount]; float out[kOutputCount];
    std::vector<float> L, R;
    Run() { presetDefaults(p); p[gMix] = 100.f; e.setSampleRate(SR); }
    // feed: generator of input per sample index
    template <class F> void go(int n, F f, int block = 64) {
        std::vector<float> il(block), ir(block), ol(block), orr(block);
        int base = int(L.size());
        for (int i = 0; i < n; i += block) {
            int m = std::min(block, n - i);
            for (int k = 0; k < m; ++k) il[k] = ir[k] = f(base + i + k);
            e.process(il.data(), ir.data(), ol.data(), orr.data(), m, p, 120.f, out);
            L.insert(L.end(), ol.begin(), ol.begin() + m); R.insert(R.end(), orr.begin(), orr.begin() + m);
        }
    }
};
static float peakIn(const std::vector<float>& x, int a, int b) { float m = 0; for (int i = std::max(0, a); i < b && i < int(x.size()); ++i) m = std::max(m, std::fabs(x[i])); return m; }
static int argPeak(const std::vector<float>& x, int a, int b) { int j = a; float m = 0; for (int i = a; i < b && i < int(x.size()); ++i) if (std::fabs(x[i]) > m) { m = std::fabs(x[i]); j = i; } return j; }
static double rms(const std::vector<float>& x, int a, int b) { double s = 0; for (int i = a; i < b; ++i) s += double(x[i]) * x[i]; return std::sqrt(s / std::max(1, b - a)); }
static bool finite(const std::vector<float>& x) { for (float v : x) if (!std::isfinite(v)) return false; return true; }
static auto impulse = [](int i) { return i == 100 ? 1.f : 0.f; };
static auto tone = [](int i) { return 0.5f * std::sin(6.2831853f * 440.f * float(i) / SR); };
// dominant frequency by zero crossings
static double zcFreq(const std::vector<float>& x, int a, int b) { int z = 0; for (int i = a + 1; i < b; ++i) if ((x[i - 1] < 0) != (x[i] < 0)) ++z; return 0.5 * z * SR / double(b - a); }

int main()
{
    { // plain echo: time and feedback decay
        Run r; r.go(SR * 2, impulse);
        int d = int(0.350 * SR);
        int p1 = argPeak(r.L, 100 + d - 50, 100 + d + 50), p2 = argPeak(r.L, 100 + 2 * d - 50, 100 + 2 * d + 50);
        CHECK(std::abs(p1 - (100 + d)) <= 1, "first echo at 350 ms (off by %d samples)", p1 - 100 - d);
        CHECK(std::fabs(std::fabs(r.L[p1]) - 1.f) < 0.02f, "first echo at full level (%.3f)", r.L[p1]);
        CHECK(std::fabs(std::fabs(r.L[p2]) - 0.35f) < 0.02f, "second echo = feedback 35%% (%.3f)", r.L[p2]);
        CHECK(peakIn(r.L, 200, 100 + d - 100) < 1e-4f, "silence between input and first echo");
    }
    { // tempo sync: 1/8. at 120 bpm = 375 ms
        Run r; r.p[voiceIndex(0, vSync)] = 1; r.p[voiceIndex(0, vDiv)] = 7; r.go(SR, impulse);
        int p1 = argPeak(r.L, 200, SR); CHECK(std::abs(p1 - 100 - int(0.375 * SR)) <= 1, "synced 1/8. = 375 ms (%d)", p1 - 100);
    }
    { // ping-pong
        Run r; r.p[gPingPong] = 1; r.p[voiceIndex(0, vFeedback)] = 60; r.go(SR * 2, impulse);
        int d = int(0.350 * SR);
        CHECK(peakIn(r.L, 100 + d - 20, 100 + d + 20) > 0.9f && peakIn(r.R, 100 + d - 20, 100 + d + 20) < 0.01f, "ping-pong: 1st echo left only");
        CHECK(peakIn(r.R, 100 + 2 * d - 20, 100 + 2 * d + 20) > 0.5f && peakIn(r.L, 100 + 2 * d - 20, 100 + 2 * d + 20) < 0.01f, "ping-pong: 2nd echo right only");
    }
    { // reverse: a rising chirp-like burst comes back with its loud end first
        Run r; r.p[voiceIndex(0, vRev)] = 1; r.p[voiceIndex(0, vFeedback)] = 0; r.p[voiceIndex(0, vTime)] = 400;
        // decaying burst from sample 0: loud at start, quiet at the end (over 300 ms)
        r.go(SR * 2, [](int i) { return i < 14400 ? std::exp(-float(i) / 2400.f) * std::sin(6.2831853f * 500.f * float(i) / SR) : 0.f; });
        CHECK(finite(r.L), "reverse: finite output");
        // find the loudest 20 ms window; in the reversed copy the loud part comes late, quieter build-up before
        int pk = argPeak(r.L, 0, SR * 2);
        double before = rms(r.L, pk - 4800, pk - 960), after = rms(r.L, pk + 960, pk + 4800);
        CHECK(before > after * 3.0, "reverse: swell before the peak, cut after it (before %.4f, after %.4f, peak@%d ms)", before, after, pk * 1000 / SR);
        CHECK(peakIn(r.L, 0, SR * 2) > 0.3f, "reverse: audible (peak %.2f)", peakIn(r.L, 0, SR * 2));
    }
    { // pitch +12 in the loop: echo of a 440 tone is 880, second echo 1760
        Run r; r.p[voiceIndex(0, vPitch)] = 12; r.p[voiceIndex(0, vFeedback)] = 80; r.p[voiceIndex(0, vTime)] = 500;
        r.go(SR * 2, [](int i) { return i < 12000 ? tone(i) : 0.f; });
        double f1 = zcFreq(r.L, int(0.56 * SR), int(0.70 * SR)), f2 = zcFreq(r.L, int(1.08 * SR), int(1.16 * SR));
        CHECK(std::fabs(f1 - 880) < 40, "pitch +12: first echo %.0f Hz (want 880)", f1);
        CHECK(std::fabs(f2 - 1760) < 120, "pitch +12: second echo %.0f Hz (want 1760)", f2);
    }
    { // tape mode: shortening the time while a tone is in the line raises the pitch
        Run r; r.p[voiceIndex(0, vFeedback)] = 0; r.p[voiceIndex(0, vTime)] = 800;
        r.go(SR, tone); r.p[gTimeMul] = 0.5f; r.go(4800, tone);
        double f = zcFreq(r.L, SR + 600, SR + 3000);
        CHECK(f > 600, "tape: pitch rises while time shrinks (%.0f Hz from 440)", f);
        r.go(SR, tone); double f2 = zcFreq(r.L, int(r.L.size()) - 9600, int(r.L.size()));
        CHECK(std::fabs(f2 - 440) < 8, "tape: pitch settles back (%.0f Hz)", f2);
        CHECK(std::fabs(r.out[kOutNow - kInputCount] - 400.f) < 2.f, "tape: delay settles at 400 ms (%.1f)", r.out[kOutNow - kInputCount]);
    }
    { // jump mode: no pitch change, time switches within a few ms, no click bigger than the signal
        Run r; r.p[gTimeMode] = 1; r.p[voiceIndex(0, vFeedback)] = 0; r.p[voiceIndex(0, vTime)] = 800;
        r.go(SR, tone); r.p[gTimeMul] = 0.5f; r.go(SR / 2, tone);
        CHECK(std::fabs(r.out[kOutNow - kInputCount] - 400.f) < 0.5f, "jump: delay is 400 ms right away (%.1f)", r.out[kOutNow - kInputCount]);
        double f = zcFreq(r.L, SR + 2400, SR + 12000);
        CHECK(std::fabs(f - 440) < 8, "jump: pitch unchanged (%.0f Hz)", f);
        CHECK(peakIn(r.L, SR, SR + 2400) <= 0.52f, "jump: no click (peak %.3f)", peakIn(r.L, SR, SR + 2400));
        CHECK(peakIn(r.L, SR + 100, SR + 180) < 0.2f, "jump: gate dips during the switch (%.3f)", peakIn(r.L, SR + 100, SR + 180));
    }
    { // freeze: the loop keeps sounding with no input and no feedback, and stops growing
        Run r; r.p[voiceIndex(0, vFeedback)] = 0; r.p[voiceIndex(0, vTime)] = 300;
        r.go(SR, tone); r.p[gFreeze] = 1; r.go(SR * 3, [](int) { return 0.f; });
        double a = rms(r.L, SR * 2, SR * 3), b = rms(r.L, SR * 3, SR * 4);
        CHECK(a > 0.25 && std::fabs(a - b) < 0.02, "freeze: holds level (%.3f then %.3f)", a, b);
        r.p[gTimeMul] = 0.1f; r.go(SR, [](int) { return 0.f; });
        CHECK(rms(r.L, SR * 4 + SR / 2, SR * 5) > 0.15, "freeze: still sounding with a 30 ms loop (%.3f)", rms(r.L, SR * 4 + SR / 2, SR * 5));
        r.p[gFreeze] = 0; r.p[gTimeMul] = 1.f; r.go(SR * 2, [](int) { return 0.f; });
        CHECK(rms(r.L, int(r.L.size()) - SR / 2, int(r.L.size())) < 1e-3, "freeze off: line empties (%.5f)", rms(r.L, int(r.L.size()) - SR / 2, int(r.L.size())));
    }
    { // crusher: 3 bits gives few distinct output levels; rate reduction holds samples
        Run r; r.p[gBits] = 3; r.p[voiceIndex(0, vFeedback)] = 0; r.go(SR, tone);
        std::vector<int> lv; for (int i = SR / 2; i < SR; ++i) { int q = int(std::lround(r.L[i] * 1000)); if (std::find(lv.begin(), lv.end(), q) == lv.end()) lv.push_back(q); }
        CHECK(lv.size() <= 6, "crusher 3 bit: %d distinct levels", int(lv.size()));
        Run s; s.p[gCrRate] = 2000; s.p[voiceIndex(0, vFeedback)] = 0; s.go(SR, tone);
        int same = 0; for (int i = SR / 2 + 1; i < SR; ++i) if (s.L[i] == s.L[i - 1]) ++same;
        CHECK(same > SR / 2 * 0.9, "crusher 2 kHz: %d%% of samples are held", same * 100 / (SR / 2));
        Run t; t.p[gBits] = 3; t.p[voiceIndex(0, vCrush)] = 0; t.p[voiceIndex(0, vFeedback)] = 0; t.go(SR, tone);
        CHECK(std::fabs(rms(t.L, SR / 2, SR) - 0.3536) < 0.005, "voice Crush 0%%: clean (%.4f)", rms(t.L, SR / 2, SR));
    }
    { // chorus on repeats: detunes the echo (frequency wobbles), dry stays untouched
        Run r; r.p[gChDetune] = 30; r.p[gChMix] = 100; r.p[gChRate] = 1; r.p[voiceIndex(0, vFeedback)] = 0; r.go(SR * 2, tone);
        double lo = 1e9, hi = 0; for (int a = SR; a + 4800 <= SR * 2; a += 2400) { double f = zcFreq(r.L, a, a + 4800); lo = std::min(lo, f); hi = std::max(hi, f); }
        double cents = 1200 * std::log2(hi / lo) / 2;
        CHECK(cents > 12 && cents < 40, "chorus 30 ct: echo wobbles by about +-%.0f ct (%.1f..%.1f Hz)", cents, lo, hi);
    }
    { // ducking
        Run r; r.p[gDuck] = 100; r.p[gMix] = 50; r.p[voiceIndex(0, vFeedback)] = 60; r.go(SR * 2, tone);
        CHECK(r.out[kOutDuck - kInputCount] > 0.95f, "ducking: fully ducked while playing (%.2f)", r.out[kOutDuck - kInputCount]);
        CHECK(std::fabs(rms(r.L, SR, SR * 2) - 0.3536) < 0.01, "ducking: only dry while playing (%.4f)", rms(r.L, SR, SR * 2));
        r.go(SR * 2, [](int) { return 0.f; });
        CHECK(peakIn(r.L, SR * 2 + SR / 2, SR * 3) > 0.1f, "ducking: repeats come back in the pause (%.3f)", peakIn(r.L, SR * 2 + SR / 2, SR * 3));
    }
    { // random glitches: all states show up, output stays sane
        Run r; r.p[gGlProb] = 100; r.p[voiceIndex(0, vTime)] = 120; r.p[voiceIndex(0, vFeedback)] = 60;
        int seen[4] = {0, 0, 0, 0};
        for (int k = 0; k < 400; ++k) { r.go(1200, tone); ++seen[int(r.out[kOutState - kInputCount])]; }
        CHECK(seen[1] > 0 && seen[2] > 0 && seen[3] > 0 && seen[0] == 0, "glitches at 100%%: reverse %d, jump %d, stutter %d", seen[1], seen[2], seen[3]);
        CHECK(finite(r.L) && peakIn(r.L, 0, int(r.L.size())) < 3.f, "glitches: output bounded (peak %.2f)", peakIn(r.L, 0, int(r.L.size())));
        Run q; q.p[gGlProb] = 100; q.p[gGlRev] = 0; q.p[gGlJump] = 0; q.p[gGlStut] = 0; q.go(SR, tone);
        CHECK(q.out[kOutState - kInputCount] == 0.f, "glitches: no types enabled -> nothing happens");
    }
    { // per-voice note values: one voice synced, one manual
        Run r; setVoices(r.p, 2); setAll(r.p, vFeedback, 0.f); r.p[voiceIndex(0, vSync)] = 1; r.p[voiceIndex(0, vDiv)] = 5; r.p[voiceIndex(1, vTime)] = 200;
        r.go(SR, impulse);
        CHECK(peakIn(r.L, 100 + 9600 - 3, 100 + 9600 + 3) > 0.5f && peakIn(r.L, 100 + 24000 - 3, 100 + 24000 + 3) > 0.5f, "voice 1 on 1/4 (500 ms), voice 2 manual 200 ms");
    }
    { // time sweep: once per cycle the time jumps and glides back; Down raises the pitch, Up lowers it
        for (int mode = 1; mode <= 2; ++mode) {
            Run r; r.p[voiceIndex(0, vFeedback)] = 0; r.p[voiceIndex(0, vTime)] = 400; r.p[gSwMode] = float(mode); r.p[gSwTime] = 1000; r.p[gSwDepth] = 50; r.p[gSwGlide] = 40;
            float lo = 1e9f, hi = 0; int kicks = 0; float prev = 400;
            double fmin = 1e9, fmax = 0;
            r.go(SR, tone);
            for (int k = 0; k < 300; ++k) { r.go(480, tone, 480); float now = r.out[kOutNow - kInputCount]; lo = std::min(lo, now); hi = std::max(hi, now);
                if (std::fabs(now - prev) > 60) ++kicks;
                prev = now;
                double f = zcFreq(r.L, int(r.L.size()) - 480, int(r.L.size())); if (rms(r.L, int(r.L.size()) - 480, int(r.L.size())) > 0.2) { fmin = std::min(fmin, f); fmax = std::max(fmax, f); } }
            if (mode == 1) CHECK(lo < 230 && hi < 402 && hi > 395 && kicks == 3 && fmin < 380, "sweep Up: time %.0f..%.0f ms, %d cycles in 3 s, pitch dips to %.0f Hz", lo, hi, kicks, fmin);
            else CHECK(hi > 570 && lo > 398 && lo < 405 && kicks == 3 && fmax > 520, "sweep Down: time %.0f..%.0f ms, %d cycles in 3 s, pitch rises to %.0f Hz", lo, hi, kicks, fmax);
            CHECK(finite(r.L) && peakIn(r.L, SR, int(r.L.size())) <= 0.75f, "sweep %s: no click (peak %.3f)", mode == 1 ? "Up" : "Down", peakIn(r.L, SR, int(r.L.size())));
        }
        Run s2; s2.p[gSwMode] = 2; s2.p[gSwSync] = 1; s2.p[gSwDiv] = 7; s2.p[voiceIndex(0, vTime)] = 300; int kicks = 0; float prev = 300;
        s2.go(SR, tone); for (int k = 0; k < 300; ++k) { s2.go(480, tone, 480); float now = s2.out[kOutNow - kInputCount]; if (now - prev > 40) ++kicks; prev = now; }
        CHECK(kicks == 8, "sweep synced to 1/8. at 120 BPM: %d cycles in 3 s (want 8)", kicks);
    }
    { // one-way sweep: time only travels away from the voice's own time, then restarts; pitch offset is steady
        for (int mode = 1; mode <= 2; ++mode) {
            Run r; r.p[voiceIndex(0, vFeedback)] = 0; r.p[voiceIndex(0, vTime)] = 400; r.p[gSwMode] = float(mode); r.p[gSwOneWay] = 1; r.p[gSwTime] = 1000; r.p[gSwDepth] = 50; r.p[gSwGlide] = 100;
            r.go(SR, tone);
            float lo = 1e9f, hi = 0, prev = r.out[kOutNow - kInputCount]; int wrong = 0, resets = 0; double fsum = 0; int fn = 0;
            for (int k = 0; k < 300; ++k) { r.go(480, tone, 480); float now = r.out[kOutNow - kInputCount]; lo = std::min(lo, now); hi = std::max(hi, now);
                const float d = now - prev; prev = now;
                if (std::fabs(d) > 100) { ++resets; continue; }
                if (mode == 1 ? d < -0.01f : d > 0.01f) ++wrong;
                if (rms(r.L, int(r.L.size()) - 480, int(r.L.size())) > 0.3) { fsum += zcFreq(r.L, int(r.L.size()) - 480, int(r.L.size())); ++fn; } }
            const double f = fsum / std::max(1, fn);
            if (mode == 1) CHECK(lo > 398 && hi > 590 && hi < 601 && wrong == 0 && resets == 3 && std::fabs(f - 440 * 0.8) < 25, "one way Up: %.0f -> %.0f ms, never goes back (%d), %d restarts in 3 s, steady %.0f Hz (want 352)", lo, hi, wrong, resets, f);
            else CHECK(hi < 402 && lo < 210 && lo > 199 && wrong == 0 && resets == 3 && std::fabs(f - 440 * 1.2) < 25, "one way Down: %.0f -> %.0f ms, never goes back (%d), %d restarts in 3 s, steady %.0f Hz (want 528)", hi, lo, wrong, resets, f);
            CHECK(finite(r.L) && peakIn(r.L, SR, int(r.L.size())) <= 0.75f, "one way %s: no click (peak %.3f)", mode == 1 ? "Up" : "Down", peakIn(r.L, SR, int(r.L.size())));
        }
        Run h; h.p[voiceIndex(0, vTime)] = 400; h.p[gSwMode] = 2; h.p[gSwOneWay] = 1; h.p[gSwTime] = 2000; h.p[gSwDepth] = 50; h.p[gSwGlide] = 25;
        h.go(SR + SR / 2 + 4800, tone); const float a = h.out[kOutNow - kInputCount]; h.go(SR / 4, tone);
        CHECK(std::fabs(a - 200.f) < 3.f && std::fabs(h.out[kOutNow - kInputCount] - a) < 0.5f, "one way with Glide 25%%: arrives and holds at %.0f ms", a);
    }
    { // everything at once, full feedback, sweeping the time: must stay finite and bounded
        for (int mode = 0; mode < 2; ++mode) {
            Run r; setVoices(r.p, 6); setAll(r.p, vFeedback, 100.f); r.p[gFbMul] = 150; r.p[gTimeMode] = float(mode);
            r.p[voiceIndex(1, vRev)] = 1; r.p[voiceIndex(2, vPitch)] = 12; r.p[voiceIndex(3, vPitch)] = -12; r.p[voiceIndex(4, vRev)] = 1; r.p[voiceIndex(4, vPitch)] = 7;
            r.p[gSpTime] = 100; r.p[gSpPan] = 100; r.p[gSpDetune] = 20; r.p[gChDetune] = 50; r.p[gBits] = 4; r.p[gCrRate] = 3000;
            r.p[gSwMode] = float(1 + mode); r.p[gSwOneWay] = float(mode); r.p[gSwTime] = 300; r.p[gSwDepth] = 95; r.p[gGlProb] = 60; r.p[gWarmth] = 50; r.p[gLoCut] = 200; r.p[gHiCut] = 4000; r.p[gPingPong] = 1; r.p[gDuck] = 50;
            for (int k = 0; k < 600; ++k) { r.p[gTimeMul] = 0.03f * std::pow(2.f / 0.03f, 0.5f + 0.5f * std::sin(k * 0.05f)); r.p[gFreeze] = (k / 97) % 2 ? 1.f : 0.f; r.go(480, tone, 37); }
            CHECK(finite(r.L) && finite(r.R), "torture (%s): finite", mode ? "jump" : "tape");
            CHECK(peakIn(r.L, 0, int(r.L.size())) < 12.f, "torture (%s): bounded (peak %.2f)", mode ? "jump" : "tape", peakIn(r.L, 0, int(r.L.size())));
        }
    }
    { // switching a voice on later plays no stale audio burst; sample rates
        for (double sr : { 44100.0, 96000.0, 192000.0 }) {
            Engine e; e.setSampleRate(sr); float p[kInputCount]; presetDefaults(p); float o[kOutputCount];
            std::vector<float> in(4096, 0.f), ol(4096), orr(4096); in[10] = 1.f;
            bool ok = true; float pk = 0; int at = 0;
            for (int k = 0; k < int(sr) / 4096; ++k) { e.process(in.data(), in.data(), ol.data(), orr.data(), 4096, p, 0, o); in[10] = 0; for (int i = 0; i < 4096; ++i) { ok &= std::isfinite(ol[i]); if (k * 4096 + i > 100 && std::fabs(ol[i]) > pk) { pk = std::fabs(ol[i]); at = k * 4096 + i; } } }
            CHECK(ok && std::fabs(at - 10 - 0.35 * sr) <= 2, "%.0f Hz: echo at %.1f ms", sr, (at - 10) * 1000.0 / sr);
        }
    }
    { // speed
        Run r; setVoices(r.p, 6); r.p[voiceIndex(1, vRev)] = 1; r.p[voiceIndex(2, vPitch)] = 12; r.p[gChDetune] = 20; r.p[gBits] = 6; r.p[gWarmth] = 40; r.p[gHiCut] = 5000; r.p[gGlProb] = 30;
        auto t0 = std::chrono::steady_clock::now(); r.go(SR * 20, tone, 256);
        double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        CHECK(s < 2.0, "speed: 20 s of audio with 6 busy voices in %.2f s (%.1f%% of one core)", s, s / 20 * 100);
    }
    std::printf("%s (%d failed)\n", fails ? "FAILED" : "ALL PASSED", fails);
    return fails ? 1 : 0;
}
