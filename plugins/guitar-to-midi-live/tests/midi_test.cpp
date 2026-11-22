// Offline checks of the MIDI side: notes, re-picks, velocity, pitch bend, note steps, legato order, CC, no stuck notes.
//   g++ -O2 -std=c++17 tests/midi_test.cpp -o midi_test && ./midi_test
#include "../src/MidiCore.hpp"
#include <cstdio>
#include <random>
#include <vector>
using namespace swmidi;

static std::mt19937 rng(11);
static const double sr = 48000.0;

static void pluck(std::vector<float>& out, int at, int len, double f, double amp)
{
    std::uniform_real_distribution<double> u(-1.0, 1.0);
    for (int i = 0; i < len && at + i < int(out.size()); ++i)
    {
        const double t = i / sr;
        double s = 0.0;
        for (int h = 1; h <= 10; ++h)
        {
            const double fh = f * h;
            if (fh > sr * 0.45) break;
            s += (h == 1 ? 1.0 : 0.7 / h) * std::exp(-t * (1.0 + 0.8 * h)) * std::sin(2 * M_PI * fh * t + 0.5 * h);
        }
        s += 0.4 * u(rng) * std::exp(-t / 0.002);
        out[at + i] += float(amp * std::min(1.0, t / 0.0005) * s);
    }
}

struct Ev { double t; int type, ch, a, b; };   // type: 0x80 / 0x90 / 0xB0 / 0xE0; for bend a = 14-bit value

static std::vector<Ev> run(const std::vector<float>& in, const float* p)
{
    MidiCore c; c.setSampleRate(sr);
    std::vector<float> oL(in.size()), oR(in.size());
    std::vector<Ev> ev;
    const int blk = 256;   // a typical host buffer
    for (size_t o = 0; o < in.size(); o += blk)
    {
        const uint32_t n = uint32_t(std::min<size_t>(blk, in.size() - o));
        c.process(&in[o], &in[o], &oL[o], &oR[o], n, p);
        for (int k = 0; k < c.count(); ++k)
        {
            const Msg& m = c.msg(k);
            Ev e { (o + m.frame) / sr, m.data[0] & 0xF0, m.data[0] & 0x0F, m.data[1], m.data[2] };
            if (e.type == 0x90 && e.b == 0) e.type = 0x80;
            if (e.type == 0xE0) e.a = m.data[1] | (m.data[2] << 7);
            ev.push_back(e);
        }
    }
    return ev;
}

static int midiOf(double f) { return int(std::lround(69 + 12 * std::log2(f / 440.0))); }

int main()
{
    int fails = 0;
    const auto check = [&fails](bool ok, const char* what) { std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++fails; };
    const auto ons = [](const std::vector<Ev>& ev) { std::vector<Ev> r; for (const Ev& e : ev) if (e.type == 0x90) r.push_back(e); return r; };
    const auto stuck = [](const std::vector<Ev>& ev) {
        int held[16][128] = {};
        for (const Ev& e : ev) { if (e.type == 0x90) ++held[e.ch][e.a]; else if (e.type == 0x80 && held[e.ch][e.a] > 0) --held[e.ch][e.a]; }
        int n = 0; for (auto& c : held) for (int v : c) n += v; return n;
    };

    // ---- 1. a line of picked notes, one soft pick, one note picked twice; transposed and on channel 3
    {
        std::vector<float> in(int(sr * 3.4), 0.f);
        const double fs[] = { 110.0, 164.81, 220.0, 220.0, 329.63 };
        const double amps[] = { 0.12, 0.12, 0.12, 0.12, 0.05 };   // pick peaks around -15 dBFS, the last one -8 dB softer
        for (int k = 0; k < 5; ++k) pluck(in, int(sr * (0.05 + 0.6 * k)), int(sr * 0.55), fs[k], amps[k]);
        float p[kInputCount]; presetDefaults(p); p[pChannel] = 2.f; p[pOctave] = -1.f; p[pSemi] = 7.f;
        const std::vector<Ev> ev = run(in, p);
        const std::vector<Ev> on = ons(ev);
        std::printf("notes:");
        for (const Ev& e : on) std::printf(" %d(ch%d vel %d @%.0fms)", e.a, e.ch + 1, e.b, e.t * 1000);
        std::printf("\n");
        bool right = on.size() == 5;
        for (int k = 0; right && k < 5; ++k) right = on[k].a == midiOf(fs[k]) - 5 && on[k].ch == 2;
        check(right, "five note-ons, right numbers (transposed -1 oct +7 st), channel 3, the repeated A3 played twice");
        check(right && on[4].b < on[0].b - 20, "soft pick gives a clearly lower velocity");
        check(stuck(ev) == 0, "every note-on has its note-off");
    }

    // ---- 2. a held tone bent up a whole tone and back: pitch bend / note steps / ignore
    std::vector<float> bendIn(int(sr * 2.2), 0.f);
    {
        double ph = 0;
        for (size_t i = 0; i < bendIn.size(); ++i)
        {
            const double t = i / sr;
            double st = 0;
            if (t > 0.5 && t < 0.8) st = 2.0 * (t - 0.5) / 0.3;
            else if (t >= 0.8 && t < 1.2) st = 2.0;
            else if (t >= 1.2 && t < 1.5) st = 2.0 * (1.5 - t) / 0.3;
            ph += 196.0 * std::pow(2.0, st / 12.0) / sr;
            const double env = t < 1.9 ? 0.3 * std::min(1.0, t / 0.002) : 0.3 * std::exp(-(t - 1.9) / 0.003);
            bendIn[i] = float(env * (std::sin(2 * M_PI * ph) + 0.4 * std::sin(4 * M_PI * ph) + 0.2 * std::sin(6 * M_PI * ph)));
        }
    }
    {
        float p[kInputCount]; presetDefaults(p);
        const std::vector<Ev> ev = run(bendIn, p);
        int maxB = 0, nb = 0, endB = 0; double gaps = 0; double lastT = -1;
        for (const Ev& e : ev) if (e.type == 0xE0) { maxB = std::max(maxB, e.a); ++nb; if (e.t < 1.85) endB = e.a; if (lastT >= 0) gaps = std::max(gaps, e.t - lastT); lastT = e.t; }
        const double maxSemis = (maxB - 8192) / 8191.0 * 2.0, endSemis = (endB - 8192) / 8191.0 * 2.0;
        std::printf("pitch bend (range 2): %zu notes, %d bend messages, peak %+.2f st, back to %+.2f st\n", ons(ev).size(), nb, maxSemis, endSemis);
        check(ons(ev).size() == 1 && std::fabs(maxSemis - 2.0) < 0.1 && std::fabs(endSemis) < 0.1, "whole-tone bend comes out as one note and pitch bend to +2 and back");
        check(stuck(ev) == 0, "no stuck note after the bend");

        p[pBendRange] = 6.f;   // +/- 12
        const std::vector<Ev> ev12 = run(bendIn, p);
        int max12 = 0; for (const Ev& e : ev12) if (e.type == 0xE0) max12 = std::max(max12, e.a);
        std::printf("pitch bend (range 12): peak value %d = %+.2f st\n", max12, (max12 - 8192) / 8191.0 * 12.0);
        check(std::fabs((max12 - 8192) / 8191.0 * 12.0 - 2.0) < 0.1, "bend range 12 scales the same bend correctly");

        p[pBends] = float(bSteps); p[pBendRange] = 1.f;
        const std::vector<Ev> st = run(bendIn, p);
        std::printf("note steps:");
        for (const Ev& e : ons(st)) std::printf(" %d", e.a);
        int nbs = 0; for (const Ev& e : st) if (e.type == 0xE0) ++nbs;
        std::printf("  (bend messages: %d)\n", nbs);
        const std::vector<Ev> so = ons(st);
        check(so.size() == 5 && so[0].a == 55 && so[1].a == 56 && so[2].a == 57 && so[3].a == 56 && so[4].a == 55, "note steps: G3 -> G#3 -> A3 -> G#3 -> G3");
        check(stuck(st) == 0, "no stuck note with steps");

        p[pBends] = float(bOff);
        const std::vector<Ev> ig = run(bendIn, p);
        int nbi = 0; for (const Ev& e : ig) if (e.type == 0xE0) ++nbi;
        check(ons(ig).size() == 1 && nbi == 0, "ignore: one note, no pitch bend");
    }

    // ---- 3. legato order: hammer-on without a new pick
    {
        std::vector<float> in(int(sr * 1.2), 0.f);
        double ph = 0;
        for (size_t i = 0; i < in.size(); ++i)
        {
            const double t = i / sr;
            ph += (t < 0.5 ? 220.0 : 246.94) / sr;
            const double env = t < 1.0 ? 0.3 * std::min(1.0, t / 0.002) * std::exp(-t * 0.5) : 0.0;
            in[i] = float(env * (std::sin(2 * M_PI * ph) + 0.3 * std::sin(4 * M_PI * ph)));
        }
        for (int mode = 0; mode < 2; ++mode)
        {
            float p[kInputCount]; presetDefaults(p); p[pLegato] = float(mode);
            const std::vector<Ev> ev = run(in, p);
            std::printf("%s:", kLegatoNames[mode]);
            for (const Ev& e : ev) if (e.type == 0x80 || e.type == 0x90) std::printf(" %s%d@%.0fms", e.type == 0x90 ? "on" : "off", e.a, e.t * 1000);
            std::printf("\n");
            // find the change: the event pair around the second note-on
            int idx = -1, k = 0;
            for (size_t j = 0; j < ev.size(); ++j) if (ev[j].type == 0x90 && ++k == 2) idx = int(j);
            bool okOrder = idx > 0;
            if (okOrder && mode == lRetrigger) okOrder = ev[idx - 1].type == 0x80 && ev[idx - 1].a == 57 && ev[idx].a == 59;
            if (okOrder && mode == lOverlap) okOrder = idx + 1 < int(ev.size()) && ev[idx + 1].type == 0x80 && ev[idx + 1].a == 57 && ev[idx].a == 59;
            check(okOrder, mode == lRetrigger ? "retrigger: off A3 before on B3" : "overlap: on B3 before off A3 (lets a mono synth glide)");
            check(stuck(ev) == 0, "no stuck note");
        }
    }

    // ---- 4. expression CC follows the level
    {
        std::vector<float> in(int(sr * 2.0), 0.f);
        pluck(in, int(sr * 0.05), int(sr * 1.9), 146.83, 0.3);
        float p[kInputCount]; presetDefaults(p); p[pExpr] = 4.f;   // CC 11
        const std::vector<Ev> ev = run(in, p);
        int n = 0, first = -1, last = -1, wrong = 0;
        for (const Ev& e : ev) if (e.type == 0xB0) { if (e.a != 11) ++wrong; ++n; if (first < 0 && e.t > 0.1) first = e.b; last = e.b; }
        std::printf("CC 11: %d messages, value at the attack %d, at the end %d\n", n, first, last);
        check(n > 20 && wrong == 0 && first > 90 && last < first - 60, "expression CC 11 falls with the decaying string");
    }

    // ---- 5. rapid host buffer sizes and sample rates: same notes
    {
        std::vector<float> in(int(sr * 2.0), 0.f);
        for (int k = 0; k < 6; ++k) pluck(in, int(sr * (0.05 + 0.3 * k)), int(sr * 0.28), 110.0 * std::pow(2.0, k * 3 / 12.0), 0.25);
        float p[kInputCount]; presetDefaults(p);
        const std::vector<Ev> a = ons(run(in, p));
        MidiCore c; c.setSampleRate(sr);
        std::vector<float> oL(in.size()), oR(in.size());
        std::vector<int> b;
        size_t o = 0; int sizes[] = { 1, 7, 64, 512, 33, 2048 }; int si = 0;
        while (o < in.size())
        {
            const uint32_t n = uint32_t(std::min<size_t>(sizes[si++ % 6], in.size() - o));
            c.process(&in[o], &in[o], &oL[o], &oR[o], n, p);
            for (int k = 0; k < c.count(); ++k) if ((c.msg(k).data[0] & 0xF0) == 0x90) b.push_back(c.msg(k).data[1]);
            o += n;
        }
        bool same = a.size() == b.size() && a.size() == 6;
        for (size_t k = 0; same && k < a.size(); ++k) same = a[k].a == b[k];
        check(same, "uneven host buffer sizes (1..2048 frames) give the same notes");
    }

    std::printf(fails == 0 ? "ALL OK\n" : "%d CHECK(S) FAILED\n", fails);
    return fails;
}
