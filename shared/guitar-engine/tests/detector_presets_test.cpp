// Saved detector settings: store / replace / delete, CSV export and import (other separators, decimal comma,
// missing columns, a full Swarm Synth preset file), duplicates and built-in names, two plugins sharing the file.
//   g++ -O2 -std=c++17 tests/detector_presets_test.cpp -o detector_presets_test && ./detector_presets_test
#include "../DetectorPresets.hpp"
#include <cstdio>
using namespace detp;

int main()
{
    int fails = 0;
    const auto check = [&fails](bool ok, const char* what) { std::printf("%s %s\n", ok ? "ok  " : "FAIL", what); if (!ok) ++fails; };
    const char* const lib = "/tmp/detp_lib.csv";
    std::remove(lib);

    const float a[kCount] = { -50.f, -35.f, 1.f, 75.f, 8.f, 12.f, 25.f, 40.f, 1.35f, 25.f, 25.f, 1.f };
    const float b[kCount] = { -46.f, -28.f, 1.5f, 85.f, 25.f, 45.f, 60.f, 60.f, 1.6f, 50.f, 35.f, 0.f };

    // two "plugins" sharing one file
    Library synth({ "Fast solo", "Balanced (default)", "Careful / let ring" }), midi({ "Fast solo" });
    synth.path = lib; midi.path = lib;
    check(synth.store("Tele bridge", a), "store a setting");
    midi.load();
    check(midi.find("Tele bridge") >= 0 && sameValues(midi.user[0].v, a), "the other plugin sees it after loading");
    check(midi.store("Strat neck", b), "the other plugin stores its own");
    check(synth.store("Tele bridge", b) && synth.user.size() == 2, "storing re-reads the file, so nothing from the other plugin is lost");
    synth.load();
    check(sameValues(synth.user[synth.find("Tele bridge")].v, b), "the same name replaces the values");
    check(synth.match(b) >= 0 && synth.match(a) < 0, "match finds a setting by its values");
    check(synth.remove("Strat neck") && synth.find("Strat neck") < 0, "delete");
    midi.load();
    check(midi.user.size() == 1, "deleted for the other plugin too");

    // export / import round trip, including a name with separators and quotes
    std::vector<DetPreset> out(2);
    out[0].name = "Gęsty; \"riff\", drop D"; std::memcpy(out[0].v, a, sizeof(a));
    out[1].name = "Tele bridge";             std::memcpy(out[1].v, b, sizeof(b));
    check(writePresets("/tmp/detp_export.csv", out), "export");
    Library imp({ "Fast solo" }); imp.path = "/tmp/detp_imp.csv"; std::remove(imp.path.c_str());
    Report rep;
    check(imp.importFile("/tmp/detp_export.csv", a, rep) && rep.added == 2 && imp.user[0].name == out[0].name && sameValues(imp.user[0].v, a),
          "import back: names and values intact");
    Report rep2;
    imp.importFile("/tmp/detp_export.csv", a, rep2);
    check(rep2.added == 0 && rep2.skipped == 2, "importing the same file again skips both");

    // a file written by Excel: comma decimals, TAB separator, other column order, some columns missing,
    // one row with a name of a built-in profile and one unreadable row
    {
        FILE* f = std::fopen("/tmp/detp_excel.csv", "wb");
        std::fputs("name\tdet_legato\tgate\ttrack\r\nExcel one\t7,5\t-60\t1,25\r\nFast solo\t30\t-40\t2\r\nbroken\tx\ty\tz\r\n", f);
        std::fclose(f);
        Library x({ "Fast solo" }); x.path = "/tmp/detp_x.csv"; std::remove(x.path.c_str());
        Report r;
        const bool ok = x.importFile("/tmp/detp_excel.csv", a, r);
        const int i = x.find("Excel one");
        check(ok && i >= 0 && x.user[i].v[4] == 7.5f && x.user[i].v[0] == -60.f && x.user[i].v[2] == 1.25f && x.user[i].v[3] == a[3],
              "Excel-style file: TAB, decimal comma, any order; missing columns keep the current values");
        check(x.find("Fast solo") < 0 && x.find("Fast solo (2)") >= 0 && r.renamed == 1, "a built-in profile's name gets \"(2)\"");
        check(r.badRows == 1, "the unreadable row is reported");
    }

    // a full Swarm Synth preset file: only its detector columns are taken, values out of range are clamped
    {
        FILE* f = std::fopen("/tmp/detp_synth.csv", "wb");
        std::fputs("preset;mode;mix;gate;cut;track;v1_on;det_clarity;det_bridge;wave\r\nMy synth preset;0;100;-90;-30;1.0;1;80;0.3;n:0,0.5\r\n", f);
        std::fclose(f);
        Library y; y.path = "/tmp/detp_y.csv"; std::remove(y.path.c_str());
        Report r;
        const bool ok = y.importFile("/tmp/detp_synth.csv", a, r);
        check(ok && r.added == 1 && y.user[0].v[0] == -80.f && y.user[0].v[11] == 0.f && y.user[0].v[4] == a[4],
              "a Swarm Synth preset file: detector columns used, -90 dB clamped to -80, bridge 0.3 rounded to off");
    }

    // files without any detector column are refused
    {
        FILE* f = std::fopen("/tmp/detp_none.csv", "wb");
        std::fputs("name;foo;bar\r\nx;1;2\r\n", f);
        std::fclose(f);
        Library z; z.path = "/tmp/detp_z.csv";
        Report r;
        check(!z.importFile("/tmp/detp_none.csv", a, r), "a CSV with no detector columns is refused");
    }

    std::printf(fails == 0 ? "ALL OK\n" : "%d CHECK(S) FAILED\n", fails);
    return fails;
}
