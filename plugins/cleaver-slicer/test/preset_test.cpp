// Round-trip test of the user preset file (run natively and under wine).
#include "UserPresets.hpp"
using namespace cleaver;
int main() {
    std::vector<UserPreset> a, b;
    for (int k = 0; k < 3; ++k) {
        UserPreset u; u.name = k == 0 ? "Zażółć gęślą jaźń" : k == 1 ? "Riff #2" : "x";
        for (int i = 0; i < kNumInputParams; ++i) u.par[i] = kParams[i].min + (kParams[i].max - kParams[i].min) * (0.25f * (k + 1));
        buildPresetPattern(kPresets[10 + k * 7], u.pat); a.push_back(u);
    }
    if (!saveUserPresets(a)) { printf("SAVE FAILED %s\n", presetFilePath().c_str()); return 1; }
    loadUserPresets(b);
    int bad = b.size() != a.size();
    for (size_t k = 0; k < b.size() && !bad; ++k) {
        if (a[k].name != b[k].name || encodePattern(a[k].pat) != encodePattern(b[k].pat)) bad = 1;
        for (int i = 0; i < kNumInputParams; ++i) if (presetStoresParam(i) && std::fabs(a[k].par[i] - b[k].par[i]) > 0.01f * (kParams[i].max - kParams[i].min)) bad = 1;
    }
    printf("%s  file=%s  presets=%d\n", bad ? "FAIL" : "ok", presetFilePath().c_str(), (int)b.size());
    return bad;
}
