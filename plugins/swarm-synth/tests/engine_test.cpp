// Offline checks of the Swarm Synth engine: pitch detector accuracy and delay, both modes, presets.
//   g++ -O2 -std=c++17 tests/engine_test.cpp -o engine_test && ./engine_test
#include "../../../shared/guitar-engine/Engine.hpp"
#include <chrono>
#include <cstdio>
#include <random>
using namespace swsyn;

static std::mt19937 rng(7);

// a plucked-string-like note: pick noise, harmonics with their own decay, slight inharmonicity
static void pluck(std::vector<float>& out, int at, int len, double f, double sr, double amp, double h2 = 0.8)
{
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    for (int i = 0; i < len && at + i < int(out.size()); ++i)
    {
        const double t = i / sr;
        double s = 0.0;
        for (int h = 1; h <= 12; ++h)
        {
            const double fh = f * h * std::sqrt(1.0 + 0.00012 * h * h);
            if (fh > sr * 0.45) break;
            const double a = (h == 1 ? 1.0 : (h == 2 ? h2 : 1.0 / h)) * std::exp(-t * (1.5 + 0.9 * h));
            s += a * std::sin(2 * M_PI * fh * t + 0.7 * h);
        }
        s += 0.6 * u(rng) * std::exp(-t / 0.002);           // pick click
        const double fade = std::min(1.0, t / 0.0005);
        out[at + i] += float(amp * 0.5 * fade * s);
    }
}

static double cents(double f, double ref) { return 1200.0 * std::log2(f / ref); }

// fundamental of a signal segment by autocorrelation with parabolic interpolation (reference measurement)
static double measureHz(const float* x, int n, double sr, double expect)
{
    const int lo = int(sr / (expect * 1.3)), hi = int(sr / (expect / 1.3));
    double best = -1e9; int bt = lo;
    std::vector<double> r(hi + 2);
    for (int tau = lo - 1; tau <= hi + 1; ++tau)
    {
        double acc = 0, m = 0;
        for (int i = 0; i + tau < n; ++i) { acc += double(x[i]) * x[i + tau]; m += double(x[i]) * x[i] + double(x[i + tau]) * x[i + tau]; }
        r[tau] = m > 0 ? 2 * acc / m : 0;
        if (tau >= lo && tau <= hi && r[tau] > best) { best = r[tau]; bt = tau; }
    }
    const double den = r[bt - 1] - 2 * r[bt] + r[bt + 1];
    const double off = den < 0 ? 0.5 * (r[bt - 1] - r[bt + 1]) / den : 0;
    return sr / (bt + off);
}

int main()
{
    int fails = 0;
    const auto check = [&fails](bool ok, const char* what) { if (!ok) { ++fails; std::printf("   ^^^ FAIL: %s\n", what); } };

    // ---- 1. pitch detector alone: steady sines, accuracy
    for (double sr : { 44100.0, 48000.0, 96000.0 })
    {
        double worst = 0;
        for (double f : { 61.74, 82.41, 110.0, 146.83, 196.0, 246.94, 329.63, 440.0, 659.25, 987.77, 1318.5 })
        {
            Tracker t; t.setSampleRate(sr);
            double got = 0;
            for (int i = 0; i < int(sr * 0.3); ++i)
                if (t.push(float(0.3 * std::sin(2 * M_PI * f * i / sr)), 1.f) && i > sr * 0.2) got = t.freq;
            worst = std::max(worst, std::fabs(cents(got, f)));
        }
        std::printf("tracker, sines 62..1319 Hz @ %.0f: worst error %.2f cents\n", sr, worst);
        check(worst < 3.0, "sine accuracy");
    }

    // ---- 2. whole engine, pitch tracking mode: plucked notes, time until the right note sounds
    const double sr = 48000.0;
    static const double kNotes[] = { 82.41, 110.0, 146.83, 196.0, 246.94, 329.63, 440.0, 659.25, 987.77 };
    for (float win : { 0.5f, 1.f, 2.f })
    {
        std::printf("pitch tracking, window %.1f periods: note -> delay / error\n  ", win);
        int wrong = 0;
        for (double f : kNotes)
            for (double h2 : { 0.8, 2.5 })   // normal, and second harmonic much stronger than the fundamental
            {
                const int N = int(sr * 0.5);
                std::vector<float> in(N, 0.f), oL(N), oR(N);
                pluck(in, 2400, N, f, sr, 0.6, h2);
                float p[kInputCount]; presetDefaults(p); setVoices(p, 1); setAll(p, vWave, float(wSine));
                p[gFltOn] = 0.f; p[gTrack] = win; p[gSpDetune] = 0.f;
                Engine e; e.setSampleRate(sr);
                float outp[kOutputCount]; float lat = -1;
                for (int o = 0; o < N; o += 32)
                {
                    e.process(&in[o], &in[o], &oL[o], &oR[o], 32, p, outp);
                    if (lat < 0 && outp[kOutFreq - kInputCount] > 0) lat = outp[kOutLatency - kInputCount];
                }
                const double got = measureHz(&oL[int(sr * 0.15)], int(sr * 0.1), sr, f);
                const double c = cents(got, f * std::sqrt(1.0 + 0.00012));
                if (std::fabs(c) > 12.0 || lat < 0) ++wrong;
                if (h2 < 1.0) std::printf("%.0fHz %.1fms/%+.1fct  ", f, lat, c);
            }
        std::printf("\n  wrong or missing notes (18 cases incl. strong 2nd harmonic): %d\n", wrong);
        if (win >= 1.f) check(wrong == 0, "tracked notes");
    }

    // ---- 3. reshape mode: the oscillator must follow the input frequency from the very start
    {
        std::printf("reshape mode: note -> error after 5 ms / in the steady part\n  ");
        int wrong = 0;
        for (double f : kNotes)
            for (double h2 : { 0.8, 2.5 })
            {
                const int N = int(sr * 0.5);
                std::vector<float> in(N, 0.f), oL(N), oR(N);
                pluck(in, 2400, N, f, sr, 0.6, h2);
                float p[kInputCount]; presetDefaults(p); setVoices(p, 1); setAll(p, vWave, float(wSine));
                p[gFltOn] = 0.f; p[gMode] = float(mReshape); p[gSpDetune] = 0.f;
                Engine e; e.setSampleRate(sr);
                float outp[kOutputCount];
                // a previous, different note so that the band-limit starts from the wrong place
                std::vector<float> pre(N, 0.f), po(N);
                pluck(pre, 0, N, f * 1.5, sr, 0.6);
                e.process(pre.data(), pre.data(), po.data(), po.data(), N, p, outp);
                std::vector<float> sil(int(sr * 0.3), 0.f), so(sil.size());
                e.process(sil.data(), sil.data(), so.data(), so.data(), uint32_t(sil.size()), p, outp);
                e.process(in.data(), in.data(), oL.data(), oR.data(), N, p, outp);
                const int start = 2400 + int(sr * 0.005);
                const double ref = f * std::sqrt(1.0 + 0.00012);
                const double early = cents(measureHz(&oL[start], int(sr * 0.06), sr, f), ref);
                const double late = cents(measureHz(&oL[int(sr * 0.15)], int(sr * 0.1), sr, f), ref);
                float peakEarly = 0; for (int i = 2400; i < 2400 + int(sr * 0.004); ++i) peakEarly = std::max(peakEarly, std::fabs(oL[i]));
                if (std::fabs(late) > 12.0 || peakEarly < 0.01f) ++wrong;
                if (h2 < 1.0) std::printf("%.0fHz %+.1f/%+.1fct  ", f, early, late); else std::printf("[h2: %+.0f/%+.0fct pk %.3f]  ", early, late, peakEarly);
            }
        std::printf("\n  wrong notes or no sound within 4 ms (18 cases): %d\n", wrong);
        check(wrong == 0, "reshape notes");
    }

    // ---- 4. octave / fifth / detune ratios
    {
        const int N = int(sr);
        std::vector<float> in(N), oL(N), oR(N);
        for (int i = 0; i < N; ++i) in[i] = float(0.3 * std::sin(2 * M_PI * 220.0 * i / sr));
        struct C { float oct, semi, det; double expect; const char* name; };
        const C cs[] = { { 1, 0, 0, 440.0, "+1 oct" }, { -1, 0, 0, 110.0, "-1 oct" }, { 0, 7, 0, 220.0 * std::pow(2.0, 7 / 12.0), "+7 st" },
                         { 0, 0, 25, 220.0 * std::pow(2.0, 25 / 1200.0), "+25 ct" } };
        for (int mode = 0; mode < 2; ++mode)
            for (const C& c : cs)
            {
                float p[kInputCount]; presetDefaults(p); setVoices(p, 1); setAll(p, vWave, float(wSine));
                p[gFltOn] = 0.f; p[gMode] = float(mode); p[gSpDetune] = 0.f;
                setAll(p, vOct, c.oct); setAll(p, vSemi, c.semi); setAll(p, vDetune, c.det);
                Engine e; e.setSampleRate(sr); float outp[kOutputCount];
                e.process(in.data(), in.data(), oL.data(), oR.data(), N, p, outp);
                const double got = measureHz(&oL[N / 2], N / 4, sr, c.expect);
                std::printf("%s %-7s -> %.2f Hz (expect %.2f)\n", mode ? "reshape " : "tracking", c.name, got, c.expect);
                check(std::fabs(cents(got, c.expect)) < 3.0, "interval");
            }
    }

    // ---- 5. every factory preset: no NaN, sane level, CPU
    {
        const int N = int(sr * 6);
        std::vector<float> in(N, 0.f), oL(N), oR(N);
        static const double seq[] = { 110.0, 164.81, 220.0, 329.63, 146.83, 440.0 };
        for (int k = 0; k < 12; ++k) pluck(in, int(k * sr * 0.5), int(sr * 0.5), seq[k % 6], sr, 0.5);
        for (int pr = 0; pr < kPresetCount; ++pr)
        {
            float p[kInputCount]; kPresets[pr].fill(p);
            Engine e; e.setSampleRate(sr);
            std::vector<float> table(kWaveSize);
            const bool wok = compileWave(presetWave(pr), table.data());
            e.setWave(table.data());
            float outp[kOutputCount]; float peak = 0; double rms = 0; bool bad = false;
            const auto t0 = std::chrono::steady_clock::now();
            for (int o = 0; o < N; o += 64) e.process(&in[o], &in[o], &oL[o], &oR[o], 64, p, outp);
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            for (int i = 0; i < N; ++i)
            {
                if (!std::isfinite(oL[i]) || !std::isfinite(oR[i])) bad = true;
                peak = std::max(peak, std::max(std::fabs(oL[i]), std::fabs(oR[i]))); rms += double(oL[i]) * oL[i];
            }
            std::printf("%-30s peak %.2f rms %.3f cpu %.1f%% %s%s\n", kPresets[pr].name, peak, std::sqrt(rms / N), ms / 60.0,
                        bad ? "NAN!" : "ok", wok ? "" : " WAVE DID NOT COMPILE");
            check(!bad && wok && peak < 2.5f && rms > 0, "preset");
        }
    }

    // ---- 6. extremes
    {
        const int N = int(sr * 2);
        std::vector<float> in(N), oL(N), oR(N);
        std::uniform_real_distribution<float> u(-1.f, 1.f);
        for (int i = 0; i < N; ++i) in[i] = (i / 4800) % 2 ? u(rng) : 0.9f * float(std::sin(2 * M_PI * 3000.0 * i / sr));
        for (int mode = 0; mode < 2; ++mode)
        {
            float p[kInputCount]; presetDefaults(p); setVoices(p, 8); p[gMode] = float(mode);
            p[gReso] = 100.f; p[gDrive] = 36.f; p[gFltEnv] = 5.f; p[gCutoff] = 18000.f; setAll(p, vOct, 2.f); setAll(p, vSemi, 12.f); setAll(p, vLevel, 150.f);
            p[gMidiOut] = 1.f;
            Engine e; e.setSampleRate(sr); float outp[kOutputCount]; float pk = 0; bool bad = false; int ev = 0;
            for (int o = 0; o < N; o += 64) { e.process(&in[o], &in[o], &oL[o], &oR[o], 64, p, outp); ev += e.midiCount(); }
            for (int i = 0; i < N; ++i) { if (!std::isfinite(oL[i])) bad = true; pk = std::max(pk, std::fabs(oL[i])); }
            std::printf("extreme settings, noise and 3 kHz bursts, mode %d: peak %.2f, %d MIDI events, %s\n", mode, pk, ev, bad ? "NAN" : "ok");
            check(!bad && pk < 6.f, "extreme");
        }
    }

    // ---- 7. MIDI out: a three-note line gives three note-ons with the right numbers
    {
        const int N = int(sr * 1.5);
        std::vector<float> in(N, 0.f), oL(N), oR(N);
        pluck(in, 0, int(sr * 0.5), 110.0, sr, 0.5); pluck(in, int(sr * 0.5), int(sr * 0.5), 164.81, sr, 0.5); pluck(in, int(sr), int(sr * 0.5), 220.0, sr, 0.5);
        float p[kInputCount]; presetDefaults(p); p[gMidiOut] = 1.f;
        Engine e; e.setSampleRate(sr); float outp[kOutputCount];
        std::printf("MIDI out:");
        int ons = 0, offs = 0;
        for (int o = 0; o < N; o += 64)
        {
            e.process(&in[o], &in[o], &oL[o], &oR[o], 64, p, outp);
            for (int k = 0; k < e.midiCount(); ++k)
            {
                const MidiEv& m = e.midiEvent(k);
                if (m.status == 0x90) { ++ons; std::printf(" on %d (vel %d) @%.0fms", m.note, m.velocity, (o + m.frame) * 1000.0 / sr); } else ++offs;
            }
        }
        std::printf("  | offs %d\n", offs);
        check(ons == 3, "midi notes (expect 45, 52, 57)");
    }

    // ---- 8. other sample rates, both modes
    for (double sr2 : { 44100.0, 96000.0 })
        for (int mode = 0; mode < 2; ++mode)
            for (double f : { 82.41, 440.0 })
            {
                const int N = int(sr2 * 0.5);
                std::vector<float> in(N, 0.f), oL(N), oR(N);
                pluck(in, int(sr2 * 0.05), N, f, sr2, 0.6);
                float p[kInputCount]; presetDefaults(p); setVoices(p, 1); setAll(p, vWave, float(wSine));
                p[gFltOn] = 0.f; p[gMode] = float(mode); p[gSpDetune] = 0.f;
                Engine e; e.setSampleRate(sr2); float outp[kOutputCount]; float lat = -1;
                for (int o = 0; o + 32 <= N; o += 32)
                {
                    e.process(&in[o], &in[o], &oL[o], &oR[o], 32, p, outp);
                    if (lat < 0 && outp[kOutFreq - kInputCount] > 0) lat = outp[kOutLatency - kInputCount];
                }
                const double c = cents(measureHz(&oL[int(sr2 * 0.2)], int(sr2 * 0.1), sr2, f), f * std::sqrt(1.0 + 0.00012));
                std::printf("%.0f Hz, %s, %.0f Hz note: %+.1f ct, delay %.1f ms\n", sr2, mode ? "reshape " : "tracking", f, c, lat);
                check(std::fabs(c) < 5.0 && lat >= 0, "other sample rate");
            }

    // ---- 9. hammer-on and bend: a tone that changes pitch with no new pick
    for (int mode = 0; mode < 2; ++mode)
    {
        const int N = int(sr);
        std::vector<float> in(N), oL(N), oR(N);
        double ph = 0;
        for (int i = 0; i < N; ++i)
        {
            const double t = i / sr;
            // 220 Hz, hammer-on to 277 Hz at 0.3 s, then a whole-tone bend up between 0.6 s and 0.8 s
            double f = t < 0.3 ? 220.0 : 277.18;
            if (t > 0.6) f *= std::pow(2.0, std::min(1.0, (t - 0.6) / 0.2) * 2.0 / 12.0);
            ph += f / sr;
            in[i] = float(0.3 * (std::sin(2 * M_PI * ph) + 0.4 * std::sin(4 * M_PI * ph) + 0.2 * std::sin(6 * M_PI * ph)));
        }
        float p[kInputCount]; presetDefaults(p); setVoices(p, 1); setAll(p, vWave, float(wSine));
        p[gFltOn] = 0.f; p[gMode] = float(mode); p[gSpDetune] = 0.f;
        Engine e; e.setSampleRate(sr); float outp[kOutputCount];
        float after = 0; double worstBend = 0;
        for (int o = 0; o + 48 <= N; o += 48)
        {
            e.process(&in[o], &in[o], &oL[o], &oR[o], 48, p, outp);
            const double t = (o + 48) / sr, got = outp[kOutFreq - kInputCount];
            if (after == 0 && t > 0.3 && std::fabs(cents(got, 277.18)) < 20) after = float((t - 0.3) * 1000.0);
            if (t > 0.62 && t < 0.8)
            {
                const double tl = t - 0.0005;   // the reading is for the middle of the block
                worstBend = std::max(worstBend, std::fabs(cents(got, 277.18 * std::pow(2.0, std::min(1.0, (tl - 0.6) / 0.2) * 2.0 / 12.0))));
            }
        }
        const double endHz = measureHz(&oL[int(sr * 0.85)], int(sr * 0.1), sr, 311.13);
        std::printf("%s: hammer-on followed after %.0f ms, worst lag during a 200 ms whole-tone bend %.0f ct, end note %+.1f ct\n",
                    mode ? "reshape " : "tracking", after, worstBend, cents(endHz, 311.13));
        check(after > 0 && after < 30 && worstBend < 30 && std::fabs(cents(endHz, 311.13)) < 3, "legato / bend");
    }

    // ---- 10. quantize, hold (with mute), vibrato, pick strength to cutoff
    for (int mode = 0; mode < 2; ++mode)
        for (int env = 0; env < 2; ++env)
        {
            // a 225 Hz note (39 cents above A3) that dies away in half a second, then silence, then at 3 s a
            // steady note that is muted abruptly at 3.6 s
            const int N = int(sr * 5);
            std::vector<float> in(N, 0.f), oL(N), oR(N);
            for (int i = 0; i < N; ++i)
            {
                const double t = i / sr;
                double a = 0;
                if (t < 2.0) a = 0.4 * std::exp(-t * 9.0) * std::min(1.0, t / 0.001);
                else if (t >= 3.0 && t < 3.6) a = 0.4 * std::exp(-(t - 3.0) * 1.0) * std::min(1.0, (t - 3.0) / 0.001);
                else if (t >= 3.6) a = 0.4 * std::exp(-0.6) * std::exp(-(t - 3.6) / 0.004);
                in[i] = float(a * (std::sin(2 * M_PI * 225.0 * t) + 0.3 * std::sin(4 * M_PI * 225.0 * t)));
            }
            float p[kInputCount]; presetDefaults(p); setVoices(p, 1); setAll(p, vWave, float(wSine));
            p[gFltOn] = 0.f; p[gMode] = float(mode); p[gEnvMode] = float(env); p[gSpDetune] = 0.f; p[gSustain] = 100.f;
            p[gQuant] = 1.f; p[gHold] = 1.f;
            Engine e; e.setSampleRate(sr); float outp[kOutputCount];
            for (int o = 0; o + 48 <= N; o += 48) e.process(&in[o], &in[o], &oL[o], &oR[o], 48, p, outp);
            const auto rms = [&oL, sr](double t0, double t1) { double s = 0; int n = 0; for (int i = int(t0 * sr); i < int(t1 * sr); ++i) { s += double(oL[i]) * oL[i]; ++n; } return std::sqrt(s / n); };
            const double hz = measureHz(&oL[int(sr * 1.5)], int(sr * 0.2), sr, 220.0);
            const double early = rms(0.1, 0.2), late = rms(2.5, 2.9), afterMute = rms(4.0, 4.5);
            std::printf("%s, %s: quantized to %.2f Hz; level at 0.1 s %.3f, held at 2.5 s %.3f, after the mute %.4f\n",
                        mode ? "reshape " : "tracking", env ? "ADSR  " : "follow", hz, early, late, afterMute);
            check(std::fabs(cents(hz, 220.0)) < 2.0, "quantize");
            check(late > 0.5 * early && late > 0.01, "hold keeps the note after the string died");
            check(afterMute < 0.02 * late, "mute ends a held note");
        }
    {
        const int N = int(sr * 2);
        std::vector<float> in(N), oL(N), oR(N);
        for (int i = 0; i < N; ++i) in[i] = float(0.3 * std::sin(2 * M_PI * 220.0 * i / sr));
        float p[kInputCount]; presetDefaults(p); setVoices(p, 1); setAll(p, vWave, float(wSine)); p[gFltOn] = 0.f; p[gSpDetune] = 0.f;
        p[gVibDepth] = 50.f; p[gVibRate] = 5.f;
        Engine e; e.setSampleRate(sr); float outp[kOutputCount];
        e.process(in.data(), in.data(), oL.data(), oR.data(), N, p, outp);
        double lo = 1e9, hi = 0;
        for (double t = 1.0; t < 1.4; t += 0.01) { const double f = measureHz(&oL[int(t * sr)], int(sr * 0.03), sr, 220.0); lo = std::min(lo, f); hi = std::max(hi, f); }
        const double startHz = measureHz(&oL[int(0.05 * sr)], int(sr * 0.08), sr, 220.0);
        std::printf("vibrato 50 ct: pitch swings %.0f..%+.0f ct once it has come in; first 100 ms %+.1f ct\n", cents(lo, 220.0), cents(hi, 220.0), cents(startHz, 220.0));
        check(cents(hi, 220.0) > 30 && cents(lo, 220.0) < -30 && std::fabs(cents(startHz, 220.0)) < 3, "vibrato");
    }

    std::printf(fails == 0 ? "ALL OK\n" : "%d CHECK(S) FAILED\n", fails);
    return fails;
}
