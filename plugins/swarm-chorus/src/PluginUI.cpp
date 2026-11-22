// Swarm Chorus - user interface (Dear ImGui)
#include "PresetIO.hpp"

#include "DistrhoUI.hpp"
#include "DearImGui/imgui_internal.h"

#include <chrono>
#include <random>

START_NAMESPACE_DISTRHO

using namespace roj;

static const ImU32 kVoiceColors[kNumVoices] = {
    IM_COL32(230, 159, 0, 255),    // orange
    IM_COL32(86, 180, 233, 255),   // sky blue
    IM_COL32(0, 200, 150, 255),    // green
    IM_COL32(240, 228, 66, 255),   // yellow
    IM_COL32(245, 95, 75, 255),    // red
    IM_COL32(214, 130, 190, 255),  // purple
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

class RojUI : public UI
{
    float fP[kParamCount];
    float fScale = 1.f;
    int fSel = -1; // -1 = edit all voices together

    Library fLib;
    std::string fCurName;
    bool fMatchDirty = true;
    std::string fStatus;
    double fStatusUntil = 0.0;
    enum { kBrowseNone, kBrowseExportCurrent, kBrowseExportAll, kBrowseImport };
    int fBrowse = kBrowseNone;
    char fNameBuf[64] = "";
    bool fOpenSave = false;
    float fDragNorm = 0.f;

    // dragging a curve on the graph
    int fDragVoice = -1;
    float fDragStart0 = 0.f, fDragPhase0 = 0.f, fDragMin0 = 0.f, fDragMax0 = 0.f;
    float fDragWin = 1.f, fDragSpan = 1.f, fDragRate = 1.f;

    // custom wave: its text form, the table compiled from it, and the editor's working state
    std::string fWave = kDefaultWave;
    float fWaveTable[kWaveSize];
    bool fOpenWave = false;
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

    std::mt19937 fRng { uint32_t(std::chrono::steady_clock::now().time_since_epoch().count()) };

    float S(float x) const { return x * fScale; }

public:
    RojUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
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
        float table[kWaveSize];
        if (!compileWave(value, table)) return;
        std::memcpy(fWaveTable, table, sizeof(table));
        fWave = value;
        fMatchDirty = true;
        if (fNodeDrag < 0) syncEditor();
        repaint();
    }

    // compiles the text into the lookup table and hands it to the audio side; false if the text is invalid
    bool setWave(const std::string& text)
    {
        float table[kWaveSize];
        std::string err;
        if (!compileWave(text.c_str(), table, &err)) { fFormulaErr = err; return false; }
        std::memcpy(fWaveTable, table, sizeof(table));
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
        else if (fWave.size() > 2)
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
        if (isVoiceParam(idx) && (idx - kGlobalCount) % kVoiceParamCount == vDiv)
            std::snprintf(b, n, "%s", kDivNames[std::min(int(kDivCount) - 1, std::max(0, int(v + 0.5f)))]);
        else if (isVoiceParam(idx) && (idx - kGlobalCount) % kVoiceParamCount == vPan)
        {
            if (std::fabs(v) < 0.5f) std::snprintf(b, n, "C");
            else std::snprintf(b, n, "%s%.0f", v < 0 ? "L" : "R", std::fabs(v));
        }
        else if (!std::strcmp(u, "Hz"))
        {
            if (v < 10.f) std::snprintf(b, n, "%.2f Hz", v);
            else if (v < 100.f) std::snprintf(b, n, "%.1f Hz", v);
            else if (v < 1000.f) std::snprintf(b, n, "%.0f Hz", v);
            else std::snprintf(b, n, "%.2f kHz", v * 0.001f);
        }
        else if (!std::strcmp(u, "ms"))
        {
            if (v < 10.f) std::snprintf(b, n, "%.2f ms", v);
            else if (v < 100.f) std::snprintf(b, n, "%.1f ms", v);
            else std::snprintf(b, n, "%.0f ms", v);
        }
        else if (!std::strcmp(u, "%"))   std::snprintf(b, n, "%.0f %%", v);
        else if (!std::strcmp(u, "dB"))  std::snprintf(b, n, "%+.1f dB", v);
        else if (!std::strcmp(u, "ct"))  std::snprintf(b, n, "%+.1f ct", v);
        else if (!std::strcmp(u, "deg")) std::snprintf(b, n, "%.0f°", v);
        else if (!std::strcmp(u, "oct")) std::snprintf(b, n, "%+.2f oct", v);
        else if (!std::strcmp(u, "x"))   std::snprintf(b, n, "%.2fx", v);
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

    void knob(const char* label, int idx, float x, float y, ImU32 accent, const char* tip)
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
            setValue(idx, fromNorm(d, toNorm(d, fP[idx]) + io.MouseWheel * (io.KeyShift ? 0.004f : 0.02f)));
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

        centeredText(dl, cx, p0.y, hovered || active ? kColText : kColDim, label);
        dl->PathArcTo(c, r, a0, a1, 40);
        dl->PathStroke(kColTrack, 0, S(3.f));
        if (std::fabs(an - from) > 0.01f)
        {
            dl->PathArcTo(c, r, std::min(from, an), std::max(from, an), 40);
            dl->PathStroke(accent, 0, S(3.f));
        }
        dl->AddCircleFilled(c, r - S(5), active ? IM_COL32(86, 92, 106, 255) : (hovered ? IM_COL32(70, 75, 88, 255) : IM_COL32(56, 60, 70, 255)), 32);
        const ImVec2 dir(std::cos(an), std::sin(an));
        dl->AddLine(ImVec2(c.x + dir.x * (r - S(12)), c.y + dir.y * (r - S(12))),
                    ImVec2(c.x + dir.x * (r - S(6)), c.y + dir.y * (r - S(6))), kColText, S(2.f));

        char val[48], shown[56];
        formatValue(idx, fP[idx], val, sizeof(val));
        std::snprintf(shown, sizeof(shown), "%s%s", mixed(idx) ? "~ " : "", val);
        centeredText(dl, cx, c.y + r + S(3), kColText, shown);

        if (hovered && !active && tip != nullptr && GImGui->HoveredIdTimer > 0.6f)
            ImGui::SetTooltip("%s", tip);
        ImGui::PopID();
    }

    void toggle(const char* label, int idx, float x, float y, const char* tip)
    {
        ImGui::SetCursorPos(ImVec2(x, y));
        bool on = fP[idx] > 0.5f;
        ImGui::PushID(idx);
        if (ImGui::Checkbox(label, &on)) setOne(idx, on ? 1.f : 0.f);
        if (tip != nullptr && ImGui::IsItemHovered() && GImGui->HoveredIdTimer > 0.6f) ImGui::SetTooltip("%s", tip);
        ImGui::PopID();
    }

    void panel(ImDrawList* dl, const ImVec2& wp, float x, float y, float w, float h, const char* title)
    {
        dl->AddRectFilled(ImVec2(wp.x + x, wp.y + y), ImVec2(wp.x + x + w, wp.y + y + h), kColPanel, S(6));
        if (title != nullptr) dl->AddText(ImVec2(wp.x + x + S(10), wp.y + y + S(6)), kColAccent, title);
    }

    // ----------------------------------------------------------------------------------------------------------------
    // graph: delay time of every voice against time since the LFOs (re)started

    void graph(ImDrawList* dl, const ImVec2& wp, float x, float y, float w, float h)
    {
        const ImVec2 a(wp.x + x, wp.y + y), b(wp.x + x + w, wp.y + y + h);
        dl->AddRectFilled(a, b, kColPanel, S(6));

        const float nowW = S(46);
        const ImVec2 g0(a.x + S(46), a.y + S(26)), g1(b.x - nowW - S(10), b.y - S(24));
        dl->AddRectFilled(g0, g1, kColGraph);
        dl->AddText(ImVec2(a.x + S(10), a.y + S(6)), kColAccent, "DELAY TIME OF EACH VOICE");
        const float bpm = fP[kOutBpm] > 1.f ? fP[kOutBpm] : 120.f;
        char sub[160];
        if (fP[gSync] > 0.5f)
            std::snprintf(sub, sizeof(sub), "ms, against seconds since the LFOs started  -  synced to %.1f BPM", bpm);
        else
            std::snprintf(sub, sizeof(sub), "ms, against seconds since the LFOs started");
        dl->AddText(ImVec2(a.x + S(200), a.y + S(6)), kColDim, sub);

        Eff eff[kNumVoices];
        const int n = computeEff(fP, eff, bpm);

        // time window
        float win = 2.f;
        for (int v = 0; v < kNumVoices; ++v)
            if (eff[v].on)
                win = std::max(win, eff[v].start + fP[gFade] * 0.001f + 2.f / std::max(0.05f, eff[v].rate));
        win = std::min(30.f, win);

        // sample the curves
        enum { kPts = 360 };
        static float ys[kNumVoices][kPts];
        float lo = 1e9f, hi = -1e9f;
        for (int v = 0; v < kNumVoices; ++v)
        {
            if (!eff[v].on) continue;
            for (int i = 0; i < kPts; ++i)
            {
                ys[v][i] = voiceDelayAt(fP, eff[v], v, win * float(i) / float(kPts - 1), fWaveTable);
                lo = std::min(lo, ys[v][i]); hi = std::max(hi, ys[v][i]);
            }
            const float now = fP[kOutNow + v];
            if (now > 0.f) { lo = std::min(lo, now); hi = std::max(hi, now); }
        }
        if (n == 0) { lo = 0.f; hi = 20.f; }
        if (hi - lo < 1.f) { const float m = 0.5f * (hi + lo); lo = m - 0.5f; hi = m + 0.5f; }
        const float pad = (hi - lo) * 0.08f;
        lo = std::max(0.f, lo - pad); hi += pad;

        const float gw = g1.x - g0.x, gh = g1.y - g0.y;
        const auto X = [&](float t) { return g0.x + gw * t / win; };
        const auto Y = [&](float ms) { return g1.y - gh * (ms - lo) / (hi - lo); };

        // direct manipulation: grab the nearest curve
        ImGui::SetCursorScreenPos(g0);
        ImGui::InvisibleButton("##graph", ImVec2(gw, gh));
        const ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsItemActivated())
        {
            const int i = std::min(int(kPts) - 1, std::max(0, int((io.MousePos.x - g0.x) / gw * float(kPts - 1) + 0.5f)));
            float best = S(16);
            fDragVoice = -1;
            for (int v = 0; v < kNumVoices; ++v)
            {
                if (!eff[v].on) continue;
                const float dist = std::fabs(Y(ys[v][i]) - io.MousePos.y);
                if (dist < best) { best = dist; fDragVoice = v; }
            }
            if (fDragVoice >= 0)
            {
                const int v = fDragVoice;
                fSel = v;
                fDragStart0 = fP[voiceIndex(v, vStart)]; fDragPhase0 = fP[voiceIndex(v, vPhase)];
                fDragMin0 = fP[voiceIndex(v, vDelayMin)]; fDragMax0 = fP[voiceIndex(v, vDelayMax)];
                fDragWin = win; fDragSpan = hi - lo; fDragRate = std::max(0.01f, eff[v].rate);
                static const int kDragParams[4] = { vStart, vPhase, vDelayMin, vDelayMax };
                for (int pp : kDragParams) editParameter(voiceIndex(v, pp), true);
            }
        }
        if (ImGui::IsItemActive() && fDragVoice >= 0)
        {
            const int v = fDragVoice;
            const ImVec2 d = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.f);
            const float dt = d.x / gw * fDragWin;
            float start = fDragStart0, phase = fDragPhase0;
            if (io.KeyShift)
            {
                phase = std::fmod(fDragPhase0 - dt * fDragRate * 360.f, 360.f);
                if (phase < 0.f) phase += 360.f;
            }
            else
                start = std::min(5000.f, std::max(0.f, fDragStart0 + dt * 1000.f));
            float dms = -d.y / gh * fDragSpan;
            dms = std::min(60.f - std::max(fDragMin0, fDragMax0), std::max(0.1f - std::min(fDragMin0, fDragMax0), dms));
            const int idx[4] = { voiceIndex(v, vStart), voiceIndex(v, vPhase), voiceIndex(v, vDelayMin), voiceIndex(v, vDelayMax) };
            const float val[4] = { start, phase, fDragMin0 + dms, fDragMax0 + dms };
            for (int k2 = 0; k2 < 4; ++k2)
                if (fP[idx[k2]] != val[k2]) { fP[idx[k2]] = val[k2]; setParameterValue(idx[k2], val[k2]); fMatchDirty = true; }
        }
        if (ImGui::IsItemDeactivated() && fDragVoice >= 0)
        {
            static const int kDragParams[4] = { vStart, vPhase, vDelayMin, vDelayMax };
            for (int pp : kDragParams) editParameter(voiceIndex(fDragVoice, pp), false);
            fDragVoice = -1;
        }

        // grid + axis labels
        char txt[48];
        for (int i = 0; i <= 4; ++i)
        {
            const float ms = lo + (hi - lo) * float(i) / 4.f;
            const float yy = std::floor(Y(ms)) + 0.5f;
            dl->AddLine(ImVec2(g0.x, yy), ImVec2(g1.x, yy), kColGrid);
            std::snprintf(txt, sizeof(txt), hi - lo < 8.f ? "%.1f" : "%.0f", ms);
            const ImVec2 sz = ImGui::CalcTextSize(txt);
            dl->AddText(ImVec2(g0.x - sz.x - S(6), yy - sz.y * 0.5f), kColDim, txt);
        }
        float step = 0.25f;
        static const float kSteps[] = { 0.25f, 0.5f, 1.f, 2.f, 5.f, 10.f };
        for (float sv : kSteps) { step = sv; if (win / sv <= 9.f) break; }
        for (float t = 0.f; t <= win + 0.001f; t += step)
        {
            const float xx = std::floor(X(t)) + 0.5f;
            dl->AddLine(ImVec2(xx, g0.y), ImVec2(xx, g1.y), kColGrid);
            std::snprintf(txt, sizeof(txt), step < 1.f ? "%.2f s" : "%.0f s", t);
            const ImVec2 sz = ImGui::CalcTextSize(txt);
            if (xx + sz.x * 0.5f < g1.x + S(8))
                dl->AddText(ImVec2(xx - sz.x * 0.5f, g1.y + S(4)), kColDim, txt);
        }

        // curves (selected voice on top)
        dl->PushClipRect(g0, g1, true);
        for (int pass = 0; pass < 2; ++pass)
            for (int v = 0; v < kNumVoices; ++v)
            {
                if (!eff[v].on) continue;
                const bool sel = fSel == v;
                if ((pass == 1) != sel) continue;
                const ImU32 col = (fSel < 0 || sel) ? kVoiceColors[v] : withAlpha(kVoiceColors[v], 0.45f);
                static ImVec2 pts[kPts];
                for (int i = 0; i < kPts; ++i)
                    pts[i] = ImVec2(X(win * float(i) / float(kPts - 1)), Y(ys[v][i]));
                dl->AddPolyline(pts, kPts, col, 0, S(sel ? 3.f : 2.f));
                // where this voice starts moving
                if (eff[v].start > 0.001f && eff[v].start < win)
                    dl->AddCircleFilled(ImVec2(X(eff[v].start), Y(voiceDelayAt(fP, eff[v], v, eff[v].start, fWaveTable))), S(4), col);
            }
        dl->PopClipRect();

        if (n == 0)
            centeredText(dl, 0.5f * (g0.x + g1.x), 0.5f * (g0.y + g1.y) - S(8), kColDim, "All voices are off - switch one on below");

        // live position of every voice
        const float nx0 = g1.x + S(12), nx1 = b.x - S(10);
        dl->AddRectFilled(ImVec2(nx0, g0.y), ImVec2(nx1, g1.y), kColGraph);
        centeredText(dl, 0.5f * (nx0 + nx1), g1.y + S(4), kColDim, "now");
        int k = 0;
        for (int v = 0; v < kNumVoices; ++v)
        {
            if (!eff[v].on) continue;
            const float now = fP[kOutNow + v];
            if (now > 0.f)
            {
                const float yy = std::min(g1.y - S(4), std::max(g0.y + S(4), Y(now)));
                const float xx = nx0 + (nx1 - nx0) * (float(k) + 0.5f) / float(std::max(1, n));
                dl->AddCircleFilled(ImVec2(xx, yy), S(3.5f), kVoiceColors[v]);
            }
            ++k;
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
        ImGui::TextColored(ImVec4(0.47f, 0.78f, 1.f, 1.f), "SWARM CHORUS");

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
                startExport(kBrowseExportCurrent, fCurName.empty() ? "swarm-preset.csv" : (fileSafe(fCurName) + ".csv").c_str());
            if (ImGui::MenuItem("All user presets...", nullptr, false, !fLib.user.empty()))
                startExport(kBrowseExportAll, "swarm-user-presets.csv");
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
        if (ImGui::IsItemHovered() && GImGui->HoveredIdTimer > 0.6f)
            ImGui::SetTooltip("Losuje głosy, kształty, tempa i rozrzuty. Głośność, EQ i Warmth zostają bez zmian.");
        ImGui::SameLine(0, S(16));
        for (int slot = 0; slot < 2; ++slot)
        {
            if (slot == 1) ImGui::SameLine(0, S(2));
            ImGui::PushStyleColor(ImGuiCol_Button, fABCur == slot ? ImVec4(0.30f, 0.42f, 0.58f, 1.f) : ImGui::GetStyle().Colors[ImGuiCol_Button]);
            if (ImGui::Button(slot == 0 ? " A " : " B ")) switchAB(slot);
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered() && GImGui->HoveredIdTimer > 0.6f)
                ImGui::SetTooltip("Dwa schowki na ustawienia do szybkiego porównania.\nPrzy pierwszym przełączeniu B dostaje kopię bieżących ustawień.");
        }

        // trigger LED
        ImDrawList* const dl = ImGui::GetWindowDrawList();
        const ImVec2 wp = ImGui::GetWindowPos();
        const bool lit = fP[kOutTrig] > 0.5f;
        const ImVec2 c(wp.x + getWidth() - S(22), wp.y + y + S(11));
        dl->AddCircleFilled(c, S(6), lit ? IM_COL32(255, 90, 70, 255) : IM_COL32(70, 40, 40, 255));
        dl->AddText(ImVec2(c.x - S(46), c.y - S(8)), kColDim, "TRIG");
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

        float v[kInputCount];
        std::memcpy(v, fP, sizeof(v));
        const int voices = 2 + int(uni(0.f, 4.999f));
        const bool sameShape = chance(0.6f);
        const int shape0 = int(uni(0.f, float(shRandom) + 0.999f));
        const float rate0 = logu(0.15f, 2.5f), min0 = logu(2.f, 12.f), span0 = uni(2.f, 14.f);
        const bool filter = chance(0.3f), fb = chance(0.2f);
        const float hue = logu(300.f, 1500.f), wah = uni(-2.5f, 2.5f);
        for (int i = 0; i < kNumVoices; ++i)
        {
            float* const vp = v + voiceIndex(i, 0);
            vp[vOn] = i < voices ? 1.f : 0.f;
            vp[vLevel] = 100.f; vp[vPan] = 0.f; vp[vPhase] = 0.f; vp[vStart] = 0.f; vp[vDetune] = 0.f;
            vp[vShape] = float(sameShape ? shape0 : int(uni(0.f, float(shRandom) + 0.999f)));
            vp[vRate] = rate0 * (sameShape ? 1.f : logu(0.7f, 1.4f));
            vp[vDiv] = float(int(uni(1.f, 9.999f)));
            vp[vDelayMin] = std::min(60.f, min0 * uni(0.85f, 1.2f));
            vp[vDelayMax] = std::min(60.f, vp[vDelayMin] + span0 * uni(0.7f, 1.3f));
            vp[vFeedback] = fb ? uni(-50.f, 55.f) : 0.f;
            vp[vFilter] = filter ? uni(50.f, 100.f) : 0.f;
            vp[vHue] = hue; vp[vWah] = filter ? wah : 0.f; vp[vReso] = uni(2.f, 6.f);
        }
        v[gMix] = uni(40.f, 62.f); v[gRateMul] = 1.f; v[gDepthMul] = 100.f;
        v[gSpPhase] = chance(0.75f) ? 360.f : uni(0.f, 360.f);
        v[gSpStart] = chance(0.25f) ? uni(100.f, 600.f) : 0.f;
        v[gSpRate] = chance(0.4f) ? uni(5.f, 60.f) : 0.f;
        v[gSpDetune] = chance(0.5f) ? uni(2.f, 12.f) : 0.f;
        v[gSpPan] = uni(50.f, 100.f);
        v[gTrigOn] = 0.f; v[gFade] = v[gSpStart] > 0.f ? uni(200.f, 800.f) : 0.f;
        v[gEnvDepth] = 0.f; v[gEnvWah] = 0.f;
        sanitize(v);
        applyValues(v);
        fCurName.clear();
    }

    // editor for the "Custom wave" LFO shape: draggable points (like EQ nodes) or a formula
    void waveEditor()
    {
        if (fOpenWave) { ImGui::OpenPopup("Custom wave"); fOpenWave = false; syncEditor(); fFormulaErr.clear(); fNodeDrag = -1; }
        ImGui::SetNextWindowPos(ImVec2(getWidth() * 0.5f, getHeight() * 0.45f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (!ImGui::BeginPopup("Custom wave")) return;

        const bool formula = fWave.size() > 1 && fWave[0] == 'f';
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColAccent), "CUSTOM WAVE");
        ImGui::SameLine();
        ImGui::TextDisabled(formula ? "formula active  -  one cycle, top = Max delay, bottom = Min delay"
                                    : "drag a point  -  double-click = add  -  right-click = remove  -  top = Max delay");

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
        if (!formula)
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
        ImVec2 pts[kWaveSize + 1];
        for (int i = 0; i <= kWaveSize; ++i)
            pts[i] = toScreen(float(i) / float(kWaveSize), fWaveTable[i & (kWaveSize - 1)]);
        dl->PushClipRect(c0, ImVec2(c0.x + size.x, c0.y + size.y), true);
        dl->AddPolyline(pts, kWaveSize + 1, kColAccent, 0, S(2.f));
        dl->PopClipRect();
        if (!formula)
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
        ImGui::SameLine(S(76)); if (ImGui::Button("Sine")) setNodes("n:0,0;0.5,1");
        ImGui::SameLine(); if (ImGui::Button("Triangle")) setNodes("l:0,0;0.5,1");
        ImGui::SameLine(); if (ImGui::Button("Trapezoid")) setNodes(kTrapezoidWave);
        ImGui::SameLine(); if (ImGui::Button("Steps")) setNodes("l:0,0;0.249,0;0.25,0.33;0.499,0.33;0.5,0.67;0.749,0.67;0.75,1;0.999,1");
        ImGui::SameLine();
        if (ImGui::Button("Random"))
        {
            std::vector<WaveNode> nd;
            for (int i = 0; i < 8; ++i)
                nd.push_back({ float(i) / 8.f, i == 0 ? 0.f : std::uniform_real_distribution<float>(0.f, 1.f)(fRng) });
            setNodes(nodesToString(nd, true).c_str());
        }
        ImGui::SameLine(0, S(18));
        if (formula)
        {
            if (ImGui::Button("Formula -> points"))
            {
                std::vector<WaveNode> nd;
                for (int i = 0; i < 16; ++i) nd.push_back({ float(i) / 16.f, fWaveTable[i * kWaveSize / 16] });
                setNodes(nodesToString(nd, true).c_str());
            }
            if (ImGui::IsItemHovered() && GImGui->HoveredIdTimer > 0.6f)
                ImGui::SetTooltip("Zamienia wzór na 16 punktów, które można potem przesuwać ręcznie.");
        }
        else if (ImGui::Checkbox("Smooth curve", &fNodesSmooth))
            setWave(nodesToString(fNodes, fNodesSmooth));

        // ---- formula
        static const char* const kExamples[] = {
            "sin(2*pi*x)", "sin(2*pi*x)^3", "abs(sin(2*pi*x))", "sin(2*pi*x) + 0.4*sin(6*pi*x)",
            "x^3", "1 - exp(-6*x)", "floor(x*5)", "tri(x)^2", "tri(2*x) * saw(x)",
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
            ImGui::TextColored(ImVec4(1.f, 0.55f, 0.45f, 1.f), "Formula not applied: %s", fFormulaErr.c_str());
        else
            ImGui::TextDisabled("x runs 0..1 over one cycle; the result is scaled to fill the range and stored as a 256-value table.");
        ImGui::TextDisabled("sin cos tan abs sqrt exp ln floor frac min max pow mod clamp mix step tri saw sqr, pi, e, + - * / ^");
        ImGui::TextDisabled("Shared by every voice whose Shape is \"Custom wave\".");
        ImGui::SameLine(S(450));
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

        if (mode == kBrowseImport)
        {
            std::vector<UserPreset> in;
            ImportReport rep;
            if (!readPresets(path.c_str(), in, &rep.badRows))
            {
                setStatus("Import failed: not a Swarm Chorus preset CSV");
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
        ImGui::Begin("ROJ", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
                                     | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor();

        ImDrawList* const dl = ImGui::GetWindowDrawList();
        const ImVec2 wp = ImGui::GetWindowPos();

        header(S(10));

        // graph + global
        graph(dl, wp, S(12), S(46), S(700), S(320));

        const float gx = S(720), gy = S(46);
        panel(dl, wp, gx, gy, S(348), S(320), "GLOBAL");
        knob("Mix", gMix, gx + S(9), gy + S(26), kColAccent, "Balans sygnału czystego i efektu. 100% = sam efekt (vibrato).");
        knob("Output", gOutput, gx + S(75), gy + S(26), kColAccent, "Głośność wyjściowa.");
        knob("Warmth", gWarmth, gx + S(141), gy + S(26), kColAccent, "Analogowy charakter głosów: przycina górę i lekko nasyca, jak w kostkach na układach BBD.\nZ feedbackiem każde powtórzenie robi się ciemniejsze.");
        knob("Rate x", gRateMul, gx + S(207), gy + S(26), kColAccent, "Mnożnik tempa wszystkich głosów naraz.");
        knob("Depth x", gDepthMul, gx + S(273), gy + S(26), kColAccent, "Mnożnik głębokości modulacji wszystkich głosów. 0% = brak ruchu (zostaje sam detune).");

        dl->AddText(ImVec2(wp.x + gx + S(10), wp.y + gy + S(112)), kColAccent, "TRIGGER");
        toggle("Start on note", gTrigOn, gx + S(12), gy + S(150),
               "Włączone: każde uderzenie w struny startuje krzywe od początku (z opóźnieniami Start / Stagger).\n"
               "Wyłączone: krzywe biegną cały czas.");
        knob("Threshold", gTrigThresh, gx + S(141), gy + S(130), kColAccent, "Próg głośności, od którego uderzenie liczy się jako start. Dioda TRIG pokazuje wyzwolenie.");
        knob("Fade in", gFade, gx + S(207), gy + S(130), kColAccent, "Czas narastania modulacji po starcie głosu.");

        dl->AddText(ImVec2(wp.x + gx + S(10), wp.y + gy + S(216)), kColAccent, "ENVELOPE");
        dl->AddText(ImVec2(wp.x + gx + S(84), wp.y + gy + S(216)), kColDim, "picking strength controls the effect");
        knob("Sens", gEnvSens, gx + S(9), gy + S(236), kColAccent, "Czułość na siłę gry. Ustaw tak, żeby pasek poziomu dochodził blisko końca przy mocnym uderzeniu.");
        knob("> Depth", gEnvDepth, gx + S(75), gy + S(236), kColAccent, "Siła gry steruje głębokością modulacji.\nPlus: mocniej grasz = głębszy chorus. Minus: mocniej grasz = płytszy.");
        knob("> Wah", gEnvWah, gx + S(141), gy + S(236), kColAccent, "Siła gry przesuwa filtr Hue o tyle oktaw - klasyczny filtr obwiedniowy.\nSłychać tylko w głosach z Filter > 0.");
        {
            const ImVec2 m0(wp.x + gx + S(222), wp.y + gy + S(262)), m1(wp.x + gx + S(334), wp.y + gy + S(272));
            dl->AddRectFilled(m0, m1, kColGraph, S(2));
            const float lv = std::min(1.f, std::max(0.f, fP[kOutEnv]));
            if (lv > 0.005f) dl->AddRectFilled(m0, ImVec2(m0.x + (m1.x - m0.x) * lv, m1.y), kColAccent, S(2));
            centeredText(dl, 0.5f * (m0.x + m1.x), m1.y + S(4), kColDim, "level");
        }

        // voice tabs
        const float ty = S(374);
        {
            ImGui::SetCursorPos(ImVec2(S(12), ty));
            const bool all = fSel < 0;
            ImGui::PushStyleColor(ImGuiCol_Button, all ? ImVec4(0.30f, 0.42f, 0.58f, 1.f) : ImGui::GetStyle().Colors[ImGuiCol_Button]);
            if (ImGui::Button("ALL VOICES", ImVec2(S(104), S(30)))) fSel = -1;
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered() && GImGui->HoveredIdTimer > 0.6f)
                ImGui::SetTooltip("Pokrętła poniżej ustawiają wszystkie głosy naraz.\nZnak ~ przy wartości = głosy mają różne ustawienia.");

            for (int v = 0; v < kNumVoices; ++v)
            {
                const float tx = S(126) + S(157) * float(v);
                const int onIdx = voiceIndex(v, vOn);
                bool on = fP[onIdx] > 0.5f;
                ImGui::PushID(1000 + v);
                ImGui::SetCursorPos(ImVec2(tx, ty + S(4)));
                ImGui::PushStyleColor(ImGuiCol_CheckMark, ImGui::ColorConvertU32ToFloat4(kVoiceColors[v]));
                if (ImGui::Checkbox("##on", &on)) setOne(onIdx, on ? 1.f : 0.f);
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered() && GImGui->HoveredIdTimer > 0.6f) ImGui::SetTooltip("Włącz / wyłącz głos");

                const bool sel = fSel == v;
                ImGui::SetCursorPos(ImVec2(tx + S(28), ty));
                const ImVec2 bp = ImGui::GetCursorScreenPos();
                ImGui::PushStyleColor(ImGuiCol_Button, sel ? ImVec4(0.30f, 0.42f, 0.58f, 1.f) : ImGui::GetStyle().Colors[ImGuiCol_Button]);
                ImGui::PushStyleColor(ImGuiCol_Text, on ? ImGui::ColorConvertU32ToFloat4(kColText) : ImGui::ColorConvertU32ToFloat4(kColDim));
                char lbl[24];
                std::snprintf(lbl, sizeof(lbl), "Voice %d", v + 1);
                if (ImGui::Button(lbl, ImVec2(S(120), S(30)))) fSel = v;
                ImGui::PopStyleColor(2);
                dl->AddRectFilled(ImVec2(bp.x + S(4), bp.y + S(25)), ImVec2(bp.x + S(116), bp.y + S(28)),
                                  on ? kVoiceColors[v] : withAlpha(kVoiceColors[v], 0.3f), S(1.5f));
                ImGui::PopID();
            }
        }

        // voice knobs
        const float vy = S(412);
        const int dv = fSel < 0 ? 0 : fSel;
        const ImU32 vc = fSel < 0 ? kColAccent : kVoiceColors[fSel];
        const auto VI = [dv](int p) { return voiceIndex(dv, p); };
        panel(dl, wp, S(12), vy, S(1056), S(112), nullptr);
        dl->AddText(ImVec2(wp.x + S(24), wp.y + vy + S(6)), kColAccent, "LEVEL");
        dl->AddText(ImVec2(wp.x + S(170), wp.y + vy + S(6)), kColAccent, "MODULATION OF DELAY TIME");
        dl->AddText(ImVec2(wp.x + S(650), wp.y + vy + S(6)), kColAccent, "PITCH / FEEDBACK");
        dl->AddText(ImVec2(wp.x + S(796), wp.y + vy + S(6)), kColAccent, "HUE / WAH FILTER");
        const float ky = vy + S(28);
        knob("Level", VI(vLevel), S(18), ky, vc, "Głośność głosu.");
        knob("Pan", VI(vPan), S(84), ky, vc, "Położenie głosu w stereo (dodaje się do Spread > Pan).");
        knob("Min", VI(vDelayMin), S(164), ky, vc, "Najkrótszy czas opóźnienia głosu. Krótkie czasy (poniżej ok. 5 ms) brzmią jak flanger.");
        knob("Max", VI(vDelayMax), S(230), ky, vc, "Najdłuższy czas opóźnienia głosu. Min = Max: głos się nie rusza.");
        toggle("Tempo sync", gSync, S(400), vy + S(3), "Tempo głosów w wartościach nutowych względem tempa projektu (zakładam metrum 4/4).\nKrzywe biegną w tempie, ale nie są przypięte do początku taktu.");
        if (fP[gSync] > 0.5f)
            knob("Rate", VI(vDiv), S(296), ky, vc, "Długość jednego cyklu w wartościach nutowych. . = z kropką, T = triola.");
        else
            knob("Rate", VI(vRate), S(296), ky, vc, "Tempo zmian czasu opóźnienia.");
        {
            const int sIdx = VI(vShape);
            ImGui::PushID(sIdx);
            centeredText(dl, wp.x + S(362) + S(64), wp.y + ky, kColDim, "Shape");
            ImGui::SetCursorPos(ImVec2(S(364), ky + S(26)));
            ImGui::SetNextItemWidth(S(124));
            const int cur = std::min(int(kShapeCount) - 1, std::max(0, int(fP[sIdx] + 0.5f)));
            const std::string preview = std::string(mixed(sIdx) ? "~ " : "") + kShapeNames[cur];
            if (ImGui::BeginCombo("##shape", preview.c_str()))
            {
                for (int i = 0; i < kShapeCount; ++i)
                    if (ImGui::Selectable(kShapeNames[i], i == cur))
                    {
                        beginEdit(sIdx); setValue(sIdx, float(i)); endEdit(sIdx);
                    }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered() && GImGui->HoveredIdTimer > 0.6f)
                ImGui::SetTooltip("Kształt krzywej zmiany czasu.\nLamp (Vibe) = żarówka + fotorezystor jak w Uni-Vibe: szybko się zapala, wolno gaśnie.");
            ImGui::SetCursorPos(ImVec2(S(364), ky + S(52)));
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(4), S(1)));
            if (ImGui::Button("Edit wave...", ImVec2(S(124), S(20))))
            {
                beginEdit(sIdx); setValue(sIdx, float(shCustom)); endEdit(sIdx);
                fOpenWave = true;
            }
            ImGui::PopStyleVar();
            if (ImGui::IsItemHovered() && GImGui->HoveredIdTimer > 0.6f)
                ImGui::SetTooltip("Ustawia kształt na Custom i otwiera edytor własnej fali: punkty jak w korektorze albo wzór matematyczny.");
            ImGui::PopID();
        }
        knob("Phase", VI(vPhase), S(494), ky, vc, "Przesunięcie krzywej tego głosu w cyklu (dodaje się do Spread > Phase).");
        knob("Start", VI(vStart), S(560), ky, vc, "Po jakim czasie od startu głos zaczyna się ruszać (dodaje się do Spread > Stagger).");
        knob("Detune", VI(vDetune), S(640), ky, vc, "Stałe odstrojenie głosu w centach, działa jednocześnie z chorusem.");
        knob("Feedback", VI(vFeedback), S(706), ky, vc, "Zawracanie głosu na jego wejście. Z krótkimi czasami daje flanger. Wartości ujemne odwracają fazę.");
        knob("Hue", VI(vHue), S(786), ky, vc, "Częstotliwość filtra głosu - jak wah zatrzymany w jednej pozycji. Słychać dopiero, gdy Filter > 0.");
        knob("Wah", VI(vWah), S(852), ky, vc, "O ile oktaw krzywa chorusa przesuwa filtr - auto-wah zsynchronizowany z chorusem. 0 = filtr stoi.");
        knob("Filter", VI(vFilter), S(918), ky, vc, "Ile filtra w głosie: 0% = wyłączony, 100% = sam dźwięk przez filtr.");
        knob("Reso", VI(vReso), S(984), ky, vc, "Ostrość (rezonans) filtra.");

        // spread
        const float by = S(532);
        panel(dl, wp, S(12), by, S(350), S(112), "SPREAD BETWEEN VOICES");
        const float sy = by + S(28);
        knob("Phase", gSpPhase, S(18), sy, kColAccent, "Rozkłada fazy włączonych głosów równo w cyklu. 360° przy 3 głosach = 0/120/240° (tri-chorus).");
        knob("Stagger", gSpStart, S(84), sy, kColAccent, "Każdy kolejny głos startuje o tyle później od poprzedniego.");
        knob("Rate", gSpRate, S(150), sy, kColAccent, "Rozrzut tempa między głosami (100% = do oktawy w dół i w górę).");
        knob("Detune", gSpDetune, S(216), sy, kColAccent, "Rozstraja głosy symetrycznie w dół i w górę o tyle centów.");
        knob("Pan", gSpPan, S(282), sy, kColAccent, "Rozkłada głosy od lewej do prawej.");

        // EQ
        panel(dl, wp, S(370), by, S(698), S(112), "EQ");
        toggle("on the whole output (off = effect only)", gEqTarget, S(420), by + S(3), "Czy korektor działa na cały sygnał, czy tylko na głosy efektu.");
        static const char* const kBand[3] = { "Low", "Mid", "High" };
        for (int bnd = 0; bnd < 3; ++bnd)
        {
            const float ex = S(384) + S(226) * float(bnd);
            char l1[24];
            std::snprintf(l1, sizeof(l1), "%s freq", kBand[bnd]);
            knob(l1, gEq1F + bnd * 3, ex, sy, kColAccent, "Częstotliwość pasma.");
            knob("Gain", gEq1G + bnd * 3, ex + S(66), sy, kColAccent, "Podbicie lub wycięcie pasma.");
            knob("Q", gEq1Q + bnd * 3, ex + S(132), sy, kColAccent, "Szerokość pasma: większe Q = węższe.");
        }

        waveEditor();

        // footer: status message, or the mouse hints
        {
            const bool st = !fStatus.empty() && ImGui::GetTime() < fStatusUntil;
            dl->AddText(ImVec2(wp.x + S(16), wp.y + S(650)), st ? IM_COL32(190, 230, 190, 255) : kColDim,
                        st ? fStatus.c_str() : "Knobs: drag up/down, Shift = fine, double-click = default, mouse wheel.     Graph: drag a curve sideways = start, with Shift = phase, up/down = delay time.");
        }

        ImGui::End();
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RojUI)
};

UI* createUI() { return new RojUI(); }

END_NAMESPACE_DISTRHO
