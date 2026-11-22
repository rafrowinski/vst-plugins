// Swarm Delay - user interface (Dear ImGui)
#include "PresetIO.hpp"

#include "DistrhoUI.hpp"
#include "DearImGui/imgui_internal.h"

#include <chrono>
#include <random>

START_NAMESPACE_DISTRHO

using namespace swd;

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

class SwarmDelayUI : public UI
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

    // dragging a voice's first echo on the graph
    int fDragVoice = -1;
    float fDragTime0 = 0.f, fDragLevel0 = 0.f, fDragWin = 1.f, fDragFactor = 1.f;
    bool fDragSync = false;

    // A/B compare
    float fAB[2][kInputCount];
    bool fABValid[2] = { false, false };
    int fABCur = 0;

    std::mt19937 fRng { uint32_t(std::chrono::steady_clock::now().time_since_epoch().count()) };

    float S(float x) const { return x * fScale; }

public:
    SwarmDelayUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        fScale = float(getScaleFactor());
        if (fScale > 1.01f)
            setSize(uint(DISTRHO_UI_DEFAULT_WIDTH * fScale), uint(DISTRHO_UI_DEFAULT_HEIGHT * fScale));
        setFontSize(14.f);

        presetDefaults(fP);
        for (int i = kInputCount; i < kParamCount; ++i) fP[i] = 0.f;
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
        // Freeze is a performance switch, not part of a sound: presets, Random and A/B leave it alone
        for (int i = 0; i < kInputCount; ++i)
            if (i != gFreeze && fP[i] != v[i]) setOne(i, v[i]);
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
        if (vp == vDiv || idx == gSwDiv)
            std::snprintf(b, n, "%s", kDivNames[std::min(int(kDivCount) - 1, std::max(0, int(v + 0.5f)))]);
        else if (vp == vPan)
        {
            if (std::fabs(v) < 0.5f) std::snprintf(b, n, "C");
            else std::snprintf(b, n, "%s%.0f", v < 0 ? "L" : "R", std::fabs(v));
        }
        else if (idx == gCrRate && v >= 47900.f) std::snprintf(b, n, "off");
        else if (idx == gHiCut && v >= 19900.f)  std::snprintf(b, n, "off");
        else if (idx == gLoCut && v <= 21.f)     std::snprintf(b, n, "off");
        else if (idx == gBits)
        {
            if (v >= 15.95f) std::snprintf(b, n, "off");
            else std::snprintf(b, n, "%.1f bit", v);
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
            else if (v < 1000.f) std::snprintf(b, n, "%.0f ms", v);
            else std::snprintf(b, n, "%.2f s", v * 0.001f);
        }
        else if (!std::strcmp(u, "%"))   std::snprintf(b, n, "%.0f %%", v);
        else if (!std::strcmp(u, "dB"))  std::snprintf(b, n, "%+.1f dB", v);
        else if (!std::strcmp(u, "ct"))  std::snprintf(b, n, d.min < 0.f ? "%+.1f ct" : "%.1f ct", v);
        else if (!std::strcmp(u, "st"))  std::snprintf(b, n, "%+d st", int(std::lround(v)));
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

    void voiceToggle(const char* label, int idx, float x, float y, const char* tip)
    {
        ImGui::SetCursorPos(ImVec2(x, y));
        bool on = fP[idx] > 0.5f;
        char lbl[64];
        std::snprintf(lbl, sizeof(lbl), "%s%s", mixed(idx) ? "~ " : "", label);
        ImGui::PushID(idx);
        if (ImGui::Checkbox(lbl, &on)) { beginEdit(idx); setValue(idx, on ? 1.f : 0.f); endEdit(idx); }
        if (tip != nullptr && ImGui::IsItemHovered() && GImGui->HoveredIdTimer > 0.6f) ImGui::SetTooltip("%s", tip);
        ImGui::PopID();
    }

    void panel(ImDrawList* dl, const ImVec2& wp, float x, float y, float w, float h, const char* title)
    {
        dl->AddRectFilled(ImVec2(wp.x + x, wp.y + y), ImVec2(wp.x + x + w, wp.y + y + h), kColPanel, S(6));
        if (title != nullptr) dl->AddText(ImVec2(wp.x + x + S(10), wp.y + y + S(6)), kColAccent, title);
    }

    // ----------------------------------------------------------------------------------------------------------------
    // graph: every repeat of every voice against time. Upwards = level in the left channel, downwards = right.

    static int repeatsOf(const Eff& e)
    {
        if (e.fb < 0.02f) return 1;
        return std::min(12, std::max(1, int(std::ceil(std::log(0.06f) / std::log(std::min(e.fb, 0.97f))))));
    }

    void graph(ImDrawList* dl, const ImVec2& wp, float x, float y, float w, float h)
    {
        const ImVec2 a(wp.x + x, wp.y + y), b(wp.x + x + w, wp.y + y + h);
        dl->AddRectFilled(a, b, kColPanel, S(6));

        const ImVec2 g0(a.x + S(30), a.y + S(26)), g1(b.x - S(12), b.y - S(24));
        dl->AddRectFilled(g0, g1, kColGraph);
        dl->AddText(ImVec2(a.x + S(10), a.y + S(6)), kColAccent, "ECHO PATTERN");
        const float bpm = fP[kOutBpm] > 1.f ? fP[kOutBpm] : 120.f;
        const bool pingpong = fP[gPingPong] > 0.5f;
        bool anySync = fP[gSwMode] > 0.5f && fP[gSwSync] > 0.5f;
        for (int v = 0; v < kNumVoices; ++v)
            if (fP[voiceIndex(v, vOn)] > 0.5f && fP[voiceIndex(v, vSync)] > 0.5f) anySync = true;
        char sub[160];
        if (anySync)
            std::snprintf(sub, sizeof(sub), "every repeat of every voice  -  up = left, down = right  -  synced to %.1f BPM", bpm);
        else
            std::snprintf(sub, sizeof(sub), "every repeat of every voice  -  up = left channel, down = right channel");
        dl->AddText(ImVec2(a.x + S(120), a.y + S(6)), kColDim, sub);

        Eff eff[kNumVoices];
        const int n = computeEff(fP, eff, bpm);

        // live delay time (follows tape glides and glitches) when the engine reports it
        float tsec[kNumVoices];
        float win = 1.f;
        for (int v = 0; v < kNumVoices; ++v)
        {
            const float now = fP[kOutNow + v];
            tsec[v] = (eff[v].on && now > 0.f ? now : eff[v].timeMs) * 0.001f;
            if (eff[v].on) win = std::max(win, eff[v].timeMs * 0.001f * float(std::min(5, repeatsOf(eff[v]))) * 1.12f);
        }
        win = std::min(12.f, win);

        const float gw = g1.x - g0.x, gh = g1.y - g0.y;
        const float mid = std::floor(0.5f * (g0.y + g1.y)) + 0.5f;
        const float half = 0.5f * gh - S(10);
        const auto X = [&](float t) { return g0.x + gw * t / win; };
        const auto H = [&](float amp) { return half * std::min(1.f, amp / 1.5f); };

        // direct manipulation: grab the first echo of a voice
        ImGui::SetCursorScreenPos(g0);
        ImGui::InvisibleButton("##graph", ImVec2(gw, gh));
        const ImGuiIO& io = ImGui::GetIO();
        if (ImGui::IsItemActivated())
        {
            float best = S(14);
            fDragVoice = -1;
            for (int v = 0; v < kNumVoices; ++v)
            {
                if (!eff[v].on) continue;
                const float dist = std::fabs(X(tsec[v]) - io.MousePos.x);
                if (dist < best) { best = dist; fDragVoice = v; }
            }
            if (fDragVoice >= 0)
            {
                const int v = fDragVoice;
                fSel = v;
                fDragTime0 = fP[voiceIndex(v, vTime)]; fDragLevel0 = fP[voiceIndex(v, vLevel)];
                fDragWin = win; fDragFactor = std::max(0.001f, eff[v].factor); fDragSync = eff[v].sync;
                editParameter(voiceIndex(v, vTime), true); editParameter(voiceIndex(v, vLevel), true);
            }
        }
        if (ImGui::IsItemActive() && fDragVoice >= 0)
        {
            const int v = fDragVoice;
            const ImVec2 d = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left, 0.f);
            const int idx[2] = { voiceIndex(v, vTime), voiceIndex(v, vLevel) };
            const float val[2] = {
                fDragSync ? fDragTime0 : std::min(2000.f, std::max(1.f, fDragTime0 + d.x / gw * fDragWin * 1000.f / fDragFactor)),
                std::min(150.f, std::max(0.f, fDragLevel0 - d.y / half * 150.f)) };
            for (int k2 = 0; k2 < 2; ++k2)
                if (fP[idx[k2]] != val[k2]) { fP[idx[k2]] = val[k2]; setParameterValue(idx[k2], val[k2]); fMatchDirty = true; }
        }
        if (ImGui::IsItemDeactivated() && fDragVoice >= 0)
        {
            editParameter(voiceIndex(fDragVoice, vTime), false); editParameter(voiceIndex(fDragVoice, vLevel), false);
            fDragVoice = -1;
        }

        // grid + axis labels
        char txt[48];
        float step = 0.1f;
        static const float kSteps[] = { 0.1f, 0.25f, 0.5f, 1.f, 2.f };
        for (float sv : kSteps) { step = sv; if (win / sv <= 11.f) break; }
        for (float t = 0.f; t <= win + 0.001f; t += step)
        {
            const float xx = std::floor(X(t)) + 0.5f;
            dl->AddLine(ImVec2(xx, g0.y), ImVec2(xx, g1.y), kColGrid);
            std::snprintf(txt, sizeof(txt), step < 1.f ? "%.2f s" : "%.0f s", t);
            const ImVec2 sz = ImGui::CalcTextSize(txt);
            if (xx + sz.x * 0.5f < g1.x + S(8))
                dl->AddText(ImVec2(xx - sz.x * 0.5f, g1.y + S(4)), kColDim, txt);
        }
        dl->AddLine(ImVec2(g0.x, mid), ImVec2(g1.x, mid), kColTrack);
        dl->AddText(ImVec2(a.x + S(12), g0.y + S(4)), kColDim, "L");
        dl->AddText(ImVec2(a.x + S(12), g1.y - S(18)), kColDim, "R");

        // the dry note
        dl->AddLine(ImVec2(g0.x + S(1), mid - H(1.f)), ImVec2(g0.x + S(1), mid + H(1.f)), kColDim, S(3.f));

        // repeats (selected voice on top)
        dl->PushClipRect(g0, g1, true);
        for (int pass = 0; pass < 2; ++pass)
            for (int v = 0; v < kNumVoices; ++v)
            {
                if (!eff[v].on) continue;
                const bool sel = fSel == v;
                if ((pass == 1) != sel) continue;
                const Eff& e = eff[v];
                const ImU32 col = (fSel < 0 || sel) ? kVoiceColors[v] : withAlpha(kVoiceColors[v], 0.45f);
                const float ang = (e.pan + 1.f) * 0.25f * 3.14159265f;
                const float pl = std::cos(ang) * 1.41421356f, pr = std::sin(ang) * 1.41421356f;
                const float off = S(1.5f) * (float(v) - 2.5f); // keeps voices with the same time apart
                const float th = S(sel ? 3.f : 2.f);
                float amp = e.level;
                for (int k = 1; k <= 40; ++k)
                {
                    const float t = tsec[v] * float(k);
                    if (t > win) break;
                    if (k > 1) amp *= std::min(1.f, e.fb);
                    if (amp < 0.03f && k > 1) break;
                    const float xx = X(t) + off;
                    const float hl = H(amp * pl * (pingpong && k % 2 == 0 ? 0.f : 1.f));
                    const float hr = H(amp * pr * (pingpong && k % 2 == 1 ? 0.f : 1.f));
                    const ImU32 c = k == 1 ? col : withAlpha(col, ((fSel < 0 || sel) ? 0.8f : 0.4f));
                    dl->AddLine(ImVec2(xx, mid - hl), ImVec2(xx, mid + hr), c, th);
                    for (int side = 0; side < 2; ++side)
                    {
                        const float hh = side == 0 ? hl : hr;
                        if (hh < S(2)) continue;
                        const ImVec2 tip(xx, side == 0 ? mid - hh : mid + hh);
                        if (e.rev) // reversed voices point backwards
                            dl->AddTriangleFilled(ImVec2(tip.x - S(7), tip.y), ImVec2(tip.x + S(3), tip.y - S(5)), ImVec2(tip.x + S(3), tip.y + S(5)), c);
                        else
                            dl->AddCircleFilled(tip, S(k == 1 ? 4.5f : 3.5f), c);
                    }
                }
            }
        dl->PopClipRect();

        if (n == 0)
            centeredText(dl, 0.5f * (g0.x + g1.x), mid - S(30), kColDim, "All voices are off - switch one on below");
    }

    // ----------------------------------------------------------------------------------------------------------------
    // presets

    void loadByName(const std::string& name)
    {
        float v[kInputCount];
        if (!fLib.lookup(name, v)) return;
        applyValues(v);
        fCurName = name;
        fMatchDirty = false;
    }

    void matchPreset()
    {
        fMatchDirty = false;
        float cur[kInputCount], v[kInputCount];
        std::memcpy(cur, fP, sizeof(cur));
        cur[gFreeze] = 0.f;
        if (!fCurName.empty() && fLib.lookup(fCurName, v) && sameValues(v, cur)) return;
        fCurName.clear();
        for (int i = 0; i < kPresetCount; ++i)
        {
            kPresets[i].fill(v);
            if (sameValues(v, cur)) { fCurName = kPresets[i].name; return; }
        }
        for (const UserPreset& u : fLib.user)
            if (sameValues(u.v, cur)) { fCurName = u.name; return; }
    }

    void header(float y)
    {
        ImGui::SetCursorPos(ImVec2(S(16), y + S(3)));
        ImGui::TextColored(ImVec4(0.47f, 0.78f, 1.f, 1.f), "SWARM DELAY");

        ImGui::SetCursorPos(ImVec2(S(128), y));
        ImGui::SetNextItemWidth(S(236));
        const std::string shown = fCurName.empty() ? std::string("(custom settings)") : fCurName;
        if (ImGui::BeginCombo("##preset", shown.c_str(), ImGuiComboFlags_HeightLargest))
        {
            ImGui::TextDisabled("Factory");
            for (int i = 0; i < kPresetCount; ++i)
                if (ImGui::Selectable(kPresets[i].name, fCurName == kPresets[i].name))
                    loadByName(kPresets[i].name);
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
                p.v[gFreeze] = 0.f;
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
            ImGui::SetTooltip("Losuje głosy, czasy, kierunki, filtry i brud. Mix, Output, Ducking, Time sweep, wybór ms/nuta i Freeze zostają bez zmian.");
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

        // freeze
        {
            const bool on = fP[gFreeze] > 0.5f;
            ImGui::SetCursorPos(ImVec2(getWidth() - S(108), y - S(2)));
            ImGui::PushStyleColor(ImGuiCol_Button, on ? ImVec4(0.35f, 0.75f, 0.95f, 1.f) : ImGui::GetStyle().Colors[ImGuiCol_Button]);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? ImVec4(0.45f, 0.82f, 1.f, 1.f) : ImGui::GetStyle().Colors[ImGuiCol_ButtonHovered]);
            ImGui::PushStyleColor(ImGuiCol_Text, on ? ImVec4(0.05f, 0.08f, 0.12f, 1.f) : ImGui::ColorConvertU32ToFloat4(kColText));
            if (ImGui::Button(on ? "FROZEN" : "FREEZE", ImVec2(S(96), S(28)))) setOne(gFreeze, on ? 0.f : 1.f);
            ImGui::PopStyleColor(3);
            if (ImGui::IsItemHovered() && GImGui->HoveredIdTimer > 0.6f)
                ImGui::SetTooltip("Zamraża to, co jest teraz w liniach opóźniających, i zapętla to bez końca.\n"
                                  "Na zatrzymanym dźwięku działają Time x, Reverse, Pitch, chorus i crusher; możesz też grać na czysto.\n"
                                  "To parametr wtyczki, więc w REAPERZE przypiszesz go do footswitcha (Param > Learn).");
        }
    }

    void switchAB(int to)
    {
        if (to == fABCur) return;
        std::memcpy(fAB[fABCur], fP, sizeof(fAB[0]));
        fABValid[fABCur] = true;
        if (!fABValid[to]) { std::memcpy(fAB[to], fP, sizeof(fAB[0])); fABValid[to] = true; }
        fABCur = to;
        applyValues(fAB[to]);
    }

    void randomize()
    {
        const auto uni = [this](float lo, float hi) { return lo + (hi - lo) * std::uniform_real_distribution<float>(0.f, 1.f)(fRng); };
        const auto logu = [&uni](float lo, float hi) { return lo * std::pow(hi / lo, uni(0.f, 1.f)); };
        const auto chance = [&uni](float pr) { return uni(0.f, 1.f) < pr; };

        float v[kInputCount];
        std::memcpy(v, fP, sizeof(v));
        static const float kRatios[6] = { 1.f, 1.5f, 2.f, 0.75f, 0.5f, 3.f };
        static const float kPitches[5] = { 12.f, -12.f, 7.f, 5.f, -5.f };
        const int voices = 1 + int(uni(0.f, 3.999f));
        const float time0 = logu(110.f, 520.f), fb0 = uni(15.f, 62.f);
        const int ratio0 = int(uni(0.f, 4.999f));
        for (int i = 0; i < kNumVoices; ++i)
        {
            float* const vp = v + voiceIndex(i, 0);
            vp[vOn] = i < voices ? 1.f : 0.f;
            vp[vLevel] = i == 0 ? 100.f : uni(70.f, 100.f);
            vp[vPan] = 0.f;
            vp[vTime] = std::min(2000.f, time0 * (i == 0 ? 1.f : kRatios[1 + (ratio0 + i) % 5])); // no two voices alike
            vp[vDiv] = float(int(uni(2.f, 11.999f)));
            vp[vFeedback] = fb0 * uni(0.8f, 1.2f);
            vp[vRev] = chance(0.2f) ? 1.f : 0.f;
            vp[vPitch] = chance(0.15f) ? kPitches[int(uni(0.f, 4.999f))] : 0.f;
            vp[vDetune] = 0.f;
            vp[vCrush] = 100.f;
        }
        v[gTimeMul] = 1.f; v[gFbMul] = 100.f;
        v[gWarmth] = chance(0.4f) ? uni(20.f, 70.f) : 0.f;
        v[gTimeMode] = chance(0.3f) ? 1.f : 0.f;
        v[gPingPong] = chance(0.25f) ? 1.f : 0.f;
        v[gSpTime] = 0.f;
        v[gSpDetune] = chance(0.3f) ? uni(3.f, 12.f) : 0.f;
        v[gSpPan] = voices > 1 ? uni(40.f, 100.f) : 0.f;
        v[gChDetune] = chance(0.35f) ? uni(4.f, 20.f) : 0.f;
        v[gChRate] = logu(0.3f, 2.f);
        v[gChMix] = chance(0.5f) ? 50.f : 100.f;
        const bool crush = chance(0.25f);
        v[gBits] = crush ? uni(4.f, 10.f) : 16.f;
        v[gCrRate] = crush ? logu(2500.f, 16000.f) : 48000.f;
        v[gLoCut] = chance(0.5f) ? logu(60.f, 300.f) : 20.f;
        v[gHiCut] = chance(0.6f) ? logu(2500.f, 9000.f) : 20000.f;
        v[gGlProb] = chance(0.25f) ? uni(15.f, 60.f) : 0.f;
        for (int i = 0; i < kNumVoices; ++i) v[voiceIndex(i, vSync)] = fP[voiceIndex(i, vSync)];
        sanitize(v);
        applyValues(v);
        fCurName.clear();
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
                setStatus("Import failed: not a Swarm Delay preset CSV");
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
            p.v[gFreeze] = 0.f;
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
        ImGui::Begin("SWD", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
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
        knob("Mix", gMix, gx + S(9), gy + S(26), kColAccent, "Ile powtórzeń w sygnale. Do 50% sygnał czysty zostaje w pełnej głośności, powyżej zaczyna cichnąć.");
        knob("Output", gOutput, gx + S(75), gy + S(26), kColAccent, "Głośność wyjściowa.");
        knob("Time x", gTimeMul, gx + S(141), gy + S(26), kColAccent,
             "Mnożnik czasu wszystkich głosów naraz - kręć nim w trakcie grania.\n"
             "W trybie Tape dźwięk w linii przyspiesza i zwalnia jak taśma, w trybie Jump przeskakuje i się zacina.\n"
             "Podwójne kliknięcie wraca do 1.00x.");
        knob("Feedback x", gFbMul, gx + S(207), gy + S(26), kColAccent, "Mnożnik feedbacku wszystkich głosów. Powyżej 100% powtórzenia mogą narastać (są miękko ograniczane).");
        knob("Warmth", gWarmth, gx + S(273), gy + S(26), kColAccent, "Analogowy charakter: przycina górę i lekko nasyca. Każde kolejne powtórzenie jest ciemniejsze.");

        dl->AddText(ImVec2(wp.x + gx + S(10), wp.y + gy + S(112)), kColAccent, "TIME CHANGES");
        for (int m = 0; m < 2; ++m)
        {
            const bool cur = (fP[gTimeMode] > 0.5f) == (m == 1);
            ImGui::SetCursorPos(ImVec2(gx + S(12) + S(82) * float(m), gy + S(140)));
            ImGui::PushStyleColor(ImGuiCol_Button, cur ? ImVec4(0.30f, 0.42f, 0.58f, 1.f) : ImGui::GetStyle().Colors[ImGuiCol_Button]);
            if (ImGui::Button(kTimeModeNames[m], ImVec2(S(78), S(28))) && !cur) setOne(gTimeMode, float(m));
            ImGui::PopStyleColor();
            if (ImGui::IsItemHovered() && GImGui->HoveredIdTimer > 0.6f)
                ImGui::SetTooltip(m == 0 ? "Zmiana czasu płynie: wysokość dźwięku w linii wygina się jak przy zmianie prędkości taśmy."
                                         : "Zmiana czasu przeskakuje bez zmiany wysokości: dźwięk się rwie i zacina.");
        }
        dl->AddText(ImVec2(wp.x + gx + S(12), wp.y + gy + S(176)), kColDim, "what happens when");
        dl->AddText(ImVec2(wp.x + gx + S(12), wp.y + gy + S(192)), kColDim, "the time changes");
        toggle("Ping-pong", gPingPong, gx + S(196), gy + S(144), "Kolejne powtórzenia skaczą między lewym a prawym kanałem (pierwsze z lewej).");

        dl->AddText(ImVec2(wp.x + gx + S(10), wp.y + gy + S(216)), kColAccent, "DUCKING");
        dl->AddText(ImVec2(wp.x + gx + S(76), wp.y + gy + S(216)), kColDim, "repeats get quieter while you play");
        knob("Duck", gDuck, gx + S(9), gy + S(236), kColAccent, "O ile powtórzenia cichną, kiedy grasz. Wracają w pauzach.");
        knob("Sens", gDuckSens, gx + S(75), gy + S(236), kColAccent, "Czułość na głośność gry. Ustaw tak, żeby pasek dochodził do końca przy normalnej grze.");
        knob("Release", gDuckRel, gx + S(141), gy + S(236), kColAccent, "Jak szybko powtórzenia wracają po tym, jak przestaniesz grać.");
        {
            const ImVec2 m0(wp.x + gx + S(222), wp.y + gy + S(262)), m1(wp.x + gx + S(334), wp.y + gy + S(272));
            dl->AddRectFilled(m0, m1, kColGraph, S(2));
            const float lv = std::min(1.f, std::max(0.f, fP[kOutDuck]));
            if (lv > 0.005f) dl->AddRectFilled(m0, ImVec2(m0.x + (m1.x - m0.x) * lv, m1.y), kColAccent, S(2));
            centeredText(dl, 0.5f * (m0.x + m1.x), m1.y + S(4), kColDim, "ducked");
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

            static const char* const kStateNames[4] = { "", "REV", "JUMP", "STUT" };
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
                // what the random glitches are doing to this voice right now
                const int st = std::min(3, std::max(0, int(fP[kOutState + v] + 0.5f)));
                if (on && st > 0)
                    dl->AddText(ImVec2(bp.x + S(86), bp.y + S(7)), kVoiceColors[v], kStateNames[st]);
                ImGui::PopID();
            }
        }

        // voice knobs
        const float vy = S(412);
        const int dv = fSel < 0 ? 0 : fSel;
        const ImU32 vc = fSel < 0 ? kColAccent : kVoiceColors[fSel];
        const auto VI = [dv](int p) { return voiceIndex(dv, p); };
        panel(dl, wp, S(12), vy, S(700), S(112), nullptr);
        dl->AddText(ImVec2(wp.x + S(24), wp.y + vy + S(6)), kColAccent, "LEVEL");
        dl->AddText(ImVec2(wp.x + S(166), wp.y + vy + S(6)), kColAccent, "TIME / REPEATS");
        dl->AddText(ImVec2(wp.x + S(494), wp.y + vy + S(6)), kColAccent, "PITCH IN THE LOOP");
        dl->AddText(ImVec2(wp.x + S(644), wp.y + vy + S(6)), kColAccent, "DIRT");
        const float ky = vy + S(28);
        knob("Level", VI(vLevel), S(18), ky, vc, "Głośność powtórzeń tego głosu.");
        knob("Pan", VI(vPan), S(84), ky, vc, "Położenie głosu w stereo (dodaje się do Spread > Pan).");
        const bool vsync = fP[VI(vSync)] > 0.5f;
        if (vsync)
            knob("Time", VI(vDiv), S(160), ky, vc, "Czas opóźnienia w wartościach nutowych względem tempa projektu. . = z kropką, T = triola.");
        else
            knob("Time", VI(vTime), S(160), ky, vc, "Czas opóźnienia głosu w milisekundach. Możesz też przeciągnąć pierwsze powtórzenie głosu na wykresie.");
        {
            // manual milliseconds, or a note value that follows the project tempo
            const int syIdx = VI(vSync), dIdx = VI(vDiv);
            ImGui::PushID(syIdx);
            centeredText(dl, wp.x + S(228) + S(48), wp.y + ky, kColDim, "Note value");
            ImGui::SetCursorPos(ImVec2(S(228), ky + S(26)));
            ImGui::SetNextItemWidth(S(96));
            const int cur = std::min(int(kDivCount) - 1, std::max(0, int(fP[dIdx] + 0.5f)));
            const std::string preview = std::string(mixed(syIdx) || (vsync && mixed(dIdx)) ? "~ " : "") + (vsync ? kDivNames[cur] : "Manual");
            if (ImGui::BeginCombo("##note", preview.c_str(), ImGuiComboFlags_HeightLargest))
            {
                if (ImGui::Selectable("Manual (ms)", !vsync)) { beginEdit(syIdx); setValue(syIdx, 0.f); endEdit(syIdx); }
                ImGui::Separator();
                for (int i = 0; i < kDivCount; ++i)
                    if (ImGui::Selectable(kDivNames[i], vsync && i == cur))
                    {
                        beginEdit(syIdx); setValue(syIdx, 1.f); endEdit(syIdx);
                        beginEdit(dIdx); setValue(dIdx, float(i)); endEdit(dIdx);
                    }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered() && GImGui->HoveredIdTimer > 0.6f)
                ImGui::SetTooltip("Manual = czas ustawiasz pokrętłem w ms.\nWartość nutowa (np. 1/8. = ósemka z kropką) = czas idzie za tempem projektu; zakładam metrum 4/4.\nKażdy głos może mieć inną wartość.");
            ImGui::PopID();
        }
        knob("Feedback", VI(vFeedback), S(328), ky, vc, "Ile głosu wraca na jego wejście, czyli jak długo wybrzmiewają powtórzenia. 0% = jedno powtórzenie.");
        voiceToggle("Reverse", VI(vRev), S(400), ky + S(26),
                    "Głos odtwarza kawałki o długości swojego czasu od tyłu.\nZ feedbackiem co drugie powtórzenie wraca do normalnego kierunku.");
        knob("Pitch", VI(vPitch), S(490), ky, vc,
             "Transpozycja w półtonach wewnątrz pętli: każde kolejne powtórzenie jest przesunięte o tyle samo.\n+12 z feedbackiem = shimmer (coraz wyższe oktawy).");
        knob("Detune", VI(vDetune), S(556), ky, vc, "Drobne odstrojenie głosu w centach, też narasta z każdym powtórzeniem.");
        knob("Crush", VI(vCrush), S(630), ky, vc, "Ile crushera dostaje ten głos. Słychać dopiero, gdy w sekcji Crusher skręcisz Bits lub Rate.");

        // time sweep (all voices)
        {
            const float px = S(720);
            panel(dl, wp, px, vy, S(348), S(112), "TIME SWEEP");
            dl->AddText(ImVec2(wp.x + px + S(92), wp.y + vy + S(6)), kColDim, "all voices");
            const int mode = std::min(2, std::max(0, int(fP[gSwMode] + 0.5f)));
            const bool oneWay = fP[gSwOneWay] > 0.5f;
            static const char* const kTipsReturn[3] = {
                "Sweep wyłączony.",
                "Czas rośnie: co cykl głosy skaczą na krótszy czas i wracają w górę do swojego.\nPowtórzenia zjeżdżają strojem w dół i wracają.",
                "Czas maleje: co cykl głosy skaczą na dłuższy czas i wracają w dół do swojego.\nPowtórzenia podjeżdżają strojem w górę i wracają." };
            static const char* const kTipsOneWay[3] = {
                "Sweep wyłączony.",
                "Czas rośnie: przez cały cykl czas głosów jedzie równo w górę od ich własnego czasu.\nPowtórzenia grają niżej, coraz bardziej rozciągnięte.",
                "Czas maleje: przez cały cykl czas głosów jedzie równo w dół od ich własnego czasu.\nPowtórzenia grają wyżej i coraz gęściej." };
            const char* const* const kTips = oneWay ? kTipsOneWay : kTipsReturn;
            for (int m = 0; m < 3; ++m)
            {
                ImGui::PushID(2000 + m);
                ImGui::SetCursorPos(ImVec2(px + S(10) + S(46) * float(m), vy + S(32)));
                ImGui::PushStyleColor(ImGuiCol_Button, mode == m ? ImVec4(0.30f, 0.42f, 0.58f, 1.f) : ImGui::GetStyle().Colors[ImGuiCol_Button]);
                if (ImGui::Button(kSweepModeNames[m], ImVec2(S(43), S(26))) && mode != m) setOne(gSwMode, float(m));
                ImGui::PopStyleColor();
                if (ImGui::IsItemHovered() && GImGui->HoveredIdTimer > 0.6f) ImGui::SetTooltip("%s", kTips[m]);
                ImGui::PopID();
            }
            toggle("Tempo sync", gSwSync, px + S(10), vy + S(62), "Długość cyklu w wartościach nutowych względem tempa projektu zamiast w ms.");
            toggle("One way", gSwOneWay, px + S(10), vy + S(86),
                   "Wyłączone: czas skacze na początku cyklu i płynnie wraca do swojego.\n"
                   "Włączone: czas startuje ze swojego i jedzie tylko w jedną stronę, bez powrotu.\n"
                   "Na początku następnego cyklu zaczyna od nowa (przeskok jest wyciszony, trwa 3 ms).");
            knob("Depth", gSwDepth, px + S(148), ky, kColAccent,
                 oneWay ? "Jak daleko odjeżdża czas w jednym cyklu: 50% = Up kończy na półtora czasu, Down na połowie."
                        : "Jak daleko skacze czas: 50% = Up startuje z połowy czasu, Down z półtora czasu.");
            if (fP[gSwSync] > 0.5f)
                knob("Every", gSwDiv, px + S(214), ky, kColAccent, "Co ile powtarza się sweep, w wartościach nutowych. 1/8. = ósemka z kropką.");
            else
                knob("Every", gSwTime, px + S(214), ky, kColAccent, "Co ile powtarza się sweep.");
            knob("Glide", gSwGlide, px + S(280), ky, kColAccent,
                 oneWay ? "Jaką część cyklu trwa jazda. 100% = jedzie przez cały cykl; mniej = dojeżdża szybciej i stoi na końcu do następnego cyklu."
                        : "Jak długo trwa powrót czasu w ramach cyklu.\nMało = krótkie szarpnięcie na początku cyklu, 100% = zjazd przez cały cykl.");
        }

        // bottom row
        const float by = S(532), sy = by + S(28);
        panel(dl, wp, S(12), by, S(214), S(112), "SPREAD BETWEEN VOICES");
        knob("Time", gSpTime, S(18), sy, kColAccent, "Każdy kolejny włączony głos jest dłuższy o tyle procent swojego czasu.\n100% przy równych czasach = 1x, 2x, 3x... (multi-tap).");
        knob("Detune", gSpDetune, S(84), sy, kColAccent, "Rozstraja głosy symetrycznie w dół i w górę o tyle centów.");
        knob("Pan", gSpPan, S(150), sy, kColAccent, "Rozkłada głosy od lewej do prawej.");

        panel(dl, wp, S(234), by, S(214), S(112), "CHORUS ON REPEATS");
        knob("Detune", gChDetune, S(240), sy, kColAccent, "Głębokość chorusa na powtórzeniach: o tyle centów faluje ich strój. 0 = chorus wyłączony.\nSiedzi w pętli, więc każde kolejne powtórzenie jest bardziej rozmyte.");
        knob("Rate", gChRate, S(306), sy, kColAccent, "Tempo falowania chorusa.");
        knob("Mix", gChMix, S(372), sy, kColAccent, "50% = klasyczny chorus (głos prosty + rozstrojony), 100% = sam rozstrojony głos (falowanie jak na taśmie).");

        panel(dl, wp, S(456), by, S(222), S(112), "CRUSHER");
        knob("Bits", gBits, S(462), sy, kColAccent, "Rozdzielczość powtórzeń w bitach. Mniej bitów = więcej szumu i zgrzytu; przy 1-3 bitach ciche dźwięki znikają.");
        knob("Rate", gCrRate, S(528), sy, kColAccent, "Częstotliwość próbkowania powtórzeń. Niżej = metaliczne, \"telefoniczne\" brzmienie starego samplera.");
        toggle("In loop", gCrLoop, S(600), sy + S(26), "Włączone: crusher jest w pętli feedbacku, więc każde powtórzenie jest brudniejsze od poprzedniego.\nWyłączone: wszystkie powtórzenia są brudzone tak samo.");

        panel(dl, wp, S(686), by, S(148), S(112), "LOOP FILTER");
        knob("Lo cut", gLoCut, S(692), sy, kColAccent, "Obcina dół powtórzeń. W pętli: każde kolejne powtórzenie jest cieńsze.");
        knob("Hi cut", gHiCut, S(758), sy, kColAccent, "Obcina górę powtórzeń. W pętli: każde kolejne powtórzenie jest ciemniejsze.");

        panel(dl, wp, S(842), by, S(226), S(112), "RANDOM GLITCHES");
        knob("Chance", gGlProb, S(848), sy, kColAccent, "Szansa, że dane powtórzenie głosu dostanie losowy glitch. Losowanie jest raz na czas opóźnienia głosu.\nNapis na zakładce głosu pokazuje, co się właśnie dzieje.");
        toggle("Reverse", gGlRev, S(928), by + S(26), "Glitch: powtórzenie idzie od tyłu (głos z Reverse wraca na chwilę do przodu).");
        toggle("Half-time jump", gGlJump, S(928), by + S(53), "Glitch: czas głosu skacze na chwilę na połowę.");
        toggle("Stutter", gGlStut, S(928), by + S(80), "Glitch: głos zacina się na krótkim wycinku (1/4 lub 1/8 swojego czasu).");

        // footer: status message, or the mouse hints
        {
            const bool st = !fStatus.empty() && ImGui::GetTime() < fStatusUntil;
            dl->AddText(ImVec2(wp.x + S(16), wp.y + S(650)), st ? IM_COL32(190, 230, 190, 255) : kColDim,
                        st ? fStatus.c_str() : "Knobs: drag up/down, Shift = fine, double-click = default, mouse wheel.     Graph: drag a voice's first echo sideways = time, up/down = level.");
        }

        ImGui::End();
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SwarmDelayUI)
};

UI* createUI() { return new SwarmDelayUI(); }

END_NAMESPACE_DISTRHO
