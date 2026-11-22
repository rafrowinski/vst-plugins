// Runs Guitar to MIDI Live over a WAV file and writes what it would send as a standard MIDI file - handy for checking the
// tracking in a DAW piano roll, or for turning a recorded guitar part into MIDI afterwards.
//   g++ -O2 -std=c++17 tests/to_midi.cpp -o to_midi
//   ./to_midi in.wav out.mid [symbol value ...]      e.g.  ./to_midi solo.wav solo.mid bends 1 det_legato 5
#include "../src/MidiCore.hpp"
#include <cstdio>
#include <string>
#include <vector>
using namespace swmidi;

static bool readWav(const char* path, std::vector<float>& L, std::vector<float>& R, double& sr)
{
    FILE* f = std::fopen(path, "rb");
    if (!f) return false;
    std::vector<unsigned char> d;
    unsigned char buf[65536]; size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) d.insert(d.end(), buf, buf + n);
    std::fclose(f);
    const auto u32 = [&d](size_t o) { return uint32_t(d[o]) | (uint32_t(d[o + 1]) << 8) | (uint32_t(d[o + 2]) << 16) | (uint32_t(d[o + 3]) << 24); };
    const auto u16 = [&d](size_t o) { return uint32_t(d[o]) | (uint32_t(d[o + 1]) << 8); };
    if (d.size() < 44 || std::memcmp(d.data(), "RIFF", 4) || std::memcmp(d.data() + 8, "WAVE", 4)) return false;
    uint32_t fmt = 0, ch = 0, bits = 0; size_t off = 0, len = 0;
    for (size_t o = 12; o + 8 <= d.size();)
    {
        const uint32_t l = u32(o + 4);
        if (!std::memcmp(d.data() + o, "fmt ", 4)) { fmt = u16(o + 8); ch = u16(o + 10); sr = u32(o + 12); bits = u16(o + 22); if (fmt == 0xFFFE) fmt = u16(o + 32); }
        else if (!std::memcmp(d.data() + o, "data", 4)) { off = o + 8; len = std::min(size_t(l), d.size() - off); break; }
        o += 8 + size_t(l) + (l & 1);
    }
    const uint32_t bytes = bits / 8;
    if (!off || !ch || !bytes) return false;
    const size_t frames = len / (bytes * ch);
    L.resize(frames); R.resize(frames);
    for (size_t i = 0; i < frames; ++i)
        for (uint32_t c = 0; c < 2; ++c)
        {
            const size_t o = off + (i * ch + std::min(c, ch - 1)) * bytes;
            float v;
            if (fmt == 3) std::memcpy(&v, d.data() + o, 4);
            else if (bytes == 2) v = float(int16_t(u16(o))) / 32768.f;
            else if (bytes == 3) v = float(int32_t(u32(o - 1) & 0xFFFFFF00u) >> 8) / 8388608.f;
            else if (bytes == 4) v = float(int32_t(u32(o))) / 2147483648.f;
            else v = (float(d[o]) - 128.f) / 128.f;
            (c == 0 ? L : R)[i] = v;
        }
    return true;
}

int main(int argc, char** argv)
{
    if (argc < 3) { std::printf("usage: to_midi in.wav out.mid [symbol value ...]\n"); return 1; }
    std::vector<float> L, R; double sr = 0;
    if (!readWav(argv[1], L, R, sr)) { std::printf("could not read %s\n", argv[1]); return 1; }
    float p[kInputCount]; presetDefaults(p);
    for (int a = 3; a + 1 < argc; a += 2)
        for (int i = 0; i < kInputCount; ++i) { char s[32]; symbolOf(i, s, sizeof(s)); if (!std::strcmp(s, argv[a])) p[i] = float(std::atof(argv[a + 1])); }

    MidiCore c; c.setSampleRate(sr);
    std::vector<float> oL(L.size()), oR(L.size());
    struct E { uint64_t frame; Msg m; };
    std::vector<E> ev;
    FILE* lst = std::fopen((std::string(argv[2]) + ".txt").c_str(), "w");
    for (size_t o = 0; o < L.size(); o += 256)
    {
        const uint32_t n = uint32_t(std::min<size_t>(256, L.size() - o));
        c.process(&L[o], &R[o], &oL[o], &oR[o], n, p);
        for (int k = 0; k < c.count(); ++k)
        {
            ev.push_back({ o + c.msg(k).frame, c.msg(k) });
            if (lst && (c.msg(k).data[0] & 0xF0) == 0x90)
                std::fprintf(lst, "%.4f %d %.1f %d\n", (o + c.msg(k).frame) / sr, c.msg(k).data[1], c.out[oLatency - kInputCount], c.msg(k).data[2]);
        }
    }
    if (lst) std::fclose(lst);

    // standard MIDI file, format 0, 120 bpm, 960 ticks per beat (1 tick = 1/1920 s)
    std::vector<unsigned char> trk;
    const auto vlq = [&trk](uint32_t v) { unsigned char b[5]; int n = 0; b[n++] = v & 127; while (v >>= 7) b[n++] = 0x80 | (v & 127); while (n) trk.push_back(b[--n]); };
    vlq(0); trk.insert(trk.end(), { 0xFF, 0x51, 0x03, 0x07, 0xA1, 0x20 });   // 500000 us per beat
    uint64_t last = 0;
    int notes = 0;
    for (const E& e : ev)
    {
        const uint64_t tick = uint64_t(double(e.frame) / sr * 1920.0 + 0.5);
        vlq(uint32_t(tick - last)); last = tick;
        trk.insert(trk.end(), e.m.data, e.m.data + e.m.size);
        if ((e.m.data[0] & 0xF0) == 0x90) ++notes;
    }
    vlq(0); trk.insert(trk.end(), { 0xFF, 0x2F, 0x00 });
    FILE* f = std::fopen(argv[2], "wb");
    if (!f) { std::printf("could not write %s\n", argv[2]); return 1; }
    const unsigned char hdr[] = { 'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x03, 0xC0, 'M', 'T', 'r', 'k' };
    std::fwrite(hdr, 1, sizeof(hdr), f);
    const uint32_t tl = uint32_t(trk.size());
    const unsigned char len[] = { (unsigned char)(tl >> 24), (unsigned char)(tl >> 16), (unsigned char)(tl >> 8), (unsigned char)tl };
    std::fwrite(len, 1, 4, f);
    std::fwrite(trk.data(), 1, trk.size(), f);
    std::fclose(f);
    std::printf("%s: %d notes, %zu MIDI messages -> %s\n", argv[1], notes, ev.size(), argv[2]);
    return 0;
}
