// Swarm Synth - user interface (Dear ImGui)
#include "PresetIO.hpp"
#include "../../../shared/guitar-engine/DetectorPresets.hpp"

#include "DistrhoUI.hpp"
#include "DearImGui/imgui_internal.h"

#include <chrono>
#include <random>

START_NAMESPACE_DISTRHO

using namespace swsyn;

static const ImU32 kVoiceColors[kNumVoices] = {
    IM_COL32(230, 159, 0, 255),    // orange
    IM_COL32(86, 180, 233, 255),   // sky blue
    IM_COL32(0, 200, 150, 255),    // green
    IM_COL32(240, 228, 66, 255),   // yellow
    IM_COL32(245, 95, 75, 255),    // red
    IM_COL32(214, 130, 190, 255),  // purple
    IM_COL32(150, 220, 90, 255),   // lime
    IM_COL32(170, 160, 255, 255),  // lavender
};
static const ImU32 kColBg     = IM_COL32(22, 24, 28, 255);
static const ImU32 kColPanel  = IM_COL32(34, 37, 44, 255);
static const ImU32 kColGraph  = IM_COL32(16, 17, 20, 255);
static const ImU32 kColTrack  = IM_COL32(70, 75, 86, 255);
static const ImU32 kColText   = IM_COL32(226, 228, 233, 255);
static const ImU32 kColDim    = IM_COL32(140, 146, 158, 255);
static const ImU32 kColAccent = IM_COL32(120, 200, 255, 255);
static const ImU32 kColGrid   = IM_COL32(48, 52, 60, 255);

static ImU32 withAlpha(ImU32 c, float a)
{
    return (c & 0x00FFFFFFu) | (ImU32(a * 255.f) << 24);
}

class SwarmSynthUI : public UI
{
    float fP[kParamCount];
    float fScale = 1.f;
    int fSel = -1; // -1 = edit all voices together

    Library fLib;
    std::string fCurName;
    bool fMatchDirty = true;
    std::string fStatus;
    double fStatusUntil = 0.0;
    enum { kBrowseNone, kBrowseExportCurrent, kBrowseExportAll, kBrowseImport, kBrowseWav,
           kBrowseDetExportCurrent, kBrowseDetExportAll, kBrowseDetImport };
    int fBrowse = kBrowseNone;
    // saved detector settings
    static constexpr int kDetIdx[detp::kCount] = { gGate, gCut, gTrack, gDetClarity, gDetLegato, gDetLow, gDetHarm, gDetProbation, gDetRepick, gDetRefract, gDetTail, gDetBridge };
    detp::Library fDetLib { detReservedNames() };
    char fDetName[64] = "";
    std::string fDetStatus, fDetDeleteName;
    bool fDetOpenSave = false;
    char fNameBuf[64] = "";
    bool fOpenSave = false;
    float fDragNorm = 0.f;

    // custom wave: its text form, the table compiled from it, and the editor's working state
    std::string fWave = kDefaultWave;
    float fWaveTable[kWaveSize];
    bool fOpenWave = false, fOpenDet = false;
    std::vector<WaveNode> fNodes;
    bool fNodesSmooth = true;
    int fNodeDrag = -1;
    char fFormulaBuf[200] = "";
    std::string fFormulaErr;

    // A/B compare
    std::string fABWave[2];
    float fAB[2][kInputCount];
    bool fABValid[2] = { false, false };
    int fABCur = 0;

    // tuner readout holds the last note for a moment
    float fShownHz = 0.f;
    double fShownUntil = 0.0;

    std::mt19937 fRng { uint32_t(std::chrono::steady_clock::now().time_since_epoch().count()) };

    // interface language for explanations: English by default, Polish on request (remembered between sessions)
    bool fLangPl = false;
    const char* T(const char* en, const char* pl) const { return fLangPl ? pl : en; }

    static std::string settingsPath()
    {
        std::string pth = libraryPath();
        const size_t cut = pth.find("user-presets.csv");
        if (pth.empty() || cut == std::string::npos) return std::string();
        return pth.substr(0, cut) + "settings.txt";
    }
    void loadSettings()
    {
        const std::string pth = settingsPath();
        if (pth.empty()) return;
        if (FILE* const f = openUtf8(pth.c_str(), "rb"))
        {
            char buf[64] = "";
            const size_t n = std::fread(buf, 1, sizeof(buf) - 1, f);
            buf[n] = 0;
            std::fclose(f);
            fLangPl = std::strstr(buf, "lang=pl") != nullptr;
        }
    }
    void saveSettings() const
    {
        const std::string pth = settingsPath();
        if (pth.empty()) return;
        if (FILE* const f = openUtf8(pth.c_str(), "wb"))
        {
            std::fputs(fLangPl ? "lang=pl\n" : "lang=en\n", f);
            std::fclose(f);
        }
    }

    float S(float x) const { return x * fScale; }

public:
    SwarmSynthUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        fScale = float(getScaleFactor());
        if (fScale > 1.01f)
            setSize(uint(DISTRHO_UI_DEFAULT_WIDTH * fScale), uint(DISTRHO_UI_DEFAULT_HEIGHT * fScale));
        setFontSize(14.f);

        presetDefaults(fP);
        for (int i = kInputCount; i < kParamCount; ++i) fP[i] = 0.f;
        compileWave(kDefaultWave, fWaveTable);
        syncEditor();
        fLib.load();
        loadSettings();
        fDetLib.load();

        ImGuiStyle& st = ImGui::GetStyle();
        st.FrameRounding = S(4); st.PopupRounding = S(4); st.GrabRounding = S(4);
        st.Colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.22f, 0.26f, 1.f);
        st.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.26f, 0.29f, 0.34f, 1.f);
        st.Colors[ImGuiCol_Button] = ImVec4(0.22f, 0.25f, 0.30f, 1.f);
        st.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.34f, 0.41f, 1.f);
        st.Colors[ImGuiCol_ButtonActive] = ImVec4(0.36f, 0.42f, 0.52f, 1.f);
        st.Colors[ImGuiCol_CheckMark] = ImVec4(0.47f, 0.78f, 1.f, 1.f);
        st.Colors[ImGuiCol_PopupBg] = ImVec4(0.12f, 0.13f, 0.16f, 0.98f);
        st.Colors[ImGuiCol_Header] = ImVec4(0.25f, 0.33f, 0.45f, 1.f);
        st.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.40f, 0.55f, 1.f);
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index >= kParamCount) return;
        if (fP[index] == value) return;
        fP[index] = value;
        if (index < kInputCount) fMatchDirty = true;
        repaint();
    }

    // the host (project load) or the plugin tells us the current custom wave
    void stateChanged(const char* key, const char* value) override
    {
        if (std::strcmp(key, "wave") != 0 || value == nullptr) return;
        std::vector<float> table(kWaveSize);
        if (!compileWave(value, table.data())) return;
        std::memcpy(fWaveTable, table.data(), sizeof(fWaveTable));
        fWave = value;
        fMatchDirty = true;
        if (fNodeDrag < 0) syncEditor();
        repaint();
    }

    // compiles the text into the lookup table and hands it to the audio side; false if the text is invalid
    bool setWave(const std::string& text)
    {
        std::vector<float> table(kWaveSize);
        std::string err;
        if (!compileWave(text.c_str(), table.data(), &err)) { fFormulaErr = err; return false; }
        std::memcpy(fWaveTable, table.data(), sizeof(fWaveTable));
        fWave = text;
        fFormulaErr.clear();
        setState("wave", text.c_str());
        fMatchDirty = true;
        return true;
    }

    void syncEditor()
    {
        if (fWave.size() > 2 && fWave[0] == 'f')
            std::snprintf(fFormulaBuf, sizeof(fFormulaBuf), "%s", fWave.c_str() + 2);
        else if (fWave.size() > 2 && (fWave[0] == 'n' || fWave[0] == 'l'))
        {
            parseNodes(fWave.c_str() + 2, fNodes);
            fNodesSmooth = fWave[0] == 'n';
        }
    }

    void applyPreset(const float* v, const std::string& wave)
    {
        applyValues(v);
        if (wave != fWave && setWave(wave)) syncEditor();
    }

    // ----------------------------------------------------------------------------------------------------------------
    // parameter helpers

    int targets(int idx, int* out) const
    {
        if (isVoiceParam(idx) && fSel < 0)
        {
            const int p = (idx - kGlobalCount) % kVoiceParamCount;
            for (int v = 0; v < kNumVoices; ++v) out[v] = voiceIndex(v, p);
            return kNumVoices;
        }
        out[0] = idx;
        return 1;
    }
    void beginEdit(int idx) { int t[kNumVoices]; const int n = targets(idx, t); for (int i = 0; i < n; ++i) editParameter(t[i], true); }
    void endEdit(int idx)   { int t[kNumVoices]; const int n = targets(idx, t); for (int i = 0; i < n; ++i) editParameter(t[i], false); }
    void setValue(int idx, float v)
    {
        int t[kNumVoices];
        const int n = targets(idx, t);
        for (int i = 0; i < n; ++i) { fP[t[i]] = v; setParameterValue(t[i], v); }
        fMatchDirty = true;
    }
    void setOne(int idx, float v)
    {
        editParameter(idx, true);
        fP[idx] = v; setParameterValue(idx, v);
        editParameter(idx, false);
        fMatchDirty = true;
    }
    void applyValues(const float* v)
    {
        for (int i = 0; i < kInputCount; ++i)
            if (fP[i] != v[i]) setOne(i, v[i]);
    }
    bool mixed(int idx) const
    {
        if (!isVoiceParam(idx) || fSel >= 0) return false;
        const int p = (idx - kGlobalCount) % kVoiceParamCount;
        for (int v = 1; v < kNumVoices; ++v)
            if (fP[voiceIndex(v, p)] != fP[voiceIndex(0, p)]) return true;
        return false;
    }

    static float toNorm(const Def& d, float v)
    {
        if (d.flags & fLog) return std::log(v / d.min) / std::log(d.max / d.min);
        return (v - d.min) / (d.max - d.min);
    }
    static float fromNorm(const Def& d, float n)
    {
        n = std::min(1.f, std::max(0.f, n));
        float v = (d.flags & fLog) ? d.min * std::pow(d.max / d.min, n) : d.min + (d.max - d.min) * n;
        if (d.flags & (fInt | fBool)) v = std::round(v);
        return std::min(d.max, std::max(d.min, v));
    }

    static void formatValue(int idx, float v, char* b, size_t n)
    {
        const Def& d = defOf(idx);
        const char* const u = d.unit;
        const int vp = isVoiceParam(idx) ? (idx - kGlobalCount) % kVoiceParamCount : -1;
        if (idx == gSlope)
            std::snprintf(b, n, "%s", kSlopeNames[std::min(3, std::max(0, int(v + 0.5f)))]);
        else if (vp == vPan)
        {
            if (std::fabs(v) < 0.5f) std::snprintf(b, n, "C");
            else std::snprintf(b, n, "%s%.0f", v < 0 ? "L" : "R", std::fabs(v));
        }
        else if (vp == vOct)  std::snprintf(b, n, "%+d oct", int(std::lround(v)));
        else if (vp == vSemi) std::snprintf(b, n, "%+d st", int(std::lround(v)));
        else if (!std::strcmp(u, "Hz"))
        {
            if (v < 100.f) std::snprintf(b, n, "%.1f Hz", v);
            else if (v < 1000.f) std::snprintf(b, n, "%.0f Hz", v);
            else std::snprintf(b, n, "%.2f kHz", v * 0.001f);
        }
        else if (idx == gScatter)
        {
            if (v < 0.5f) std::snprintf(b, n, "off");
            else std::snprintf(b, n, "%.0f ct", v);
        }
        else if (!std::strcmp(u, "ms"))
        {
            if (v < 10.f) std::snprintf(b, n, "%.1f ms", v);
            else if (v < 100.f && idx >= gDetClarity && idx <= gDetBridge) std::snprintf(b, n, "%.1f ms", v);
            else if (v < 1000.f) std::snprintf(b, n, "%.0f ms", v);
            else std::snprintf(b, n, "%.2f s", v * 0.001f);
        }
        else if (!std::strcmp(u, "%"))   std::snprintf(b, n, "%.0f %%", v);
        else if (!std::strcmp(u, "dB"))  std::snprintf(b, n, "%+.1f dB", v);
        else if (!std::strcmp(u, "ct"))  std::snprintf(b, n, "%+.1f ct", v);
        else if (!std::strcmp(u, "oct")) std::snprintf(b, n, "%+.2f oct", v);
        else if (!std::strcmp(u, "per")) std::snprintf(b, n, "%.2f per", v);
        else if (!std::strcmp(u, "x"))   std::snprintf(b, n, "x %.2f", v);
        else std::snprintf(b, n, "%.2f", v);
    }

    void setStatus(const std::string& s) { fStatus = s; fStatusUntil = ImGui::GetTime() + 8.0; }

    // ----------------------------------------------------------------------------------------------------------------
    // widgets (x, y are window coordinates, already scaled)

    void centeredText(ImDrawList* dl, float cx, float y, ImU32 col, const char* txt)
    {
        const ImVec2 sz = ImGui::CalcTextSize(txt);
        dl->AddText(ImVec2(std::floor(cx - sz.x * 0.5f), y), col, txt);
    }

    void tip(const char* text)
    {
        if (text != nullptr && ImGui::IsItemHovered() && GImGui->HoveredIdTimer > 0.6f) ImGui::SetTooltip("%s", text);
    }

    // unused = the knob has no effect with the current settings (drawn dimmed, still editable)
    void knob(const char* label, int idx, float x, float y, ImU32 accent, const char* tipText, bool unused = false)
    {
        const Def& d = defOf(idx);
        const ImGuiIO& io = ImGui::GetIO();
        const float cell = S(66), r = S(18);

        ImGui::PushID(idx);
        ImGui::SetCursorPos(ImVec2(x, y));
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##knob", ImVec2(cell, S(76)));
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();

        if (ImGui::IsItemActivated()) { beginEdit(idx); fDragNorm = toNorm(d, fP[idx]); }
        if (active)
        {
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            {
                setValue(idx, defaultOf(idx));
                fDragNorm = toNorm(d, fP[idx]);
            }
            else if (io.MouseDelta.y != 0.f)
            {
                fDragNorm -= io.MouseDelta.y * (io.KeyShift ? 0.0006f : 0.005f) / fScale;
                fDragNorm = std::min(1.f, std::max(0.f, fDragNorm));
                setValue(idx, fromNorm(d, fDragNorm));
            }
        }
        if (ImGui::IsItemDeactivated()) endEdit(idx);
        if (hovered && !active && io.MouseWheel != 0.f)
        {
            beginEdit(idx);
            if (d.flags & fInt) setValue(idx, std::min(d.max, std::max(d.min, fP[idx] + (io.MouseWheel > 0.f ? 1.f : -1.f))));
            else setValue(idx, fromNorm(d, toNorm(d, fP[idx]) + io.MouseWheel * (io.KeyShift ? 0.004f : 0.02f)));
            endEdit(idx);
        }

        ImDrawList* const dl = ImGui::GetWindowDrawList();
        const float cx = p0.x + cell * 0.5f;
        const ImVec2 c(cx, p0.y + S(17) + r);
        const float a0 = 0.75f * 3.14159265f, a1 = 2.25f * 3.14159265f;
        const float nrm = std::min(1.f, std::max(0.f, toNorm(d, fP[idx])));
        const float an = a0 + (a1 - a0) * nrm;
        const bool bipolar = d.min < 0.f && d.max > 0.f && !(d.flags & fLog);
        const float from = bipolar ? a0 + (a1 - a0) * toNorm(d, 0.f) : a0;
        const float fade = unused ? 0.4f : 1.f;

        centeredText(dl, cx, p0.y, withAlpha(hovered || active ? kColText : kColDim, fade), label);
        dl->PathArcTo(c, r, a0, a1, 40);
        dl->PathStroke(kColTrack, 0, S(3.f));
        if (std::fabs(an - from) > 0.01f)
        {
            dl->PathArcTo(c, r, std::min(from, an), std::max(from, an), 40);
            dl->PathStroke(withAlpha(accent, fade), 0, S(3.f));
        }
        dl->AddCircleFilled(c, r - S(5), active ? IM_COL32(86, 92, 106, 255) : (hovered ? IM_COL32(70, 75, 88, 255) : IM_COL32(56, 60, 70, 255)), 32);
        const ImVec2 dir(std::cos(an), std::sin(an));
        dl->AddLine(ImVec2(c.x + dir.x * (r - S(12)), c.y + dir.y * (r - S(12))),
                    ImVec2(c.x + dir.x * (r - S(6)), c.y + dir.y * (r - S(6))), withAlpha(kColText, fade), S(2.f));

        char val[48], shown[56];
        formatValue(idx, fP[idx], val, sizeof(val));
        std::snprintf(shown, sizeof(shown), "%s%s", mixed(idx) ? "~ " : "", val);
        centeredText(dl, cx, c.y + r + S(3), withAlpha(kColText, fade), shown);

        if (hovered && !active && tipText != nullptr && GImGui->HoveredIdTimer > 0.6f)
            ImGui::SetTooltip("%s", tipText);
        ImGui::PopID();
    }

    void toggle(const char* label, int idx, float x, float y, const char* tipText)
    {
        ImGui::SetCursorPos(ImVec2(x, y));
        bool on = fP[idx] > 0.5f;
        ImGui::PushID(idx);
        if (ImGui::Checkbox(label, &on)) setOne(idx, on ? 1.f : 0.f);
        tip(tipText);
        ImGui::PopID();
    }

    // drop-down for a parameter that is a list of choices
    void combo(int idx, const char* const* names, int count, float x, float y, float w, const char* tipText)
    {
        ImGui::PushID(idx);
        ImGui::SetCursorPos(ImVec2(x, y));
        ImGui::SetNextItemWidth(w);
        const int cur = std::min(count - 1, std::max(0, int(fP[idx] + 0.5f)));
        const std::string preview = std::string(mixed(idx) ? "~ " : "") + names[cur];
        if (ImGui::BeginCombo("##combo", preview.c_str()))
        {
            for (int i = 0; i < count; ++i)
                if (ImGui::Selectable(names[i], i == cur))
                {
                    beginEdit(idx); setValue(idx, float(i)); endEdit(idx);
                }
            ImGui::EndCombo();
        }
        tip(tipText);
        ImGui::PopID();
    }

    void panel(ImDrawList* dl, const ImVec2& wp, float x, float y, float w, float h, const char* title)
    {
        dl->AddRectFilled(ImVec2(wp.x + x, wp.y + y), ImVec2(wp.x + x + w, wp.y + y + h), kColPanel, S(6));
        if (title != nullptr) dl->AddText(ImVec2(wp.x + x + S(10), wp.y + y + S(6)), kColAccent, title);
    }

    // ----------------------------------------------------------------------------------------------------------------
    // display: the wave of every voice over two periods of the played note, and what the synth is playing

    void display(ImDrawList* dl, const ImVec2& wp, float x, float y, float w, float h)
    {
        const ImVec2 a(wp.x + x, wp.y + y), b(wp.x + x + w, wp.y + y + h);
        dl->AddRectFilled(a, b, kColPanel, S(6));
        dl->AddText(ImVec2(a.x + S(10), a.y + S(6)), kColAccent, "VOICES");
        dl->AddText(ImVec2(a.x + S(70), a.y + S(6)), kColDim, "two periods of the played note; white = all voices added up");

        const float sideW = S(170);
        const ImVec2 g0(a.x + S(12), a.y + S(28)), g1(b.x - sideW - S(12), b.y - S(12));
        dl->AddRectFilled(g0, g1, kColGraph);
        const float gw = g1.x - g0.x, gh = g1.y - g0.y, mid = 0.5f * (g0.y + g1.y);
        for (int i = 1; i < 4; ++i)
        {
            const float xx = std::floor(g0.x + gw * float(i) / 4.f) + 0.5f;
            dl->AddLine(ImVec2(xx, g0.y), ImVec2(xx, g1.y), kColGrid);
        }
        dl->AddLine(ImVec2(g0.x, mid), ImVec2(g1.x, mid), kColGrid);

        Eff eff[kNumVoices];
        const int n = computeEff(fP, eff);
        enum { kPts = 400 };
        static ImVec2 pts[kPts];
        static float sum[kPts];
        for (int i = 0; i < kPts; ++i) sum[i] = 0.f;
        float maxLevel = 0.01f;
        for (int v = 0; v < kNumVoices; ++v)
            if (eff[v].on) maxLevel = std::max(maxLevel, fP[voiceIndex(v, vLevel)] * 0.01f);
        const float amp = gh * 0.42f / std::max(1.f, maxLevel);

        dl->PushClipRect(g0, g1, true);
        for (int pass = 0; pass < 2; ++pass)
            for (int v = 0; v < kNumVoices; ++v)
            {
                if (!eff[v].on) continue;
                const bool sel = fSel == v;
                if ((pass == 1) != sel) continue;
                const float* vp = fP + voiceIndex(v, 0);
                const int wave = std::min(int(kWaveCount) - 1, std::max(0, int(vp[vWave] + 0.5f)));
                const float lvl = vp[vLevel] * 0.01f;
                const float ratio = std::pow(2.f, std::round(vp[vOct]) + std::round(vp[vSemi]) / 12.f + eff[v].cents / 1200.f);
                for (int i = 0; i < kPts; ++i)
                {
                    const float ph = 2.f * float(i) / float(kPts - 1) * ratio;
                    const float s = oscNaive(wave, ph, vp[vWidth] * 0.01f, fWaveTable, v) * lvl;
                    sum[i] += s;
                    pts[i] = ImVec2(g0.x + gw * float(i) / float(kPts - 1), mid - s * amp);
                }
                const ImU32 col = (fSel < 0 || sel) ? withAlpha(kVoiceColors[v], 0.9f) : withAlpha(kVoiceColors[v], 0.4f);
                dl->AddPolyline(pts, kPts, col, 0, S(sel ? 2.5f : 1.5f));
            }
        if (n > 1)
        {
            // scaled to the same height as the loudest single voice
            float peak = 1e-3f;
            for (int i = 0; i < kPts; ++i) peak = std::max(peak, std::fabs(sum[i]));
            const float norm = std::max(1.f, maxLevel) / peak;
            for (int i = 0; i < kPts; ++i)
                pts[i] = ImVec2(g0.x + gw * float(i) / float(kPts - 1), mid - sum[i] * norm * amp);
            dl->AddPolyline(pts, kPts, withAlpha(kColText, 0.75f), 0, S(1.5f));
        }
        dl->PopClipRect();
        if (n == 0)
            centeredText(dl, 0.5f * (g0.x + g1.x), mid - S(8), kColDim, "All voices are off - switch one on below");

        // ---- what is being played
        const float sx = g1.x + S(12), sw = b.x - S(12) - sx, scx = sx + sw * 0.5f;
        dl->AddRectFilled(ImVec2(sx, g0.y), ImVec2(b.x - S(12), g1.y), kColGraph);
        const float hz = fP[kOutFreq];
        const double now = ImGui::GetTime();
        if (hz > 10.f) { fShownHz = hz; fShownUntil = now + 0.6; }
        const bool live = now < fShownUntil;
        char name[16], txt[64];
        float cents = 0.f;
        noteName(live ? fShownHz : 0.f, &cents, name, sizeof(name));
        centeredText(dl, scx, g0.y + S(8), kColDim, "PLAYING");
        {
            ImFont* const font = ImGui::GetFont();
            const float fs = S(44);
            const ImVec2 sz = font->CalcTextSizeA(fs, 1e4f, 0.f, name);
            dl->AddText(font, fs, ImVec2(std::floor(scx - sz.x * 0.5f), g0.y + S(26)), live ? kColText : kColTrack, name);
        }
        if (live) std::snprintf(txt, sizeof(txt), "%.1f Hz", fShownHz); else std::snprintf(txt, sizeof(txt), "-");
        centeredText(dl, scx, g0.y + S(76), kColDim, txt);

        // cents: a tick left or right of the centre mark
        {
            const float by = g0.y + S(104), bx0 = sx + S(14), bx1 = sx + sw - S(14);
            dl->AddRectFilled(ImVec2(bx0, by), ImVec2(bx1, by + S(4)), kColTrack, S(2));
            dl->AddRectFilled(ImVec2(scx - S(1), by - S(5)), ImVec2(scx + S(1), by + S(9)), kColDim);
            if (live)
            {
                const float px = scx + (bx1 - bx0) * 0.5f * std::min(1.f, std::max(-1.f, cents / 50.f));
                dl->AddRectFilled(ImVec2(px - S(3), by - S(6)), ImVec2(px + S(3), by + S(10)), kColAccent, S(2));
                std::snprintf(txt, sizeof(txt), "%+.0f ct", cents);
            }
            else std::snprintf(txt, sizeof(txt), " ");
            centeredText(dl, scx, by + S(14), kColDim, txt);
        }

        // how long the note took to be recognised
        {
            const float ly = g0.y + S(150);
            if (fP[gMode] > 0.5f)
            {
                centeredText(dl, scx, ly, kColDim, "no detection delay");
                centeredText(dl, scx, ly + S(16), kColDim, "(signal reshaped)");
            }
            else
            {
                centeredText(dl, scx, ly, kColDim, "note found after");
                std::snprintf(txt, sizeof(txt), "%.1f ms", fP[kOutLatency]);
                centeredText(dl, scx, ly + S(16), fP[kOutLatency] > 0.f ? kColText : kColDim, fP[kOutLatency] > 0.f ? txt : "-");
            }
        }

        // input level in dB with the gate threshold marked
        {
            const float my = g1.y - S(44), mx0 = sx + S(14), mx1 = sx + sw - S(14);
            const auto pos = [&](float db) { return mx0 + (mx1 - mx0) * std::min(1.f, std::max(0.f, (db + 80.f) / 80.f)); };
            const float lv = fP[kOutEnv] / 2.5f;
            const float db = lv > 1e-5f ? 20.f * std::log10(lv) : -100.f;
            const bool open = fP[kOutGate] > 0.5f;
            dl->AddRectFilled(ImVec2(mx0, my), ImVec2(mx1, my + S(8)), kColTrack, S(2));
            if (db > -80.f) dl->AddRectFilled(ImVec2(mx0, my), ImVec2(pos(db), my + S(8)), open ? kColAccent : kColDim, S(2));
            const float gx = pos(fP[gGate]);
            dl->AddRectFilled(ImVec2(gx - S(1), my - S(4)), ImVec2(gx + S(1), my + S(12)), kColText);
            centeredText(dl, scx, my + S(16), kColDim, open ? "input - gate open" : "input - gate closed");
        }
    }

    // ----------------------------------------------------------------------------------------------------------------
    // presets

    void loadByName(const std::string& name)
    {
        float v[kInputCount];
        std::string wave;
        if (!fLib.lookup(name, v, &wave)) return;
        applyPreset(v, wave);
        fCurName = name;
        fMatchDirty = false;
    }

    void matchPreset()
    {
        fMatchDirty = false;
        float v[kInputCount];
        std::string wave;
        if (!fCurName.empty() && fLib.lookup(fCurName, v, &wave) && sameValues(v, fP) && wave == fWave) return;
        fCurName.clear();
        for (int i = 0; i < kPresetCount; ++i)
        {
            kPresets[i].fill(v);
            if (sameValues(v, fP) && fWave == presetWave(i)) { fCurName = kPresets[i].name; return; }
        }
        for (const UserPreset& u : fLib.user)
            if (sameValues(u.v, fP) && u.wave == fWave) { fCurName = u.name; return; }
    }

    void header(float y)
    {
        ImGui::SetCursorPos(ImVec2(S(16), y + S(3)));
        ImGui::TextColored(ImVec4(0.47f, 0.78f, 1.f, 1.f), "SWARM SYNTH");

        ImGui::SetCursorPos(ImVec2(S(140), y));
        ImGui::SetNextItemWidth(S(250));
        const std::string shown = fCurName.empty() ? std::string("(custom settings)") : fCurName;
        if (ImGui::BeginCombo("##preset", shown.c_str(), ImGuiComboFlags_HeightLargest))
        {
            for (int i = 0; i < kPresetCount; ++i)
            {
                if (kPresets[i].group != nullptr)
                {
                    if (i > 0) ImGui::Separator();
                    ImGui::TextDisabled("%s", kPresets[i].group);
                }
                if (ImGui::Selectable(kPresets[i].name, fCurName == kPresets[i].name))
                    loadByName(kPresets[i].name);
            }
            ImGui::Separator();
            ImGui::TextDisabled(fLib.user.empty() ? "User (none yet - use Save or Import)" : "User");
            std::string pick;
            for (size_t i = 0; i < fLib.user.size(); ++i)
            {
                ImGui::PushID(int(i));
                if (ImGui::Selectable(fLib.user[i].name.c_str(), fCurName == fLib.user[i].name))
                    pick = fLib.user[i].name;
                ImGui::PopID();
            }
            if (!pick.empty()) loadByName(pick);
            ImGui::EndCombo();
        }

        ImGui::SameLine(0, S(8));
        if (ImGui::Button("Save..."))
        {
            const std::string base = (!fCurName.empty() && !Library::isFactory(fCurName)) ? fCurName : std::string("My preset");
            std::snprintf(fNameBuf, sizeof(fNameBuf), "%s", base.c_str());
            fOpenSave = true;
        }
        ImGui::SameLine(0, S(4));
        const int curUser = fCurName.empty() ? -1 : fLib.findUser(fCurName);
        ImGui::BeginDisabled(curUser < 0);
        if (ImGui::Button("Delete"))
            ImGui::OpenPopup("Delete preset?");
        ImGui::EndDisabled();
        ImGui::SameLine(0, S(16));
        if (ImGui::Button("Export..."))
            ImGui::OpenPopup("##exportmenu");
        ImGui::SameLine(0, S(4));
        if (ImGui::Button("Import..."))
        {
            FileBrowserOptions opts;
            opts.title = "Import presets (CSV)";
            fBrowse = kBrowseImport;
            if (!openFileBrowser(opts)) { fBrowse = kBrowseNone; setStatus("Could not open the file dialog"); }
        }

        if (ImGui::BeginPopup("##exportmenu"))
        {
            if (ImGui::MenuItem("Current settings..."))
                startExport(kBrowseExportCurrent, fCurName.empty() ? "swarm-synth-preset.csv" : (fileSafe(fCurName) + ".csv").c_str());
            if (ImGui::MenuItem("All user presets...", nullptr, false, !fLib.user.empty()))
                startExport(kBrowseExportAll, "swarm-synth-user-presets.csv");
            ImGui::EndPopup();
        }

        if (fOpenSave) { ImGui::OpenPopup("Save preset"); fOpenSave = false; }
        ImGui::SetNextWindowPos(ImVec2(getWidth() * 0.5f, getHeight() * 0.4f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Save preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Name of the preset:");
            ImGui::SetNextItemWidth(S(300));
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            const bool enter = ImGui::InputText("##name", fNameBuf, sizeof(fNameBuf), ImGuiInputTextFlags_EnterReturnsTrue);
            const std::string name = trim(fNameBuf);
            const bool factory = Library::isFactory(name);
            const int existing = fLib.findUser(name);
            if (factory) ImGui::TextColored(ImVec4(1.f, 0.6f, 0.4f, 1.f), "That is a factory preset name - pick another one.");
            else if (existing >= 0) ImGui::TextColored(ImVec4(1.f, 0.85f, 0.4f, 1.f), "A preset with this name exists and will be replaced.");
            else ImGui::TextDisabled("Stored in your user preset list.");
            const bool can = !name.empty() && !factory;
            ImGui::BeginDisabled(!can);
            const bool ok = ImGui::Button(existing >= 0 ? "Replace" : "Save", ImVec2(S(100), 0));
            ImGui::EndDisabled();
            if (can && (ok || enter))
            {
                UserPreset p;
                p.name = name;
                std::memcpy(p.v, fP, sizeof(p.v));
                p.wave = fWave;
                if (existing >= 0) fLib.user[existing] = p; else fLib.user.push_back(p);
                fCurName = name;
                setStatus(fLib.save() ? "Saved \"" + name + "\"" : "Saved for this session only (could not write the preset file)");
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(S(100), 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::SetNextWindowPos(ImVec2(getWidth() * 0.5f, getHeight() * 0.4f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Delete preset?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text("Delete the user preset \"%s\"?", fCurName.c_str());
            if (ImGui::Button("Delete", ImVec2(S(100), 0)))
            {
                const int u = fLib.findUser(fCurName);
                if (u >= 0)
                {
                    fLib.user.erase(fLib.user.begin() + u);
                    fLib.save();
                    setStatus("Deleted \"" + fCurName + "\"");
                    fCurName.clear();
                }
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(S(100), 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::SameLine(0, S(16));
        if (ImGui::Button("Random")) randomize();
        tip(T("Randomises the voices (how many, waves, octaves), the detune and the filter. Mode, loudness and envelope stay as they are.", "Losuje głosy (liczbę, fale, oktawy), rozstrojenie i filtr. Tryb, głośność i obwiednia zostają bez zmian."));
        ImGui::SameLine(0, S(16));
        for (int slot = 0; slot < 2; ++slot)
        {
            if (slot == 1) ImGui::SameLine(0, S(2));
            ImGui::PushStyleColor(ImGuiCol_Button, fABCur == slot ? ImVec4(0.30f, 0.42f, 0.58f, 1.f) : ImGui::GetStyle().Colors[ImGuiCol_Button]);
            if (ImGui::Button(slot == 0 ? " A " : " B ")) switchAB(slot);
            ImGui::PopStyleColor();
            tip(T("Two slots for comparing settings quickly.\nThe first time you switch, B gets a copy of the current settings.", "Dwa schowki na ustawienia do szybkiego porównania.\nPrzy pierwszym przełączeniu B dostaje kopię bieżących ustawień."));
        }

        ImGui::SameLine(0, S(16));
        if (ImGui::Button(fLangPl ? "PL" : "EN", ImVec2(S(34), 0))) { fLangPl = !fLangPl; saveSettings(); }
        tip("Language of the explanations: English / Polish. Click to switch.\nJęzyk opisów: angielski / polski. Kliknij, żeby przełączyć.");

        // gate LED
        ImDrawList* const dl = ImGui::GetWindowDrawList();
        const ImVec2 wp = ImGui::GetWindowPos();
        const bool lit = fP[kOutGate] > 0.5f;
        const ImVec2 c(wp.x + getWidth() - S(22), wp.y + y + S(11));
        dl->AddCircleFilled(c, S(6), lit ? IM_COL32(255, 90, 70, 255) : IM_COL32(70, 40, 40, 255));
        dl->AddText(ImVec2(c.x - S(48), c.y - S(8)), kColDim, "NOTE");
    }

    void switchAB(int to)
    {
        if (to == fABCur) return;
        std::memcpy(fAB[fABCur], fP, sizeof(fAB[0]));
        fABWave[fABCur] = fWave;
        fABValid[fABCur] = true;
        if (!fABValid[to]) { std::memcpy(fAB[to], fP, sizeof(fAB[0])); fABWave[to] = fWave; fABValid[to] = true; }
        fABCur = to;
        applyPreset(fAB[to], fABWave[to]);
    }

    void randomize()
    {
        const auto uni = [this](float lo, float hi) { return lo + (hi - lo) * std::uniform_real_distribution<float>(0.f, 1.f)(fRng); };
        const auto logu = [&uni](float lo, float hi) { return lo * std::pow(hi / lo, uni(0.f, 1.f)); };
        const auto chance = [&uni](float pr) { return uni(0.f, 1.f) < pr; };
        static const int kWaves[5] = { wSaw, wSaw, wPulse, wTriangle, wSine };

        float v[kInputCount];
        std::memcpy(v, fP, sizeof(v));
        const int voices = 1 + int(uni(0.f, 7.999f));
        const bool sameWave = chance(0.6f);
        const int wave0 = kWaves[int(uni(0.f, 4.999f))];
        for (int i = 0; i < kNumVoices; ++i)
        {
            float* const vp = v + voiceIndex(i, 0);
            vp[vOn] = i < voices ? 1.f : 0.f;
            vp[vLevel] = 100.f; vp[vPan] = 0.f; vp[vDetune] = 0.f;
            vp[vWave] = float(sameWave ? wave0 : kWaves[int(uni(0.f, 4.999f))]);
            vp[vWidth] = chance(0.5f) ? 50.f : uni(15.f, 85.f);
            vp[vOct] = 0.f; vp[vSemi] = 0.f;
            if (i > 0 && chance(0.3f)) { vp[vOct] = chance(0.5f) ? -1.f : 1.f; vp[vLevel] = 70.f; }
            else if (i > 0 && chance(0.15f)) { vp[vSemi] = 7.f; vp[vLevel] = 75.f; }
        }
        v[gSpDetune] = voices > 1 ? uni(3.f, 25.f) : 0.f;
        v[gSpPan] = uni(40.f, 100.f);
        v[gDrift] = (voices > 2 && chance(0.4f)) ? uni(4.f, 20.f) : 0.f;
        v[gFltOn] = chance(0.85f) ? 1.f : 0.f;
        v[gCutoff] = logu(300.f, 5000.f); v[gReso] = uni(0.f, 60.f); v[gSlope] = float(int(uni(1.f, 3.999f)));
        v[gDrive] = chance(0.3f) ? uni(3.f, 18.f) : 0.f;
        v[gFltEnv] = uni(0.f, 4.f); v[gFltAtk] = 2.f; v[gFltDec] = logu(100.f, 1200.f); v[gKey] = uni(30.f, 100.f);
        sanitize(v);
        applyValues(v);
        fCurName.clear();
    }

    // editor for the "Custom wave" oscillator shape: draggable points, a formula, or one cycle from a WAV file
    void waveEditor()
    {
        if (fOpenWave) { ImGui::OpenPopup("Custom wave"); fOpenWave = false; syncEditor(); fFormulaErr.clear(); fNodeDrag = -1; }
        ImGui::SetNextWindowPos(ImVec2(getWidth() * 0.5f, getHeight() * 0.45f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopup("Custom wave")) return;

        const bool formula = fWave.size() > 1 && fWave[0] == 'f';
        const bool sample = fWave.size() > 1 && fWave[0] == 'w';
        const bool points = !formula && !sample;
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColAccent), "CUSTOM WAVE");
        ImGui::SameLine();
        ImGui::TextDisabled(formula ? "formula active  -  one cycle of the oscillator"
                            : (sample ? "one cycle loaded from a WAV file"
                                      : "drag a point  -  double-click = add  -  right-click = remove"));

        const ImVec2 size(S(540), S(220));
        const ImVec2 c0 = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##wave", size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const ImGuiIO& io = ImGui::GetIO();
        // a small margin keeps points at the very top, bottom and left edge easy to grab
        const float pad = S(9);
        const float iw = size.x - 2.f * pad, ih = size.y - 2.f * pad;
        const float mx = (io.MousePos.x - c0.x - pad) / iw;
        const float my = 1.f - (io.MousePos.y - c0.y - pad) / ih;
        const auto toScreen = [&](float x, float y) { return ImVec2(c0.x + pad + iw * x, c0.y + pad + ih * (1.f - y)); };

        int hover = -1;
        if (points)
        {
            if (ImGui::IsItemHovered() || ImGui::IsItemActive())
            {
                float best = S(11);
                for (int i = 0; i < int(fNodes.size()); ++i)
                {
                    const ImVec2 sp = toScreen(fNodes[i].x, fNodes[i].y);
                    const float d = std::hypot(sp.x - io.MousePos.x, sp.y - io.MousePos.y);
                    if (d < best) { best = d; hover = i; }
                }
            }
            if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            {
                if (hover >= 0 && fNodes.size() > 2)
                {
                    fNodes.erase(fNodes.begin() + hover);
                    setWave(nodesToString(fNodes, fNodesSmooth));
                    hover = -1;
                }
            }
            else if (ImGui::IsItemActivated() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            {
                fNodeDrag = hover;
                if (hover < 0 && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && int(fNodes.size()) < kMaxNodes)
                {
                    WaveNode n;
                    n.x = std::min(0.9999f, std::max(0.f, mx));
                    n.y = std::min(1.f, std::max(0.f, my));
                    size_t at = 0;
                    while (at < fNodes.size() && fNodes[at].x < n.x) ++at;
                    fNodes.insert(fNodes.begin() + at, n);
                    fNodeDrag = int(at);
                    setWave(nodesToString(fNodes, fNodesSmooth));
                }
            }
            if (ImGui::IsItemActive() && fNodeDrag >= 0 && fNodeDrag < int(fNodes.size()) && ImGui::IsMouseDown(ImGuiMouseButton_Left))
            {
                // a point can't cross its neighbours, so the order never changes while dragging
                const int d = fNodeDrag;
                const float lo = d > 0 ? fNodes[d - 1].x + 0.004f : 0.f;
                const float hi = d + 1 < int(fNodes.size()) ? fNodes[d + 1].x - 0.004f : 0.9999f;
                const float nx = std::min(std::max(lo, hi), std::max(lo, mx));
                const float ny = std::min(1.f, std::max(0.f, my));
                if (nx != fNodes[d].x || ny != fNodes[d].y)
                {
                    fNodes[d].x = nx; fNodes[d].y = ny;
                    setWave(nodesToString(fNodes, fNodesSmooth));
                }
                hover = d;
            }
            if (ImGui::IsItemDeactivated()) fNodeDrag = -1;
        }

        ImDrawList* const dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(c0, ImVec2(c0.x + size.x, c0.y + size.y), kColGraph);
        for (int i = 1; i < 4; ++i)
        {
            const float yy = c0.y + pad + ih * float(i) / 4.f, xx = c0.x + pad + iw * float(i) / 4.f;
            dl->AddLine(ImVec2(c0.x, yy), ImVec2(c0.x + size.x, yy), kColGrid);
            dl->AddLine(ImVec2(xx, c0.y), ImVec2(xx, c0.y + size.y), kColGrid);
        }
        dl->AddRect(ImVec2(c0.x + pad, c0.y + pad), ImVec2(c0.x + pad + iw, c0.y + pad + ih), kColGrid);
        enum { kDraw = 512 };
        static ImVec2 pts[kDraw + 1];
        for (int i = 0; i <= kDraw; ++i)
            pts[i] = toScreen(float(i) / float(kDraw), fWaveTable[(i * (kWaveSize / kDraw)) & (kWaveSize - 1)]);
        dl->PushClipRect(c0, ImVec2(c0.x + size.x, c0.y + size.y), true);
        dl->AddPolyline(pts, kDraw + 1, kColAccent, 0, S(2.f));
        dl->PopClipRect();
        if (points)
            for (int i = 0; i < int(fNodes.size()); ++i)
            {
                const ImVec2 sp = toScreen(fNodes[i].x, fNodes[i].y);
                dl->AddCircleFilled(sp, S(i == hover ? 7.f : 5.f), kColBg);
                dl->AddCircle(sp, S(i == hover ? 7.f : 5.f), i == hover ? kColText : kColAccent, 20, S(2.f));
            }

        // ---- points
        const auto setNodes = [this](const char* text) { if (setWave(text)) syncEditor(); };
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Points:");
        ImGui::SameLine(S(76)); if (ImGui::Button("Sine")) setNodes("n:0,0.5;0.25,1;0.75,0");
        ImGui::SameLine(); if (ImGui::Button("Triangle")) setNodes("l:0,0.5;0.25,1;0.75,0");
        ImGui::SameLine(); if (ImGui::Button("Saw")) setNodes("l:0,0;0.999,1");
        ImGui::SameLine(); if (ImGui::Button("Square")) setNodes("l:0,1;0.499,1;0.5,0;0.999,0");
        ImGui::SameLine();
        if (ImGui::Button("Random"))
        {
            std::vector<WaveNode> nd;
            for (int i = 0; i < 8; ++i)
                nd.push_back({ float(i) / 8.f, std::uniform_real_distribution<float>(0.f, 1.f)(fRng) });
            setNodes(nodesToString(nd, true).c_str());
        }
        ImGui::SameLine(0, S(18));
        if (!points)
        {
            if (ImGui::Button("Wave -> points"))
            {
                std::vector<WaveNode> nd;
                for (int i = 0; i < kMaxNodes; ++i) nd.push_back({ float(i) / float(kMaxNodes), fWaveTable[i * kWaveSize / kMaxNodes] });
                setNodes(nodesToString(nd, true).c_str());
            }
            tip(T("Turns the current wave into 32 points that you can then move by hand.", "Zamienia bieżącą falę na 32 punkty, które można potem przesuwać ręcznie."));
        }
        else if (ImGui::Checkbox("Smooth curve", &fNodesSmooth))
            setWave(nodesToString(fNodes, fNodesSmooth));

        // ---- formula
        static const char* const kExamples[] = {
            "sin(2*pi*x)", "sin(2*pi*x) + 0.5*sin(4*pi*x) + 0.3*sin(6*pi*x)", "sin(2*pi*x)^3", "tanh(4*sin(2*pi*x))",
            "sin(2*pi*x + 2*sin(4*pi*x))", "sin(2*pi*x) * exp(-3*x)", "abs(sin(pi*x))", "floor(sin(2*pi*x)*4)/4",
            "tri(x) + 0.5*sqr(3*x)", "saw(x) - saw(x + 0.35)",
        };
        bool apply = false;
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Formula:");
        ImGui::SameLine(S(76));
        ImGui::SetNextItemWidth(S(290));
        if (ImGui::InputText("##formula", fFormulaBuf, sizeof(fFormulaBuf), ImGuiInputTextFlags_EnterReturnsTrue)) apply = true;
        ImGui::SameLine(); if (ImGui::Button("Apply")) apply = true;
        ImGui::SameLine();
        ImGui::SetNextItemWidth(S(104));
        if (ImGui::BeginCombo("##examples", "Examples"))
        {
            for (const char* ex : kExamples)
                if (ImGui::Selectable(ex)) { std::snprintf(fFormulaBuf, sizeof(fFormulaBuf), "%s", ex); apply = true; }
            ImGui::EndCombo();
        }
        if (apply)
        {
            const std::string f = trim(fFormulaBuf);
            if (f.empty()) fFormulaErr = "type a formula first";
            else setWave("f:" + f);
        }
        if (!fFormulaErr.empty())
            ImGui::TextColored(ImVec4(1.f, 0.55f, 0.45f, 1.f), "Not applied: %s", fFormulaErr.c_str());
        else
            ImGui::TextDisabled("x runs 0..1 over one cycle; the result is scaled to full height and stored as a table.");
        ImGui::TextDisabled("sin cos tan tanh abs sqrt exp ln floor frac min max pow mod clamp mix step tri saw sqr, pi, e, + - * / ^");

        // ---- file
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("File:");
        ImGui::SameLine(S(76));
        if (ImGui::Button("Load WAV..."))
        {
            FileBrowserOptions opts;
            opts.title = "Load a single-cycle WAV";
            fBrowse = kBrowseWav;
            if (!openFileBrowser(opts)) { fBrowse = kBrowseNone; fFormulaErr = "could not open the file dialog"; }
        }
        tip(T("Loads a WAV file and takes the WHOLE file as one cycle of the wave (single-cycle files, e.g. the AKWF collection).\nThe wave is stored in the project and in the preset; the file is not needed afterwards.", "Wczytuje plik WAV i traktuje CAŁY plik jako jeden okres fali (pliki typu single-cycle, np. kolekcja AKWF).\n"
            "Fala jest zapisywana w projekcie i w presecie, plik nie jest potem potrzebny."));
        ImGui::SameLine();
        ImGui::TextDisabled("the whole file = one cycle.  Shared by every voice whose Wave is \"Custom wave\".");
        ImGui::SetCursorPosX(S(459));
        if (ImGui::Button("Close", ImVec2(S(90), 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // one detector setting: name, slider, value, and what it does
    void detSetting(int idx, const char* label, const char* desc)
    {
        const Def& d = defOf(idx);
        ImGui::PushID(idx);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColText), "%s", label);
        ImGui::SameLine(S(132));
        char val[48];
        formatValue(idx, fP[idx], val, sizeof(val));
        if (d.flags & fBool)
        {
            bool on = fP[idx] > 0.5f;
            if (ImGui::Checkbox("##b", &on)) setOne(idx, on ? 1.f : 0.f);
            ImGui::SameLine();
            ImGui::TextUnformatted(on ? "on" : "off");
        }
        else
        {
            float v = fP[idx];
            ImGui::SetNextItemWidth(S(190));
            const bool changed = ImGui::SliderFloat("##s", &v, d.min, d.max, "");
            if (ImGui::IsItemActivated()) editParameter(idx, true);
            if (changed) { fP[idx] = v; setParameterValue(idx, v); fMatchDirty = true; }
            if (ImGui::IsItemDeactivated()) editParameter(idx, false);
            ImGui::SameLine();
            ImGui::TextUnformatted(val);
            if (fP[idx] != defaultOf(idx))
            {
                ImGui::SameLine(S(396));
                ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(4), S(1)));
                if (ImGui::Button("reset")) setOne(idx, defaultOf(idx));
                ImGui::PopStyleVar();
            }
        }
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kColDim));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + S(440));
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(1, S(4)));
        ImGui::PopID();
    }

    // ----------------------------------------------------------------------------------------------------------------
    // saved detector settings - the library file is shared with the other plugin (see DetectorPresets.hpp)

    void detGet(float* d) const { for (int k = 0; k < detp::kCount; ++k) d[k] = fP[kDetIdx[k]]; }

    void detApply(const float* d)
    {
        float v[kInputCount];
        std::memcpy(v, fP, sizeof(v));
        for (int k = 0; k < detp::kCount; ++k) v[kDetIdx[k]] = d[k];
        applyValues(v);
    }

    // name of the saved setting or built-in profile the current values match, or ""
    std::string detCurrentName() const
    {
        float d[detp::kCount];
        detGet(d);
        const int u = fDetLib.match(d);
        if (u >= 0) return fDetLib.user[u].name;
        for (int i = 0; i < kDetProfileCount; ++i)
        {
            float v[kInputCount];
            std::memcpy(v, fP, sizeof(v));
            applyDetProfile(v, kDetProfiles[i]);
            float e[detp::kCount];
            for (int k = 0; k < detp::kCount; ++k) e[k] = v[kDetIdx[k]];
            if (detp::sameValues(d, e)) return kDetProfiles[i].name;
        }
        return std::string();
    }

    static std::vector<std::string> detReservedNames()
    {
        std::vector<std::string> r;
        for (int i = 0; i < kDetProfileCount; ++i) r.push_back(kDetProfiles[i].name);
        return r;
    }

    static std::string detFileSafe(const std::string& s)
    {
        std::string o;
        for (char c : s) o += (std::strchr("\\/:*?\"<>|", c) != nullptr) ? '_' : c;
        return o;
    }

    void startDetBrowse(int mode, const std::string& defaultName, bool saving)
    {
        FileBrowserOptions opts;
        opts.saving = saving;
        if (saving) opts.defaultName = defaultName.c_str();
        opts.title = saving ? "Export detector settings (CSV)" : "Import detector settings (CSV)";
        fBrowse = mode;
        if (!openFileBrowser(opts)) { fBrowse = kBrowseNone; fDetStatus = T("Could not open the file dialog", "Nie udało się otworzyć okna wyboru pliku"); }
    }

    // called from uiFileBrowserSelected for the detector modes
    void detFileSelected(int mode, std::string path)
    {
        char msg[200];
        if (mode == kBrowseDetImport)
        {
            float cur[detp::kCount];
            detGet(cur);
            detp::Report rep;
            if (!fDetLib.importFile(path.c_str(), cur, rep))
            {
                fDetStatus = T("Import failed: no detector columns in that file", "Import nieudany: plik nie ma kolumn detektora");
                return;
            }
            if (rep.added > 0)
            {
                const int i = fDetLib.find(rep.firstName);
                if (i >= 0) detApply(fDetLib.user[i].v);
            }
            std::snprintf(msg, sizeof(msg), T("Imported %d (%d renamed), skipped %d duplicate(s)%s", "Zaimportowano %d (%d ze zmienioną nazwą), pominięto duplikaty: %d%s"),
                          rep.added, rep.renamed, rep.skipped, rep.badRows > 0 ? T(", some rows unreadable", ", część wierszy nieczytelna") : "");
            fDetStatus = msg;
            return;
        }
        const size_t slash = path.find_last_of("/\\");
        const size_t dot = path.find_last_of('.');
        if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) path += ".csv";
        std::vector<detp::DetPreset> out;
        if (mode == kBrowseDetExportAll) { fDetLib.load(); out = fDetLib.user; }
        else
        {
            detp::DetPreset p;
            const std::string cur = detCurrentName();
            p.name = cur.empty() ? std::string("Detector settings") : cur;
            detGet(p.v);
            out.push_back(p);
        }
        if (detp::writePresets(path.c_str(), out))
        {
            std::snprintf(msg, sizeof(msg), T("Exported %d setting(s)", "Wyeksportowano ustawień: %d"), int(out.size()));
            fDetStatus = msg;
        }
        else fDetStatus = T("Export failed: could not write the file", "Eksport nieudany: nie można zapisać pliku");
    }

    void detPresetRow()
    {
        const std::string cur = detCurrentName();
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(T("My settings:", "Moje ustawienia:"));
        ImGui::SameLine();
        ImGui::SetNextItemWidth(S(240));
        const std::string shown = cur.empty() ? std::string(T("(current - not saved)", "(bieżące - niezapisane)")) : cur;
        if (ImGui::BeginCombo("##detlib", shown.c_str(), ImGuiComboFlags_HeightLarge))
        {
            if (fDetLib.user.empty())
                ImGui::TextDisabled("%s", T("Nothing saved yet - use Save...", "Nic jeszcze nie zapisano - użyj Save..."));
            int pick = -1;
            for (size_t i = 0; i < fDetLib.user.size(); ++i)
            {
                ImGui::PushID(int(i));
                if (ImGui::Selectable(fDetLib.user[i].name.c_str(), fDetLib.user[i].name == cur)) pick = int(i);
                ImGui::PopID();
            }
            if (pick >= 0) detApply(fDetLib.user[pick].v);
            ImGui::EndCombo();
        }
        if (ImGui::IsItemActivated()) fDetLib.load();   // pick up what the other plugin saved
        tip(T("Detector settings you have saved (Gate, Cut, Window and everything in this window).\n"
              "Shared by Swarm Synth and Guitar to MIDI Live: what you save in one appears in the other.",
              "Zapisane ustawienia detektora (Gate, Cut, Window i wszystko w tym oknie).\n"
              "Wspólne dla Swarm Synth i Guitar to MIDI Live: to, co zapiszesz w jednej, pojawi się w drugiej."));
        ImGui::SameLine();
        if (ImGui::Button("Save..."))
        {
            const std::string base = (!cur.empty() && !fDetLib.isReserved(cur)) ? cur : std::string(T("My guitar", "Moja gitara"));
            std::snprintf(fDetName, sizeof(fDetName), "%s", base.c_str());
            fDetOpenSave = true;
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(fDetLib.find(cur) < 0);
        if (ImGui::Button("Delete")) { fDetDeleteName = cur; ImGui::OpenPopup("Delete detector settings?"); }
        ImGui::EndDisabled();
        ImGui::SameLine(0, S(16));
        if (ImGui::Button("Export...")) ImGui::OpenPopup("##detexport");
        tip(T("Saves detector settings to a CSV file (one row per setting) - e.g. to share them or keep them with a project.",
              "Zapisuje ustawienia detektora do pliku CSV (jeden wiersz na ustawienie) - np. żeby się nimi podzielić albo trzymać przy projekcie."));
        ImGui::SameLine();
        if (ImGui::Button("Import...")) startDetBrowse(kBrowseDetImport, std::string(), false);
        tip(T("Adds the settings from a CSV file to My settings. Duplicates are skipped; the same name with other values gets \"(2)\".\n"
              "A Swarm Synth preset file works too: its detector columns are used.",
              "Dodaje ustawienia z pliku CSV do Moich ustawień. Duplikaty są pomijane; ta sama nazwa z innymi wartościami dostaje \"(2)\".\n"
              "Plik presetów Swarm Synth też zadziała: zostaną wzięte jego kolumny detektora."));

        if (ImGui::BeginPopup("##detexport"))
        {
            if (ImGui::MenuItem(T("Current settings...", "Bieżące ustawienia...")))
                startDetBrowse(kBrowseDetExportCurrent, (cur.empty() ? std::string("detector-settings") : detFileSafe(cur)) + ".csv", true);
            if (ImGui::MenuItem(T("All my settings...", "Wszystkie moje ustawienia..."), nullptr, false, !fDetLib.user.empty()))
                startDetBrowse(kBrowseDetExportAll, "detector-presets.csv", true);
            ImGui::EndPopup();
        }

        if (fDetOpenSave) { ImGui::OpenPopup("Save detector settings"); fDetOpenSave = false; }
        if (ImGui::BeginPopupModal("Save detector settings", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted(T("Name:", "Nazwa:"));
            ImGui::SetNextItemWidth(S(300));
            if (ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
            const bool enter = ImGui::InputText("##detname", fDetName, sizeof(fDetName), ImGuiInputTextFlags_EnterReturnsTrue);
            const std::string name = detp::trim(fDetName);
            const bool reserved = fDetLib.isReserved(name);
            const int existing = fDetLib.find(name);
            if (reserved) ImGui::TextColored(ImVec4(1.f, 0.6f, 0.4f, 1.f), "%s", T("That is a built-in profile name - pick another one.", "To nazwa wbudowanego profilu - wybierz inną."));
            else if (existing >= 0) ImGui::TextColored(ImVec4(1.f, 0.85f, 0.4f, 1.f), "%s", T("A setting with this name exists and will be replaced.", "Ustawienie o tej nazwie istnieje i zostanie zastąpione."));
            else ImGui::TextDisabled("%s", T("Also available in the other plugin.", "Będzie dostępne także w drugiej wtyczce."));
            const bool can = !name.empty() && !reserved;
            ImGui::BeginDisabled(!can);
            const bool ok = ImGui::Button(existing >= 0 ? T("Replace", "Zastąp") : T("Save", "Zapisz"), ImVec2(S(100), 0));
            ImGui::EndDisabled();
            if (can && (ok || enter))
            {
                float d[detp::kCount];
                detGet(d);
                fDetStatus = fDetLib.store(name, d) ? std::string(T("Saved: ", "Zapisano: ")) + name
                                                    : std::string(T("Could not write the settings file", "Nie udało się zapisać pliku ustawień"));
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(T("Cancel", "Anuluj"), ImVec2(S(100), 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (ImGui::BeginPopupModal("Delete detector settings?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Text(T("Delete \"%s\"? It also disappears from the other plugin.", "Usunąć \"%s\"? Zniknie też z drugiej wtyczki."), fDetDeleteName.c_str());
            if (ImGui::Button(T("Delete", "Usuń"), ImVec2(S(100), 0)))
            {
                if (fDetLib.remove(fDetDeleteName)) fDetStatus = std::string(T("Deleted: ", "Usunięto: ")) + fDetDeleteName;
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button(T("Cancel", "Anuluj"), ImVec2(S(100), 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        if (!fDetStatus.empty())
        {
            ImGui::SameLine(0, S(16));
            ImGui::TextColored(ImVec4(0.75f, 0.9f, 0.75f, 1.f), "%s", fDetStatus.c_str());
        }
    }

    void detectorWindow()
    {
        if (fOpenDet) { ImGui::OpenPopup("Detector"); fOpenDet = false; fDetStatus.clear(); fDetLib.load(); }
        ImGui::SetNextWindowPos(ImVec2(getWidth() * 0.5f, getHeight() * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopup("Detector")) return;

        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColAccent), "DETECTOR - FINE TUNING");
        ImGui::SameLine();
        ImGui::TextDisabled("%s", T("Every setting is a trade-off: faster = more false notes, safer = slower.", "Każde ustawienie to kompromis: szybciej = więcej fałszywych nut, pewniej = wolniej."));

        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted("Profile:");
        for (int i = 0; i < kDetProfileCount; ++i)
        {
            ImGui::SameLine();
            if (ImGui::Button(kDetProfiles[i].name))
            {
                float v[kInputCount];
                std::memcpy(v, fP, sizeof(v));
                applyDetProfile(v, kDetProfiles[i]);
                applyValues(v);
            }
        }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", T("sets everything below and Window. Bridge notes is left as it is.", "ustawia wszystko poniżej oraz Window. Bridge notes zostaje bez zmian."));
        detPresetRow();
        ImGui::Separator();

        ImGui::BeginChild("##detL", ImVec2(S(456), S(520)));
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColAccent), "%s", T("A NEW PICK", "NOWE UDERZENIE"));
        detSetting(gDetRepick, "Pick sensitivity",
                   T("How far the level has to jump above what it was a moment ago to count as a new pick. Lower: catches soft and dense picking, but the wobble of a ringing string can pass for a pick. Higher: only clear picks; a quiet note after a loud one is then handled as legato, which is slower.", "O ile głośność musi skoczyć względem poziomu sprzed chwili, żeby uznać to za nowe uderzenie. "
                   "Mniej: łapie delikatne i gęste uderzenia, ale falowanie głośności wybrzmiewającej struny może udawać uderzenie. "
                   "Więcej: tylko wyraźne uderzenia; cicha nuta po głośnej przechodzi wtedy jako legato, czyli wolniej."));
        detSetting(gDetRefract, "Pick spacing",
                   T("The shortest gap between two picks. Lower: keeps up with tremolo picking and very fast runs. Higher: one pick is never counted twice.", "Najkrótszy odstęp między dwoma uderzeniami. Mniej: nadąża za tremolo i bardzo szybkimi biegnikami. "
                   "Więcej: jedno uderzenie nigdy nie zostanie policzone podwójnie."));
        detSetting(gDetClarity, "Clarity",
                   T("How periodic the signal has to be before a pitch reading is accepted. Lower: notes are caught sooner, also through a dirty attack, but noise and two strings at once give false notes more often. Higher: clean readings only; a note played over ringing strings starts later.", "Jak bardzo okresowy musi być sygnał, żeby odczyt wysokości został przyjęty. "
                   "Mniej: nuty łapią się szybciej, także przy brudnym ataku, ale szum i dwie struny naraz częściej dają fałszywą nutę. "
                   "Więcej: tylko czyste odczyty; nuta zagrana na tle wybrzmiewających strun startuje później."));
        detSetting(gDetLow, "Low note time",
                   T("How long, after a pick, a note more than a fifth below the previous one has to keep confirming. Right after a pick the old and the new string sound together and for a moment look like one low note (typically a false low note for about 30 ms). Lower: big leaps down are caught sooner. Higher: fewer false low notes.", "Jak długo po uderzeniu musi się potwierdzać nuta niższa od poprzedniej o więcej niż kwintę. "
                   "Tuż po uderzeniu stara i nowa struna brzmią razem i przez moment wyglądają jak jedna niska nuta "
                   "(typowo: fałszywa niska nuta na ok. 30 ms). Mniej: duże skoki w dół łapią się szybciej. Więcej: mniej fałszywych niskich nut."));
        detSetting(gDetProbation, "First-reading time",
                   T("The first reading of a note is made from a very short piece of signal and is sometimes wrong. For this long it is held loosely: if a lower note keeps showing up, it replaces the first reading quickly. 0 = the first reading is final at once.", "Pierwszy odczyt nuty powstaje z bardzo krótkiego kawałka sygnału i bywa błędny. Przez ten czas jest traktowany luźno: "
                   "jeśli uporczywie pojawia się niższa nuta, zastępuje go szybko. 0 = pierwszy odczyt obowiązuje od razu."));
        ImGui::EndChild();
        ImGui::SameLine(0, S(16));
        ImGui::BeginChild("##detR", ImVec2(S(456), S(520)));
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColAccent), "%s", T("NOTE CHANGES WITHOUT A PICK, AND TAILS", "ZMIANA NUTY BEZ UDERZENIA I KOŃCÓWKI"));
        detSetting(gDetLegato, "Legato time",
                   T("How long a new pitch with no pick (hammer-on, pull-off, slide, tapping) has to hold before the synth moves to it. Lower: legato responds at once, but the moment where both notes sound together produces short false notes. Higher: clean transitions, but slower. For comparison: a commercial guitar synth that was measured needed about 40-50 ms here.", "Jak długo nowa wysokość bez uderzenia (hammer-on, pull-off, slide, tapping) musi się utrzymać, zanim syntezator na nią przejdzie. "
                   "Mniej: legato reaguje od razu, ale chwila, w której brzmią obie nuty naraz, daje krótkie fałszywe dźwięki. "
                   "Więcej: czyste przejścia, ale wolniejsze. Dla porównania: zmierzony komercyjny syntezator gitarowy potrzebował tu ok. 40-50 ms."));
        detSetting(gDetHarm, "Octave time",
                   T("How long a jump of an octave or a fifth with no new pick has to last. Such a jump is far more often a loud harmonic than a new note. Lower: faster octave hammer-ons and harmonics. Higher: fewer octave jumps. A note far below the one playing waits just as long when its pick was not noticed.", "Jak długo skok o oktawę lub kwintę bez nowego uderzenia musi trwać. Taki skok to najczęściej głośna harmoniczna, a nie nowa nuta. "
                   "Mniej: szybsze oktawowe hammer-ony i flażolety. Więcej: mniej przeskoków o oktawę. "
                   "Tyle samo czeka nuta dużo niższa od granej, jeśli uderzenie nie zostało zauważone."));
        detSetting(gDetTail, "Tail hold",
                   T("Once a note has faded below this fraction of its peak, its pitch is frozen: in the tail the fundamental fades first and harmonics and neighbouring strings are left. Lower: bends are followed for longer. Higher: steadier tails.", "Gdy nuta ścichnie poniżej tej części swojego szczytu, wysokość zostaje zamrożona: w końcówce podstawa zanika pierwsza "
                   "i zostają harmoniczne oraz sąsiednie struny. Mniej: podciągnięcia są śledzone dłużej. Więcej: stabilniejsze końcówki."));
        detSetting(gDetBridge, "Bridge notes",
                   T("On: between notes the previous note keeps playing until the new one is recognised - no holes in the sound. Off: silence while a note is being recognised (usually 5-25 ms), every note starts on its own, clearer articulation.", "Włączone: między nutami gra poprzednia nuta, aż nowa zostanie rozpoznana - bez dziur w dźwięku. "
                   "Wyłączone: cisza na czas rozpoznawania (zwykle 5-25 ms), każda nuta zaczyna się osobno, wyraźniejsza artykulacja."));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kColDim));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + S(440));
        ImGui::TextUnformatted(T("On the main panel: Window (how many periods of the note the detector compares - the main speed / safety control), Gate (input threshold) and Cut (cutting off the quiet tail).", "Na panelu głównym: Window (ile okresów nuty detektor porównuje - główny suwak szybkość / pewność), "
                               "Gate (próg wejścia) i Cut (ucinanie cichej końcówki)."));
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::EndChild();

        ImGui::TextDisabled("%s", T("These settings are part of the preset and are saved with the project.", "Ustawienia są częścią presetu i zapisują się z projektem."));
        ImGui::SameLine(S(838));
        if (ImGui::Button("Close", ImVec2(S(90), 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    static std::string fileSafe(const std::string& s)
    {
        std::string o;
        for (char c : s) o += (std::strchr("\\/:*?\"<>|", c) != nullptr) ? '_' : c;
        return o;
    }

    void startExport(int mode, const char* defaultName)
    {
        FileBrowserOptions opts;
        opts.saving = true;
        opts.defaultName = defaultName;
        opts.title = "Export presets (CSV)";
        fBrowse = mode;
        if (!openFileBrowser(opts)) { fBrowse = kBrowseNone; setStatus("Could not open the file dialog"); }
    }

    void uiFileBrowserSelected(const char* filename) override
    {
        const int mode = fBrowse;
        fBrowse = kBrowseNone;
        if (filename == nullptr || filename[0] == '\0') return;
        std::string path = filename;
        if (mode == kBrowseDetImport || mode == kBrowseDetExportCurrent || mode == kBrowseDetExportAll) { detFileSelected(mode, path); return; }

        if (mode == kBrowseWav)
        {
            std::string text, err;
            if (!loadWavCycle(path.c_str(), text, err)) { fFormulaErr = err; setStatus("WAV not loaded: " + err); return; }
            if (setWave(text)) { syncEditor(); setStatus("Custom wave loaded from the WAV file"); }
            return;
        }

        if (mode == kBrowseImport)
        {
            std::vector<UserPreset> in;
            ImportReport rep;
            if (!readPresets(path.c_str(), in, &rep.badRows))
            {
                setStatus("Import failed: not a Swarm Synth preset CSV");
                return;
            }
            for (const UserPreset& p : in) fLib.addImported(p, rep);
            if (rep.added > 0) { fLib.save(); loadByName(rep.firstName); }
            char msg[160];
            std::snprintf(msg, sizeof(msg), "Imported %d (%d renamed), skipped %d duplicate%s%s",
                          rep.added, rep.renamed, rep.skipped, rep.skipped == 1 ? "" : "s",
                          rep.badRows > 0 ? ", some rows unreadable" : "");
            setStatus(msg);
            return;
        }

        const size_t slash = path.find_last_of("/\\");
        const size_t dot = path.find_last_of('.');
        if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) path += ".csv";

        std::vector<UserPreset> out;
        if (mode == kBrowseExportAll) out = fLib.user;
        else
        {
            UserPreset p;
            p.name = fCurName.empty() ? std::string("Custom") : fCurName;
            std::memcpy(p.v, fP, sizeof(p.v));
            p.wave = fWave;
            out.push_back(p);
        }
        char msg[96];
        std::snprintf(msg, sizeof(msg), "Exported %d preset%s", int(out.size()), out.size() == 1 ? "" : "s");
        setStatus(writePresets(path.c_str(), out) ? std::string(msg) : std::string("Export failed: could not write the file"));
    }

    // ----------------------------------------------------------------------------------------------------------------

    void onImGuiDisplay() override
    {
        if (fMatchDirty) matchPreset();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(getWidth(), getHeight()));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(kColBg));
        ImGui::Begin("SWSYN", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
                                       | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor();

        ImDrawList* const dl = ImGui::GetWindowDrawList();
        const ImVec2 wp = ImGui::GetWindowPos();
        const bool reshape = fP[gMode] > 0.5f;

        header(S(10));

        display(dl, wp, S(12), S(46), S(700), S(320));

        // ---- global
        const float gx = S(720), gy = S(46);
        panel(dl, wp, gx, gy, S(348), S(320), "HOW THE GUITAR DRIVES THE SYNTH");
        combo(gMode, kModeNames, kModeCount, gx + S(12), gy + S(27), S(190),
              T("Pitch tracking: a detector recognises the note and the oscillators play it. Clean, stable sound,\nbut a note can only start after about two of its periods (low E: about 25 ms, high strings: 5-8 ms).\n\nReshape (instant): the phase of the guitar signal itself drives the oscillators, sample by sample. The sound is there at once,\nbends and vibrato come through immediately, but for the first milliseconds after a pick\nthe pitch is still settling. When strings ring over each other the pitch wanders - Pitch tracking is better then.\n\nBoth modes are monophonic: play single notes.", "Pitch tracking: detektor rozpoznaje nutę, a oscylatory ją grają. Czysty, stabilny dźwięk,\n"
              "ale nuta może zabrzmieć dopiero po ok. dwóch jej okresach (niskie E: ok. 25 ms, wysokie struny: 5-8 ms).\n\n"
              "Reshape (instant): faza samego sygnału gitary napędza oscylatory próbka po próbce. Dźwięk jest od razu,\n"
              "podciągnięcia i vibrato przechodzą natychmiast, za to przez pierwsze milisekundy po uderzeniu\n"
              "wysokość dopiero się ustala. Gdy struny brzmią jedna na drugiej, wysokość pływa - wtedy lepszy jest Pitch tracking.\n\n"
              "Oba tryby są jednogłosowe: graj pojedyncze nuty."));
        toggle("MIDI out", gMidiOut, gx + S(222), gy + S(27),
               T("Sends the recognised notes as MIDI (note on/off with pick strength) to drive another instrument.\nIn REAPER: put the instrument after this plugin on the same track. Works in both modes,\nwith the detector's delay. No pitch bend - bends are not sent.", "Wysyła rozpoznane nuty jako MIDI (note on/off z siłą uderzenia), żeby sterować innym instrumentem.\n"
               "W REAPERZE: wstaw instrument za tą wtyczką na tym samym torze. Działa w obu trybach,\n"
               "z opóźnieniem detektora. Bez pitch bendu - podciągnięcia nie są wysyłane."));
        toggle("Quantize", gQuant, gx + S(12), gy + S(53),
               T("Like a keyboard: the pitch is rounded to the nearest semitone.\nSlightly out-of-tune strings, bends and finger vibrato disappear - a whole-tone bend\nsteps through the semitones (with Glide: smoothly).", "Jak na klawiszach: wysokość jest zaokrąglana do najbliższego półtonu.\n"
               "Znika lekkie rozstrojenie strun, podciągnięcia i vibrato palcem - podciągnięcie o cały ton\n"
               "przeskakuje po półtonach (z Glide: płynnie)."));
        toggle("Hold", gHold, gx + S(112), gy + S(53),
               T("Like a held key: the note keeps playing at full level even after the string has died away.\nIt ends with the next note, or when you mute the strings while they still ring\n(a sudden drop in level). A note that faded out quietly keeps playing until you play another one or switch Hold off.", "Jak przytrzymany klawisz: nuta gra pełnym poziomem także wtedy, gdy struna już wybrzmiała.\n"
               "Kończy ją następna nuta albo wytłumienie strun ręką, póki jeszcze brzmią\n"
               "(gwałtowny spadek głośności). Cicho wybrzmiała nuta gra dalej, aż zagrasz kolejną lub wyłączysz Hold."));
        dl->AddText(ImVec2(wp.x + gx + S(176), wp.y + gy + S(57)), kColDim, "like a keyboard");

        const float r1 = gy + S(80), r2 = gy + S(160), r3 = gy + S(240);
        knob("Mix", gMix, gx + S(9), r1, kColAccent, T("Balance of guitar and synth. 100% = synth only.", "Balans gitary i syntezatora. 100% = sam syntezator."));
        knob("Output", gOutput, gx + S(75), r1, kColAccent, T("Output volume.", "Głośność wyjściowa."));
        knob("Gate", gGate, gx + S(141), r1, kColAccent,
             T("The threshold above which the guitar triggers the synth (the white mark on the level meter).\nToo low: noise and hum trigger random notes. Too high: notes break off too early.", "Próg, powyżej którego gitara uruchamia syntezator (biała kreska na wskaźniku poziomu).\n"
             "Za nisko: szum i przydźwięk wyzwalają przypadkowe dźwięki. Za wysoko: nuty urywają się za wcześnie."));
        knob("Cut", gCut, gx + S(207), r1, kColAccent,
             T("Cuts the note once it has faded by this many dB from its loudest moment.\nIn the tail the fundamental fades first and harmonics and hum are left -\nthe pitch is no longer certain there. Closer to 0 dB = shorter, safer notes. -60 dB = almost to the end.\nWith Hold on the note keeps playing; only its pitch stops being tracked.", "Ucina nutę, gdy ścichnie o tyle dB względem swojego najgłośniejszego momentu.\n"
             "W końcówce wybrzmiewania podstawa zanika pierwsza i zostają harmoniczne oraz przydźwięk -\n"
             "wtedy wysokość przestaje być pewna. Bliżej 0 dB = krótsze, pewniejsze nuty. -60 dB = prawie do końca.\n"
             "Z włączonym Hold nuta gra dalej, tylko jej wysokość przestaje być śledzona."));
        knob("Window", gTrack, gx + S(273), r1, kColAccent,
             T("How many periods of the note the detector compares before it believes a pitch.\nLower = less delay, but below 1.0 it clearly gets notes wrong more often. Higher = safer and slower.\nThe \"note found after\" field shows the measured delay of the last note.", "Ile okresów nuty detektor porównuje, zanim uwierzy w wysokość.\n"
             "Mniej = krótsze opóźnienie, ale poniżej 1.0 wyraźnie częściej myli nuty. Więcej = pewniej i wolniej.\n"
             "Pole \"note found after\" pokazuje zmierzone opóźnienie ostatniej nuty."));

        knob("Spread", gSpDetune, gx + S(9), r2, kColAccent,
             T("Detunes the active voices symmetrically down and up, by at most this many cents.\nThis is the \"swarm\" (unison) effect. Adds to each voice's own Detune.\nNeighbours in the stereo field get opposite detune, so both sides sound full.", "Rozstraja włączone głosy symetrycznie w dół i w górę, maksymalnie o tyle centów.\n"
             "To daje efekt \"roju\" (unison). Dodaje się do Detune poszczególnych głosów.\n"
             "Sąsiedzi w stereo dostają przeciwne odstrojenie, żeby obie strony brzmiały pełno."));
        knob("Width", gSpPan, gx + S(75), r2, kColAccent, T("Spreads the voices from left to right. Adds to each voice's own Pan.", "Rozkłada głosy od lewej do prawej. Dodaje się do Pan poszczególnych głosów."));
        knob("Drift", gDrift, gx + S(141), r2, kColAccent,
             T("Every voice slowly wanders in pitch on its own, by at most this many cents.\nThe beating between voices stops being even and repetitive - the sound is alive, as in drones.\nSpread sets a fixed detune, Drift adds movement.", "Każdy głos powoli i niezależnie pływa w wysokości, maksymalnie o tyle centów.\n"
             "Dudnienia między głosami przestają być równe i powtarzalne - brzmienie żyje, jak w dronach.\n"
             "Spread ustawia stałe odstrojenie, Drift dokłada ruch."));
        ImGui::SetCursorPos(ImVec2(gx + S(214), r2 + S(22)));
        if (ImGui::Button("Detector...", ImVec2(S(122), S(26)))) fOpenDet = true;
        tip(T("Detailed pitch detector settings, each with an explanation of what it does and why,\nplus ready-made profiles: Fast solo, Balanced, Careful / let ring.", "Dokładne ustawienia detektora wysokości z opisem, co każde robi i dlaczego,\n"
            "oraz gotowe profile: Fast solo, Balanced, Careful / let ring."));
        dl->AddText(ImVec2(wp.x + gx + S(214), wp.y + r2 + S(54)), kColDim, "fine-tune the tracking");

        knob("Glide", gGlide, gx + S(9), r3, kColAccent,
             T("Pitch tracking: a smooth slide between notes (portamento).\nReshape: smoothing of the pitch - more = calmer attack, but slower response.", "Pitch tracking: płynne przejście między nutami (portamento).\n"
             "Reshape: wygładzanie wysokości - więcej = spokojniejszy atak, ale wolniejsza reakcja."));
        knob("Vibrato", gVibDepth, gx + S(75), r3, kColAccent,
             T("Vibrato depth in cents (all voices together). It fades in a moment after the note starts,\nas on a keyboard lead. 0 = off.", "Głębokość wibrata w centach (wszystkie głosy razem). Wchodzi płynnie chwilę po starcie nuty,\n"
             "jak na klawiszowym leadzie. 0 = wyłączone."));
        knob("Vib rate", gVibRate, gx + S(141), r3, kColAccent, T("Vibrato speed.", "Szybkość wibrata."), fP[gVibDepth] < 0.01f);
        knob("Scatter", gScatter, gx + S(207), r3, kColAccent,
             T("Every new note starts with the voices scattered by up to this many cents (1200 = an octave),\n"
               "each somewhere else, and they slide together into the note - each in its own time.\n"
               "A cloud that resolves into one sound, like a wall of free-running oscillators being tuned. 0 = off.",
               "Każda nowa nuta zaczyna się od głosów rozrzuconych maksymalnie o tyle centów (1200 = oktawa),\n"
               "każdy gdzie indziej, i zjeżdżają się w nutę - każdy we własnym tempie.\n"
               "Chmura, która rozwiązuje się w jeden dźwięk, jak ściana wolnych oscylatorów w trakcie strojenia. 0 = wyłączone."));
        knob("Converge", gConverge, gx + S(273), r3, kColAccent,
             T("How long the scattered voices take to come together (individual voices: about half to one and a half times this).",
               "Ile trwa zjeżdżanie się rozrzuconych głosów (poszczególne głosy: mniej więcej od połowy do półtora raza tyle)."),
             fP[gScatter] < 0.5f);

        // ---- voice tabs
        const float ty = S(374);
        {
            ImGui::SetCursorPos(ImVec2(S(12), ty));
            const bool all = fSel < 0;
            ImGui::PushStyleColor(ImGuiCol_Button, all ? ImVec4(0.30f, 0.42f, 0.58f, 1.f) : ImGui::GetStyle().Colors[ImGuiCol_Button]);
            if (ImGui::Button("ALL VOICES", ImVec2(S(104), S(30)))) fSel = -1;
            ImGui::PopStyleColor();
            tip(T("The knobs below set all voices at once.\nA ~ next to a value = the voices have different settings.", "Pokrętła poniżej ustawiają wszystkie głosy naraz.\nZnak ~ przy wartości = głosy mają różne ustawienia."));

            for (int v = 0; v < kNumVoices; ++v)
            {
                const float tx = S(126) + S(118) * float(v);
                const int onIdx = voiceIndex(v, vOn);
                bool on = fP[onIdx] > 0.5f;
                ImGui::PushID(1000 + v);
                ImGui::SetCursorPos(ImVec2(tx, ty + S(4)));
                ImGui::PushStyleColor(ImGuiCol_CheckMark, ImGui::ColorConvertU32ToFloat4(kVoiceColors[v]));
                if (ImGui::Checkbox("##on", &on)) setOne(onIdx, on ? 1.f : 0.f);
                ImGui::PopStyleColor();
                tip(T("Voice on / off", "Włącz / wyłącz głos"));

                const bool sel = fSel == v;
                ImGui::SetCursorPos(ImVec2(tx + S(28), ty));
                const ImVec2 bp = ImGui::GetCursorScreenPos();
                ImGui::PushStyleColor(ImGuiCol_Button, sel ? ImVec4(0.30f, 0.42f, 0.58f, 1.f) : ImGui::GetStyle().Colors[ImGuiCol_Button]);
                ImGui::PushStyleColor(ImGuiCol_Text, on ? ImGui::ColorConvertU32ToFloat4(kColText) : ImGui::ColorConvertU32ToFloat4(kColDim));
                char lbl[24];
                std::snprintf(lbl, sizeof(lbl), "Voice %d", v + 1);
                if (ImGui::Button(lbl, ImVec2(S(82), S(30)))) fSel = v;
                ImGui::PopStyleColor(2);
                dl->AddRectFilled(ImVec2(bp.x + S(4), bp.y + S(25)), ImVec2(bp.x + S(78), bp.y + S(28)),
                                  on ? kVoiceColors[v] : withAlpha(kVoiceColors[v], 0.3f), S(1.5f));
                ImGui::PopID();
            }
        }

        // ---- voice knobs
        const float vy = S(412);
        const int dv = fSel < 0 ? 0 : fSel;
        const ImU32 vc = fSel < 0 ? kColAccent : kVoiceColors[fSel];
        const auto VI = [dv](int p) { return voiceIndex(dv, p); };
        panel(dl, wp, S(12), vy, S(1056), S(112), nullptr);
        dl->AddText(ImVec2(wp.x + S(24), wp.y + vy + S(6)), kColAccent, "LEVEL");
        dl->AddText(ImVec2(wp.x + S(170), wp.y + vy + S(6)), kColAccent, "OSCILLATOR");
        dl->AddText(ImVec2(wp.x + S(430), wp.y + vy + S(6)), kColAccent, "PITCH RELATIVE TO THE PLAYED NOTE");
        const float ky = vy + S(28);
        knob("Level", VI(vLevel), S(18), ky, vc, T("Level of the voice.", "Głośność głosu."));
        knob("Pan", VI(vPan), S(84), ky, vc, T("Position of the voice in the stereo field (adds to Spread > Width).", "Położenie głosu w stereo (dodaje się do Spread > Pan)."));
        {
            const int wIdx = VI(vWave);
            centeredText(dl, wp.x + S(164) + S(78), wp.y + ky, kColDim, "Wave");
            combo(wIdx, kWaveNames, kWaveCount, S(164), ky + S(26), S(156),
                  T("The waveform of the oscillator.\nThe noises have no pitch - they add \"breath\".\nCustom wave = your own wave from the editor.", "Kształt fali oscylatora.\nSzumy nie mają wysokości - dodają \"oddech\".\nCustom wave = własna fala z edytora."));
            ImGui::PushID(wIdx + 5000);
            ImGui::SetCursorPos(ImVec2(S(164), ky + S(52)));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(4), S(1)));
            if (ImGui::Button("Edit custom wave...", ImVec2(S(156), S(20))))
            {
                beginEdit(wIdx); setValue(wIdx, float(wCustom)); endEdit(wIdx);
                fOpenWave = true;
            }
            ImGui::PopStyleVar();
            tip(T("Sets the wave to Custom and opens the editor: points, a math formula, or one cycle from a WAV file.", "Ustawia falę na Custom i otwiera edytor: punkty, wzór matematyczny albo jeden okres z pliku WAV."));
            ImGui::PopID();
        }
        {
            const int cur = int(fP[VI(vWave)] + 0.5f);
            knob("Width", VI(vWidth), S(336), ky, vc, T("Pulse width of the Square / pulse wave. 50% = square, extreme values = thin, nasal sound.", "Szerokość impulsu fali Square / pulse. 50% = prostokąt, skrajne wartości = cienki, nosowy dźwięk."),
                 cur != wPulse && !mixed(VI(vWave)));
        }
        knob("Octave", VI(vOct), S(424), ky, vc, T("Shifts the voice by whole octaves.", "Przesunięcie głosu o całe oktawy."));
        knob("Semitones", VI(vSemi), S(490), ky, vc, T("Shift in semitones. +7 = a fifth, +12 = an octave, -5 = a fourth down.", "Przesunięcie o półtony. +7 = kwinta, +12 = oktawa, -5 = kwarta w dół."));
        knob("Detune", VI(vDetune), S(556), ky, vc, T("Detune of the voice in cents (adds to Spread).", "Odstrojenie głosu w centach (dodaje się do Spread > Detune)."));
        {
            // quick intervals
            static const struct { const char* label; float oct, semi; } kQuick[] = {
                { "-2 oct", -2, 0 }, { "-1 oct", -1, 0 }, { "unison", 0, 0 }, { "+5th", 0, 7 }, { "+1 oct", 1, 0 }, { "+oct+5th", 1, 7 }, { "+2 oct", 2, 0 },
            };
            dl->AddText(ImVec2(wp.x + S(640), wp.y + ky + S(2)), kColDim, "Quick:");
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(6), S(3)));
            float qx = S(640);
            for (size_t q = 0; q < sizeof(kQuick) / sizeof(kQuick[0]); ++q)
            {
                ImGui::PushID(7000 + int(q));
                ImGui::SetCursorPos(ImVec2(qx, ky + S(24)));
                if (ImGui::Button(kQuick[q].label))
                {
                    beginEdit(VI(vOct)); setValue(VI(vOct), kQuick[q].oct); endEdit(VI(vOct));
                    beginEdit(VI(vSemi)); setValue(VI(vSemi), kQuick[q].semi); endEdit(VI(vSemi));
                }
                qx += ImGui::GetItemRectSize().x + S(4);
                ImGui::PopID();
            }
            ImGui::PopStyleVar();
            dl->AddText(ImVec2(wp.x + S(640), wp.y + ky + S(56)), kColDim,
                        fSel < 0 ? "applies to all voices - pick a voice tab to set just one" : "applies to the selected voice");
        }

        // ---- envelope
        const float by = S(532);
        const bool follow = fP[gEnvMode] < 0.5f;
        panel(dl, wp, S(12), by, S(420), S(112), "LOUDNESS");
        combo(gEnvMode, kEnvModeNames, kEnvModeCount, S(96), by + S(3), S(150),
              T("Follow guitar: the synth is as loud as you play and fades with the string.\nSynth ADSR: every pick starts the synth's own envelope (Attack, Decay, Sustain, Release);\nthe note holds for as long as the guitar is above the Gate threshold.", "Follow guitar: syntezator jest tak głośny, jak grasz, i wybrzmiewa razem ze struną.\n"
              "Synth ADSR: każde uderzenie uruchamia własną obwiednię syntezatora (Attack, Decay, Sustain, Release);\n"
              "nuta trzyma się, dopóki gitara jest powyżej progu Gate."));
        const float sy = by + S(28);
        knob("Attack", gAttack, S(18), sy, kColAccent, T("Rise time after a pick.", "Czas narastania po uderzeniu."), follow);
        knob("Decay", gDecay, S(84), sy, kColAccent, T("Time to fall to the Sustain level.", "Czas opadania do poziomu Sustain."), follow);
        knob("Sustain", gSustain, S(150), sy, kColAccent, T("The level the note holds at while the string sounds.", "Poziom, na którym nuta się trzyma, dopóki struna brzmi."), follow);
        knob("Release", gRelease, S(216), sy, kColAccent, T("Fade-out time after the guitar has dropped below the Gate threshold.", "Czas wygasania po tym, jak gitara ucichnie poniżej progu Gate."), follow);
        knob("Dynamics", gDyn, S(282), sy, kColAccent, T("How much the pick strength affects the loudness. 0% = every note equally loud.", "Jak mocno siła uderzenia wpływa na głośność. 0% = każda nuta tak samo głośna."), follow);
        knob("Compress", gComp, S(348), sy, kColAccent,
             T("Follow guitar only: evens out the synth's loudness while the string fades.\n0% = the synth fades with the guitar. 100% = it holds one level until the note is cut (Cut / Gate).", "Tylko Follow guitar: wyrównuje głośność syntezatora w trakcie wybrzmiewania struny.\n"
             "0% = syntezator cichnie razem z gitarą. 100% = trzyma jeden poziom aż do ucięcia nuty (Cut / Gate)."), !follow);
        dl->AddText(ImVec2(wp.x + S(256), wp.y + by + S(6)), kColDim, follow ? "ADSR knobs are off" : "Compress is off");

        // ---- filter
        const float fx = S(440);
        const bool fon = fP[gFltOn] > 0.5f;
        panel(dl, wp, fx, by, S(628), S(112), "FILTER");
        toggle("on  (ladder low-pass)", gFltOn, fx + S(62), by + S(3), T("Ladder-type low-pass filter on the sum of the voices.", "Filtr dolnoprzepustowy typu ladder na sumie głosów."));
        knob("Cutoff", gCutoff, fx + S(6), sy, kColAccent, T("Cutoff frequency.", "Częstotliwość odcięcia."), !fon);
        knob("Reso", gReso, fx + S(72), sy, kColAccent, T("Resonance - a boost at the cutoff frequency. Near 100% the filter starts to whistle by itself.", "Rezonans - podbicie przy częstotliwości odcięcia. Blisko 100% filtr zaczyna sam gwizdać."), !fon);
        knob("Slope", gSlope, fx + S(138), sy, kColAccent, T("Filter slope: 6 / 12 / 18 / 24 dB per octave.", "Stromość filtra: 6 / 12 / 18 / 24 dB na oktawę."), !fon);
        knob("Drive", gDrive, fx + S(204), sy, kColAccent, T("Overdrive at the filter input.", "Przesterowanie na wejściu filtra."), !fon);
        knob("Env", gFltEnv, fx + S(270), sy, kColAccent, T("By how many octaves the filter envelope opens (plus) or closes (minus) the filter on every note.", "O ile oktaw obwiednia filtra otwiera (plus) albo zamyka (minus) filtr przy każdej nucie."), !fon);
        knob("Attack", gFltAtk, fx + S(336), sy, kColAccent, T("Rise time of the filter envelope.", "Czas narastania obwiedni filtra."), !fon);
        knob("Decay", gFltDec, fx + S(402), sy, kColAccent, T("Fall time of the filter envelope.", "Czas opadania obwiedni filtra."), !fon);
        knob("Key", gKey, fx + S(468), sy, kColAccent, T("How much the cutoff follows the pitch of the note. 100% = the same tone colour all over the neck.", "O ile odcięcie podąża za wysokością nuty. 100% = barwa taka sama na całym gryfie."), !fon);
        knob("Velocity", gVelFlt, fx + S(534), sy, kColAccent,
             T("By how many octaves a hard pick opens the filter. Like velocity on a synth: soft = dark, hard = bright.", "O ile oktaw mocne uderzenie otwiera filtr. Jak velocity w syntezatorze: delikatnie = ciemno, mocno = jasno."), !fon);

        waveEditor();
        detectorWindow();

        // footer: status message, or the mouse hints
        {
            const bool st = !fStatus.empty() && ImGui::GetTime() < fStatusUntil;
            dl->AddText(ImVec2(wp.x + S(16), wp.y + S(650)), st ? IM_COL32(190, 230, 190, 255) : kColDim,
                        st ? fStatus.c_str() : "Knobs: drag up/down, Shift = fine, double-click = default, mouse wheel.     Hold the mouse over a control for an explanation.");
        }

        ImGui::End();
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SwarmSynthUI)
};

UI* createUI() { return new SwarmSynthUI(); }

END_NAMESPACE_DISTRHO
