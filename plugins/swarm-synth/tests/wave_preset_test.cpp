// Checks of the custom wave (points, formula, WAV cycle), its band-limited copies and the preset CSV.
//   g++ -O2 -std=c++17 tests/wave_preset_test.cpp -o wave_preset_test && ./wave_preset_test
#include "../../../shared/guitar-engine/Engine.hpp"
#include "../src/PresetIO.hpp"
#include <cstdio>
using namespace swsyn;

int main()
{
    int fails = 0;
    const auto check = [&fails](bool ok, const char* what) { std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++fails; };
    std::vector<float> t(kWaveSize);
    std::string err;

    check(compileWave(kDefaultWave, t.data()) && std::fabs(t[kWaveSize / 4] - 1.f) < 0.01f && std::fabs(t[3 * kWaveSize / 4]) < 0.01f, "default wave peaks at 1/4 and dips at 3/4");
    check(compileWave("f:sin(2*pi*x)", t.data()) && std::fabs(t[0] - 0.5f) < 0.01f && std::fabs(t[kWaveSize / 4] - 1.f) < 0.01f, "formula sine");
    check(!compileWave("f:sin(2*pi*x", t.data(), &err) && !err.empty(), "broken formula is rejected with a message");
    check(compileWave(kOrganWave, t.data()) && compileWave(kFormantWave, t.data()) && compileWave(kStairWave, t.data()), "factory waves compile");

    // band-limited copies of a saw: level 0 has all harmonics, the last level is a pure sine
    {
        compileWave("l:0,0;0.999,1", t.data());
        std::vector<WaveSet> ws(1);
        buildWaveSet(t.data(), ws[0]);
        float pk0 = 0, dev = 0;
        double a = 0, b = 0;
        const float* const top = ws[0].t[kMips - 1];
        for (int i = 0; i < kWaveSize; ++i)
        {
            pk0 = std::max(pk0, std::fabs(ws[0].t[0][i]));
            a += top[i] * std::sin(2 * M_PI * i / kWaveSize); b += top[i] * std::cos(2 * M_PI * i / kWaveSize);
        }
        a *= 2.0 / kWaveSize; b *= 2.0 / kWaveSize;
        for (int i = 0; i < kWaveSize; ++i)
            dev = std::max(dev, float(std::fabs(top[i] - a * std::sin(2 * M_PI * i / kWaveSize) - b * std::cos(2 * M_PI * i / kWaveSize))));
        std::printf("     fundamental of the saw: amplitude %.3f (expect %.3f), rest %.5f\n", std::hypot(a, b), 2 / M_PI, dev);
        check(std::fabs(pk0 - 1.f) < 1e-3f, "wave set is normalised to +-1");
        check(dev < 1e-3f && std::fabs(std::hypot(a, b) - 2 / M_PI) < 0.01, "highest level of the saw is its fundamental only");
    }

    // WAV: write a 600-sample 16-bit single cycle (two harmonics), load it, compare
    {
        const int n = 600;
        std::vector<unsigned char> d(44 + n * 2, 0);
        const auto w32 = [&d](int o, uint32_t v) { d[o] = v & 255; d[o + 1] = (v >> 8) & 255; d[o + 2] = (v >> 16) & 255; d[o + 3] = (v >> 24) & 255; };
        const auto w16 = [&d](int o, uint32_t v) { d[o] = v & 255; d[o + 1] = (v >> 8) & 255; };
        std::memcpy(&d[0], "RIFF", 4); w32(4, 36 + n * 2); std::memcpy(&d[8], "WAVEfmt ", 8); w32(16, 16); w16(20, 1); w16(22, 1);
        w32(24, 44100); w32(28, 88200); w16(32, 2); w16(34, 16); std::memcpy(&d[36], "data", 4); w32(40, n * 2);
        for (int i = 0; i < n; ++i)
        {
            const double v = 0.6 * std::sin(2 * M_PI * i / n) + 0.3 * std::sin(6 * M_PI * i / n);
            w16(44 + 2 * i, uint32_t(int(std::lround(v * 32767.0)) & 0xFFFF));
        }
        FILE* f = std::fopen("/tmp/swsyn_cycle.wav", "wb"); std::fwrite(d.data(), 1, d.size(), f); std::fclose(f);
        std::string text;
        const bool ok = loadWavCycle("/tmp/swsyn_cycle.wav", text, err);
        check(ok && text.size() < 900, "WAV cycle loads into a short text");
        float worst = 0;
        double peak = 0;
        for (int i = 0; i < n; ++i) peak = std::max(peak, std::fabs(0.6 * std::sin(2 * M_PI * i / n) + 0.3 * std::sin(6 * M_PI * i / n)));
        if (ok && compileWave(text.c_str(), t.data()))
            for (int i = 0; i < kWaveSize; ++i)
            {
                const double x = double(i) / kWaveSize;
                const double want = 0.5 + 0.5 * (0.6 * std::sin(2 * M_PI * (x + 0.5 / kWavSamples)) + 0.3 * std::sin(6 * M_PI * (x + 0.5 / kWavSamples))) / peak;
                worst = std::max(worst, float(std::fabs(t[i] - want)));
            }
        else worst = 9;
        std::printf("     worst difference to the original cycle: %.4f\n", worst);
        check(worst < 0.02f, "loaded cycle matches the file");
        check(!loadWavCycle("/tmp/does-not-exist.wav", text, err), "missing file is reported");
    }

    // presets: CSV round trip incl. a WAV wave and a name with separators; duplicates
    {
        std::vector<UserPreset> out, in;
        for (int i = 0; i < kPresetCount; ++i) { UserPreset p; p.name = kPresets[i].name; kPresets[i].fill(p.v); p.wave = presetWave(i); out.push_back(p); }
        UserPreset odd; odd.name = "Zażółć; \"gęślą\", jaźń"; presetDefaults(odd.v); odd.v[gCutoff] = 1234.5f;
        float cyc[kWavSamples]; for (int i = 0; i < kWavSamples; ++i) cyc[i] = 0.5f + 0.5f * float(std::sin(2 * M_PI * i / kWavSamples));
        odd.wave = "w:" + encodeSamples(cyc); out.push_back(odd);
        check(writePresets("/tmp/swsyn_presets.csv", out) && readPresets("/tmp/swsyn_presets.csv", in) && in.size() == out.size(), "CSV written and read back");
        bool same = in.size() == out.size();
        for (size_t i = 0; same && i < in.size(); ++i) same = in[i].name == out[i].name && sameValues(in[i].v, out[i].v) && in[i].wave == out[i].wave;
        check(same, "all presets identical after the round trip");
        Library lib; ImportReport rep;
        for (const UserPreset& p : in) lib.addImported(p, rep);
        check(rep.added == 1 && rep.skipped == kPresetCount, "factory duplicates skipped, the new one added");
        UserPreset ch = odd; ch.v[gMix] = 12.f; lib.addImported(ch, rep);
        check(lib.user.size() == 2 && lib.user[1].name == odd.name + " (2)", "same name with other settings is added as (2)");
    }

    std::printf(fails == 0 ? "ALL OK\n" : "%d CHECK(S) FAILED\n", fails);
    return fails;
}
