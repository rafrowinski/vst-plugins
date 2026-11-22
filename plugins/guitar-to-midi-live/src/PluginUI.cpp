// Guitar to MIDI Live - user interface (Dear ImGui)
#include "MidiParams.hpp"
#include "../../../shared/guitar-engine/DetectorPresets.hpp"

#include "DistrhoUI.hpp"
#include "DearImGui/imgui_internal.h"

#include <string>
#include <vector>
#ifdef _WIN32
# include <windows.h>
#else
# include <sys/stat.h>
#endif

START_NAMESPACE_DISTRHO

using namespace swmidi;

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

static const ImU32 kColNote = IM_COL32(120, 200, 255, 255);
static const ImU32 kColBendLine = IM_COL32(255, 210, 120, 255);

class GuitarToMidiLiveUI : public UI
{
    float fP[kParamCount];
    float fScale = 1.f;
    float fDragNorm = 0.f;
    bool fOpenDet = false;
    enum { kBrowseNone, kBrowseDetExportCurrent, kBrowseDetExportAll, kBrowseDetImport };
    int fBrowse = kBrowseNone;
    // saved detector settings
    static constexpr int kDetIdx[detp::kCount] = { pGate, pCut, pWindow, pDetClarity, pDetLegato, pDetLow, pDetHarm, pDetProbation, pDetRepick, pDetRefract, pDetTail, pDetBridge };
    detp::Library fDetLib { detReservedNames() };
    char fDetName[64] = "";
    std::string fDetStatus, fDetDeleteName;
    bool fDetOpenSave = false;

    // what has been sent, sampled every time the window is drawn; drawn as a scrolling piano roll
    struct RollPoint { double t; int note; float bend; int serial; int vel; };
    std::vector<RollPoint> fRoll;
    double fLastActivityChange = -10.0;
    float fLastActivity = 0.f;
    float fShownNote = -1.f;
    double fShownUntil = 0.0;
    static constexpr double kRollSeconds = 8.0;

    // interface language for explanations: English by default, Polish on request (remembered between sessions)
    bool fLangPl = false;
    const char* T(const char* en, const char* pl) const { return fLangPl ? pl : en; }

    static FILE* openUtf8(const char* path, const char* mode)
    {
#ifdef _WIN32
        wchar_t wpath[2048], wmode[8];
        if (MultiByteToWideChar(CP_UTF8, 0, path, -1, wpath, 2048) == 0) return nullptr;
        MultiByteToWideChar(CP_UTF8, 0, mode, -1, wmode, 8);
        return _wfopen(wpath, wmode);
#else
        return std::fopen(path, mode);
#endif
    }
    static std::string settingsPath()
    {
#ifdef _WIN32
        const wchar_t* const appdata = _wgetenv(L"APPDATA");
        if (appdata == nullptr) return std::string();
        std::wstring dir = std::wstring(appdata) + L"\\Guitar to MIDI Live";
        CreateDirectoryW(dir.c_str(), nullptr);
        dir += L"\\settings.txt";
        char buf[4096];
        if (WideCharToMultiByte(CP_UTF8, 0, dir.c_str(), -1, buf, sizeof(buf), nullptr, nullptr) == 0) return std::string();
        return buf;
#else
        const char* const home = std::getenv("HOME");
        if (home == nullptr) return std::string();
        std::string dir = std::string(home) + "/.config";
        mkdir(dir.c_str(), 0755);
        dir += "/Guitar to MIDI Live";
        mkdir(dir.c_str(), 0755);
        return dir + "/settings.txt";
#endif
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
    GuitarToMidiLiveUI() : UI(DISTRHO_UI_DEFAULT_WIDTH, DISTRHO_UI_DEFAULT_HEIGHT)
    {
        fScale = float(getScaleFactor());
        if (fScale > 1.01f)
            setSize(uint(DISTRHO_UI_DEFAULT_WIDTH * fScale), uint(DISTRHO_UI_DEFAULT_HEIGHT * fScale));
        setFontSize(14.f);

        presetDefaults(fP);
        for (int i = kInputCount; i < kParamCount; ++i) fP[i] = 0.f;
        fP[oNote] = -1.f;
        loadSettings();
        fDetLib.load();
        fRoll.reserve(4096);

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
        st.Colors[ImGuiCol_SliderGrab] = ImVec4(0.47f, 0.78f, 1.f, 1.f);
    }

protected:
    void parameterChanged(uint32_t index, float value) override
    {
        if (index >= kParamCount) return;
        if (fP[index] == value) return;
        fP[index] = value;
        repaint();
    }

    // keeps the roll scrolling while something was played in the last few seconds
    void uiIdle() override
    {
        if (!fRoll.empty() && ImGui::GetTime() - fRoll.back().t < kRollSeconds + 1.0) repaint();
    }

    // ----------------------------------------------------------------------------------------------------------------
    // parameter helpers

    void beginEdit(int idx) { editParameter(idx, true); }
    void endEdit(int idx) { editParameter(idx, false); }
    void setValue(int idx, float v) { fP[idx] = v; setParameterValue(idx, v); }
    void setOne(int idx, float v)
    {
        editParameter(idx, true);
        fP[idx] = v; setParameterValue(idx, v);
        editParameter(idx, false);
    }
    void applyValues(const float* v)
    {
        for (int i = 0; i < kInputCount; ++i)
            if (fP[i] != v[i]) setOne(i, v[i]);
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
        if (idx == pOctave) std::snprintf(b, n, "%+d oct", int(std::lround(v)));
        else if (idx == pSemi) std::snprintf(b, n, "%+d st", int(std::lround(v)));
        else if (idx == pVelMin || idx == pVelMax) std::snprintf(b, n, "%d", int(std::lround(v)));
        else if (idx == pVelCurve)
        {
            if (std::fabs(v) < 0.5f) std::snprintf(b, n, "linear");
            else std::snprintf(b, n, "%s %.0f", v < 0 ? "soft" : "hard", std::fabs(v));
        }
        else if (idx == pVelFull) std::snprintf(b, n, "%.0f dBFS", v);
        else if (!std::strcmp(u, "ms"))
        {
            if (v < 100.f) std::snprintf(b, n, "%.1f ms", v);
            else std::snprintf(b, n, "%.0f ms", v);
        }
        else if (!std::strcmp(u, "%"))   std::snprintf(b, n, "%.0f %%", v);
        else if (!std::strcmp(u, "dB"))  std::snprintf(b, n, "%+.1f dB", v);
        else if (!std::strcmp(u, "per")) std::snprintf(b, n, "%.2f per", v);
        else if (!std::strcmp(u, "x"))   std::snprintf(b, n, "x %.2f", v);
        else std::snprintf(b, n, "%.2f", v);
    }

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
        std::snprintf(shown, sizeof(shown), "%s%s", "", val);
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
        const std::string preview = std::string("") + names[cur];
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
            if (changed) { fP[idx] = v; setParameterValue(idx, v); }
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

        ImGui::BeginChild("##detL", ImVec2(S(456), S(480)));
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColAccent), "%s", T("A NEW PICK", "NOWE UDERZENIE"));
        detSetting(pDetRepick, "Pick sensitivity",
                   T("How far the level has to jump above what it was a moment ago to count as a new pick. Lower: catches soft and dense picking, but the wobble of a ringing string can pass for a pick. Higher: only clear picks; a quiet note after a loud one is then handled as legato, which is slower.", "O ile głośność musi skoczyć względem poziomu sprzed chwili, żeby uznać to za nowe uderzenie. "
                   "Mniej: łapie delikatne i gęste uderzenia, ale falowanie głośności wybrzmiewającej struny może udawać uderzenie. "
                   "Więcej: tylko wyraźne uderzenia; cicha nuta po głośnej przechodzi wtedy jako legato, czyli wolniej."));
        detSetting(pDetRefract, "Pick spacing",
                   T("The shortest gap between two picks. Lower: keeps up with tremolo picking and very fast runs. Higher: one pick is never counted twice.", "Najkrótszy odstęp między dwoma uderzeniami. Mniej: nadąża za tremolo i bardzo szybkimi biegnikami. "
                   "Więcej: jedno uderzenie nigdy nie zostanie policzone podwójnie."));
        detSetting(pDetClarity, "Clarity",
                   T("How periodic the signal has to be before a pitch reading is accepted. Lower: notes are caught sooner, also through a dirty attack, but noise and two strings at once give false notes more often. Higher: clean readings only; a note played over ringing strings starts later.", "Jak bardzo okresowy musi być sygnał, żeby odczyt wysokości został przyjęty. "
                   "Mniej: nuty łapią się szybciej, także przy brudnym ataku, ale szum i dwie struny naraz częściej dają fałszywą nutę. "
                   "Więcej: tylko czyste odczyty; nuta zagrana na tle wybrzmiewających strun startuje później."));
        detSetting(pDetLow, "Low note time",
                   T("How long, after a pick, a note more than a fifth below the previous one has to keep confirming. Right after a pick the old and the new string sound together and for a moment look like one low note (typically a false low note for about 30 ms). Lower: big leaps down are caught sooner. Higher: fewer false low notes.", "Jak długo po uderzeniu musi się potwierdzać nuta niższa od poprzedniej o więcej niż kwintę. "
                   "Tuż po uderzeniu stara i nowa struna brzmią razem i przez moment wyglądają jak jedna niska nuta "
                   "(typowo: fałszywa niska nuta na ok. 30 ms). Mniej: duże skoki w dół łapią się szybciej. Więcej: mniej fałszywych niskich nut."));
        detSetting(pDetProbation, "First-reading time",
                   T("The first reading of a note is made from a very short piece of signal and is sometimes wrong. For this long it is held loosely: if a lower note keeps showing up, it replaces the first reading quickly. 0 = the first reading is final at once.", "Pierwszy odczyt nuty powstaje z bardzo krótkiego kawałka sygnału i bywa błędny. Przez ten czas jest traktowany luźno: "
                   "jeśli uporczywie pojawia się niższa nuta, zastępuje go szybko. 0 = pierwszy odczyt obowiązuje od razu."));
        ImGui::EndChild();
        ImGui::SameLine(0, S(16));
        ImGui::BeginChild("##detR", ImVec2(S(456), S(480)));
        ImGui::TextColored(ImGui::ColorConvertU32ToFloat4(kColAccent), "%s", T("NOTE CHANGES WITHOUT A PICK, AND TAILS", "ZMIANA NUTY BEZ UDERZENIA I KOŃCÓWKI"));
        detSetting(pDetLegato, "Legato time",
                   T("How long a new pitch with no pick (hammer-on, pull-off, slide, tapping) has to hold before the synth moves to it. Lower: legato responds at once, but the moment where both notes sound together produces short false notes. Higher: clean transitions, but slower. For comparison: a commercial guitar synth that was measured needed about 40-50 ms here.", "Jak długo nowa wysokość bez uderzenia (hammer-on, pull-off, slide, tapping) musi się utrzymać, zanim syntezator na nią przejdzie. "
                   "Mniej: legato reaguje od razu, ale chwila, w której brzmią obie nuty naraz, daje krótkie fałszywe dźwięki. "
                   "Więcej: czyste przejścia, ale wolniejsze. Dla porównania: zmierzony komercyjny syntezator gitarowy potrzebował tu ok. 40-50 ms."));
        detSetting(pDetHarm, "Octave time",
                   T("How long a jump of an octave or a fifth with no new pick has to last. Such a jump is far more often a loud harmonic than a new note. Lower: faster octave hammer-ons and harmonics. Higher: fewer octave jumps. A note far below the one playing waits just as long when its pick was not noticed.", "Jak długo skok o oktawę lub kwintę bez nowego uderzenia musi trwać. Taki skok to najczęściej głośna harmoniczna, a nie nowa nuta. "
                   "Mniej: szybsze oktawowe hammer-ony i flażolety. Więcej: mniej przeskoków o oktawę. "
                   "Tyle samo czeka nuta dużo niższa od granej, jeśli uderzenie nie zostało zauważone."));
        detSetting(pDetTail, "Tail hold",
                   T("Once a note has faded below this fraction of its peak, its pitch is frozen: in the tail the fundamental fades first and harmonics and neighbouring strings are left. Lower: bends are followed for longer. Higher: steadier tails.", "Gdy nuta ścichnie poniżej tej części swojego szczytu, wysokość zostaje zamrożona: w końcówce podstawa zanika pierwsza "
                   "i zostają harmoniczne oraz sąsiednie struny. Mniej: podciągnięcia są śledzone dłużej. Więcej: stabilniejsze końcówki."));
        detSetting(pDetBridge, "Bridge notes",
                   T("On: between notes the previous note keeps playing until the new one is recognised - no holes in the sound. Off: silence while a note is being recognised (usually 5-25 ms), every note starts on its own, clearer articulation.", "Włączone: między nutami gra poprzednia nuta, aż nowa zostanie rozpoznana - bez dziur w dźwięku. "
                   "Wyłączone: cisza na czas rozpoznawania (zwykle 5-25 ms), każda nuta zaczyna się osobno, wyraźniejsza artykulacja."));
        ImGui::PushStyleColor(ImGuiCol_Text, ImGui::ColorConvertU32ToFloat4(kColDim));
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + S(440));
        ImGui::TextUnformatted(T("On the main panel: Window (how many periods of the note the detector compares - the main speed / safety control), Gate (input threshold) and Cut (cutting off the quiet tail).", "Na panelu głównym: Window (ile okresów nuty detektor porównuje - główny suwak szybkość / pewność), "
                               "Gate (próg wejścia) i Cut (ucinanie cichej końcówki)."));
        ImGui::PopTextWrapPos();
        ImGui::PopStyleColor();
        ImGui::EndChild();

        ImGui::TextDisabled("%s", T("These settings are saved with the project.", "Ustawienia zapisują się z projektem."));
        ImGui::SameLine(S(838));
        if (ImGui::Button("Close", ImVec2(S(90), 0))) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    // ----------------------------------------------------------------------------------------------------------------
    // piano roll of what was sent, plus the current note

    void recordRoll()
    {
        const double now = ImGui::GetTime();
        RollPoint pt { now, int(std::lround(fP[oNote])), fP[oBend], int(std::lround(fP[oSerial])), int(std::lround(fP[oVelocity])) };
        if (!fRoll.empty())
        {
            const RollPoint& l = fRoll.back();
            // nothing to add while silent
            if (pt.note < 0 && l.note < 0) { fRoll.back().t = now; return; }
        }
        fRoll.push_back(pt);
        size_t drop = 0;
        while (drop < fRoll.size() && now - fRoll[drop].t > kRollSeconds + 0.5) ++drop;
        if (drop > 512 || fRoll.size() > 8000) fRoll.erase(fRoll.begin(), fRoll.begin() + std::min(drop, fRoll.size()));
        if (fP[oActivity] != fLastActivity) { fLastActivity = fP[oActivity]; fLastActivityChange = now; }
    }

    void roll(ImDrawList* dl, const ImVec2& wp, float x, float y, float w, float h)
    {
        const ImVec2 a(wp.x + x, wp.y + y), b(wp.x + x + w, wp.y + y + h);
        dl->AddRectFilled(a, b, kColPanel, S(6));
        const char* const title = T("MIDI SENT", "WYSŁANE MIDI");
        dl->AddText(ImVec2(a.x + S(10), a.y + S(6)), kColAccent, title);
        dl->AddText(ImVec2(a.x + S(22) + ImGui::CalcTextSize(title).x, a.y + S(6)), kColDim,
                    T("last 8 seconds; bar = note, its brightness = velocity, orange line = pitch bend",
                      "ostatnie 8 sekund; pasek = nuta, jasność = velocity, pomarańczowa linia = pitch bend"));

        const float sideW = S(180);
        const ImVec2 g0(a.x + S(42), a.y + S(28)), g1(b.x - sideW - S(12), b.y - S(12));
        dl->AddRectFilled(g0, g1, kColGraph);
        const double now = ImGui::GetTime();
        const float gw = g1.x - g0.x, gh = g1.y - g0.y;

        // vertical range: the notes in view, at least two octaves
        int lo = 128, hi = -1;
        for (const RollPoint& p : fRoll)
            if (p.note >= 0 && now - p.t < kRollSeconds) { lo = std::min(lo, p.note + int(std::floor(p.bend))); hi = std::max(hi, p.note + int(std::ceil(p.bend))); }
        if (hi < 0) { lo = 52; hi = 76; }
        lo -= 2; hi += 2;
        if (hi - lo < 24) { const int mid = (lo + hi) / 2; lo = mid - 12; hi = mid + 12; }
        const float rowH = gh / float(hi - lo + 1);
        const auto yOf = [&](float note) { return g1.y - (note - float(lo) + 0.5f) * rowH; };
        const auto xOf = [&](double t) { return g1.x - float((now - t) / kRollSeconds) * gw; };

        // grid: a line for every C, shaded rows for the black keys
        char nm[16];
        for (int n = lo; n <= hi; ++n)
        {
            const int pc = ((n % 12) + 12) % 12;
            const bool black = pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
            if (black) dl->AddRectFilled(ImVec2(g0.x, yOf(float(n)) - rowH * 0.5f), ImVec2(g1.x, yOf(float(n)) + rowH * 0.5f), IM_COL32(255, 255, 255, 8));
            if (pc == 0 || pc == 4 || pc == 7)
            {
                if (pc == 0) dl->AddLine(ImVec2(g0.x, yOf(float(n)) + rowH * 0.5f), ImVec2(g1.x, yOf(float(n)) + rowH * 0.5f), kColGrid);
                const ImVec2 ts = ImGui::CalcTextSize(noteName(n, nm, sizeof(nm)));
                if (rowH * 3.f >= ts.y || pc == 0)
                    dl->AddText(ImVec2(g0.x - ts.x - S(5), yOf(float(n)) - ts.y * 0.5f), pc == 0 ? kColText : kColDim, nm);
            }
        }
        for (int s = 1; s < int(kRollSeconds); ++s)
        {
            const float xx = std::floor(g1.x - gw * float(s) / float(kRollSeconds)) + 0.5f;
            dl->AddLine(ImVec2(xx, g0.y), ImVec2(xx, g1.y), IM_COL32(255, 255, 255, 10));
        }

        // notes: runs of samples with the same note and counter
        dl->PushClipRect(g0, g1, true);
        size_t i = 0;
        static ImVec2 line[8192];
        while (i < fRoll.size())
        {
            if (fRoll[i].note < 0) { ++i; continue; }
            size_t j = i;
            while (j + 1 < fRoll.size() && fRoll[j + 1].note == fRoll[i].note && fRoll[j + 1].serial == fRoll[i].serial) ++j;
            const double t1 = j + 1 < fRoll.size() ? fRoll[j + 1].t : fRoll[j].t;
            const float x0 = xOf(fRoll[i].t), x1 = std::max(x0 + S(2), xOf(t1));
            if (x1 > g0.x)
            {
                const float yy = yOf(float(fRoll[i].note));
                const float alpha = 0.35f + 0.65f * float(fRoll[i].vel) / 127.f;
                dl->AddRectFilled(ImVec2(x0, yy - rowH * 0.42f), ImVec2(x1, yy + rowH * 0.42f), withAlpha(kColNote, alpha), S(2));
                dl->AddLine(ImVec2(x0, yy - rowH * 0.42f), ImVec2(x0, yy + rowH * 0.42f), kColText, S(1.5f));
                // pitch bend as a line over the bar
                int nl = 0; bool bent = false;
                for (size_t k = i; k <= j && nl < 8190; ++k)
                {
                    line[nl++] = ImVec2(xOf(fRoll[k].t), yOf(float(fRoll[k].note) + fRoll[k].bend));
                    if (std::fabs(fRoll[k].bend) > 0.08f) bent = true;
                }
                line[nl++] = ImVec2(x1, line[nl - 1].y);
                if (bent && nl > 1) dl->AddPolyline(line, nl, kColBendLine, 0, S(1.5f));
            }
            i = j + 1;
        }
        dl->PopClipRect();
        if (fRoll.empty() || fP[oActivity] == 0.f)
            centeredText(dl, 0.5f * (g0.x + g1.x), 0.5f * (g0.y + g1.y) - S(8), kColDim,
                         T("Play single notes - what is sent appears here", "Graj pojedyncze nuty - wysłane MIDI pojawi się tutaj"));

        // ---- the current note
        const float sx = g1.x + S(12), sw = b.x - S(12) - sx, scx = sx + sw * 0.5f;
        dl->AddRectFilled(ImVec2(sx, g0.y), ImVec2(b.x - S(12), g1.y), kColGraph);
        const int note = int(std::lround(fP[oNote]));
        if (note >= 0) { fShownNote = float(note); fShownUntil = now + 0.6; }
        const bool live = note >= 0 || now < fShownUntil;
        char txt[64];
        centeredText(dl, scx, g0.y + S(8), kColDim, note >= 0 ? T("SENDING", "WYSYŁA") : T("LAST NOTE", "OSTATNIA NUTA"));
        {
            ImFont* const font = ImGui::GetFont();
            const float fs = S(44);
            noteName(live ? int(fShownNote) : -1, nm, sizeof(nm));
            const ImVec2 sz = font->CalcTextSizeA(fs, 1e4f, 0.f, nm);
            dl->AddText(font, fs, ImVec2(std::floor(scx - sz.x * 0.5f), g0.y + S(26)), note >= 0 ? kColText : kColTrack, nm);
        }
        if (note >= 0 && fP[pBends] < 0.5f) std::snprintf(txt, sizeof(txt), "bend %+.2f st", fP[oBend]);
        else if (note >= 0) std::snprintf(txt, sizeof(txt), "%.1f Hz", fP[oHz]);
        else std::snprintf(txt, sizeof(txt), " ");
        centeredText(dl, scx, g0.y + S(76), kColDim, txt);
        std::snprintf(txt, sizeof(txt), "velocity %d", int(fP[oVelocity]));
        centeredText(dl, scx, g0.y + S(100), live ? kColText : kColDim, txt);
        std::snprintf(txt, sizeof(txt), T("found after %.1f ms", "rozpoznana po %.1f ms"), fP[oLatency]);
        centeredText(dl, scx, g0.y + S(120), kColDim, fP[oLatency] > 0.f ? txt : " ");
        std::snprintf(txt, sizeof(txt), T("channel %d", "kanał %d"), int(fP[pChannel]) + 1);
        centeredText(dl, scx, g0.y + S(140), kColDim, txt);

        // input level in dB with the gate threshold marked
        {
            const float my = g1.y - S(44), mx0 = sx + S(14), mx1 = sx + sw - S(14);
            const auto pos = [&](float db) { return mx0 + (mx1 - mx0) * std::min(1.f, std::max(0.f, (db + 80.f) / 80.f)); };
            const float lv = fP[oLevel] / 2.5f;
            const float db = lv > 1e-5f ? 20.f * std::log10(lv) : -100.f;
            const bool open = fP[oGate] > 0.5f;
            dl->AddRectFilled(ImVec2(mx0, my), ImVec2(mx1, my + S(8)), kColTrack, S(2));
            if (db > -80.f) dl->AddRectFilled(ImVec2(mx0, my), ImVec2(pos(db), my + S(8)), open ? kColAccent : kColDim, S(2));
            const float gx = pos(fP[pGate]);
            dl->AddRectFilled(ImVec2(gx - S(1), my - S(4)), ImVec2(gx + S(1), my + S(12)), kColText);
            centeredText(dl, scx, my + S(16), kColDim, open ? T("gate open", "bramka otwarta") : T("gate closed", "bramka zamknięta"));
        }
    }

    // ----------------------------------------------------------------------------------------------------------------

    void onImGuiDisplay() override
    {
        recordRoll();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(getWidth(), getHeight()));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImGui::ColorConvertU32ToFloat4(kColBg));
        ImGui::Begin("G2ML", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings
                                        | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoScrollWithMouse);
        ImGui::PopStyleVar(3);
        ImGui::PopStyleColor();

        ImDrawList* const dl = ImGui::GetWindowDrawList();
        const ImVec2 wp = ImGui::GetWindowPos();

        // ---- header
        ImGui::SetCursorPos(ImVec2(S(16), S(13)));
        ImGui::TextColored(ImVec4(0.47f, 0.78f, 1.f, 1.f), "GUITAR TO MIDI LIVE");
        ImGui::SameLine(0, S(12));
        ImGui::TextDisabled("%s", T("play single notes; put a synth after this plugin on the same track",
                                    "graj pojedyncze nuty; syntezator wstaw za tą wtyczką na tym samym torze"));
        ImGui::SetCursorPos(ImVec2(getWidth() - S(120), S(10)));
        if (ImGui::Button(fLangPl ? "PL" : "EN", ImVec2(S(34), 0))) { fLangPl = !fLangPl; saveSettings(); }
        tip("Language of the explanations: English / Polish. Click to switch.\nJęzyk opisów: angielski / polski. Kliknij, żeby przełączyć.");
        {
            const bool lit = ImGui::GetTime() - fLastActivityChange < 0.08;
            const ImVec2 c(wp.x + getWidth() - S(22), wp.y + S(21));
            dl->AddCircleFilled(c, S(6), lit ? IM_COL32(255, 90, 70, 255) : IM_COL32(70, 40, 40, 255));
            dl->AddText(ImVec2(c.x - S(42), c.y - S(8)), kColDim, "MIDI");
        }

        roll(dl, wp, S(12), S(44), S(976), S(270));

        // ---- detector
        const float by = S(322), bh = S(262);
        const float dx = S(12), dw = S(318);
        panel(dl, wp, dx, by, dw, bh, T("NOTE DETECTION", "WYKRYWANIE NUT"));
        const float k1 = by + S(30);
        knob("Gate", pGate, dx + S(8), k1, kColAccent,
             T("The threshold above which the guitar plays notes (the white mark on the level meter).\nToo low: noise and hum trigger random notes. Too high: notes break off too early.",
               "Próg, powyżej którego gitara gra nuty (biała kreska na wskaźniku poziomu).\nZa nisko: szum i przydźwięk wyzwalają przypadkowe nuty. Za wysoko: nuty urywają się za wcześnie."));
        knob("Cut", pCut, dx + S(74), k1, kColAccent,
             T("Sends the note-off once the note has faded by this many dB from its loudest moment.\nIn the tail the pitch is no longer certain. Closer to 0 dB = shorter notes. -60 dB = almost to the end.\nWith Hold on the note stays on; only its pitch stops being followed.",
               "Wysyła note-off, gdy nuta ścichnie o tyle dB względem swojego najgłośniejszego momentu.\nW końcówce wysokość przestaje być pewna. Bliżej 0 dB = krótsze nuty. -60 dB = prawie do końca.\nZ włączonym Hold nuta zostaje włączona, tylko jej wysokość przestaje być śledzona."));
        knob("Window", pWindow, dx + S(140), k1, kColAccent,
             T("How many periods of the note the detector compares before it believes a pitch.\nLower = less delay, but below 1.0 it clearly gets notes wrong more often. Higher = safer and slower.",
               "Ile okresów nuty detektor porównuje, zanim uwierzy w wysokość.\nMniej = krótsze opóźnienie, ale poniżej 1.0 wyraźnie częściej myli nuty. Więcej = pewniej i wolniej."));
        toggle("Hold", pHold, dx + S(222), k1 + S(4),
               T("Like a held key: the note stays on after the string has died away.\nIt ends with the next note, or when you mute the strings while they still ring.",
                 "Jak przytrzymany klawisz: nuta zostaje włączona po wybrzmieniu struny.\nKończy ją następna nuta albo wytłumienie strun ręką, póki jeszcze brzmią."));
        const float k2 = by + S(118);
        dl->AddText(ImVec2(wp.x + dx + S(12), wp.y + k2), kColDim, T("Detector profile:", "Profil detektora:"));
        for (int i = 0; i < kDetProfileCount; ++i)
        {
            ImGui::SetCursorPos(ImVec2(dx + S(12), k2 + S(22) + S(28) * float(i)));
            ImGui::PushID(9000 + i);
            if (ImGui::Button(kDetProfiles[i].name, ImVec2(S(150), 0)))
            {
                float v[kInputCount];
                std::memcpy(v, fP, sizeof(v));
                applyDetProfile(v, kDetProfiles[i]);
                applyValues(v);
            }
            ImGui::PopID();
        }
        ImGui::SetCursorPos(ImVec2(dx + S(176), k2 + S(22)));
        if (ImGui::Button("Detector...", ImVec2(S(128), S(52)))) fOpenDet = true;
        tip(T("All detector settings, each with an explanation of what it does and why.",
              "Wszystkie ustawienia detektora, każde z opisem, co robi i dlaczego."));
        ImGui::SetCursorPos(ImVec2(dx + S(12), by + bh - S(30)));
        ImGui::PushTextWrapPos(dx + dw - S(12));
        ImGui::TextDisabled("%s", T("Fast solo: quicker changes, more false notes.", "Fast solo: szybsze zmiany, więcej fałszywych nut."));
        ImGui::PopTextWrapPos();

        // ---- MIDI out
        const float mx = S(338), mw = S(650);
        panel(dl, wp, mx, by, mw, bh, T("MIDI OUT", "WYJŚCIE MIDI"));
        const float r1 = by + S(30);
        dl->AddText(ImVec2(wp.x + mx + S(12), wp.y + r1 + S(2)), kColDim, T("Channel", "Kanał"));
        combo(pChannel, kChannelNames, 16, mx + S(12), r1 + S(22), S(64),
              T("MIDI channel the notes are sent on.", "Kanał MIDI, na którym idą nuty."));
        knob("Octave", pOctave, mx + S(84), r1, kColAccent,
             T("Shifts every note by whole octaves, e.g. -1 to play a bass synth from the guitar.",
               "Przesuwa każdą nutę o całe oktawy, np. -1, żeby grać z gitary na basowym syntezatorze."));
        knob("Semitones", pSemi, mx + S(150), r1, kColAccent,
             T("Shifts every note by semitones. +7 = a fifth up, -5 = a fourth down.",
               "Przesuwa każdą nutę o półtony. +7 = kwinta w górę, -5 = kwarta w dół."));
        dl->AddText(ImVec2(wp.x + mx + S(232), wp.y + r1 + S(2)), kColDim, T("Bends and vibrato", "Podciągnięcia i vibrato"));
        combo(pBends, kBendNames, kBendsCount, mx + S(232), r1 + S(22), S(150),
              T("Pitch bend: bends, slides and vibrato go out as pitch bend - set the same bend range in your synth.\n"
                "Note steps: the pitch is rounded to semitones and every step is a new note (like a keyboard).\n"
                "Ignore: one note per pick, its pitch never moves.",
                "Pitch bend: podciągnięcia, slajdy i vibrato idą jako pitch bend - ustaw ten sam zakres w syntezatorze.\n"
                "Note steps: wysokość zaokrąglona do półtonów, każdy krok to nowa nuta (jak na klawiszach).\n"
                "Ignore: jedna nuta na uderzenie, jej wysokość się nie zmienia."));
        dl->AddText(ImVec2(wp.x + mx + S(392), wp.y + r1 + S(2)), kColDim, T("Bend range", "Zakres bendu"));
        combo(pBendRange, kBendRangeNames, kBendRangeCount, mx + S(392), r1 + S(22), S(110),
              T("Must match the pitch bend range set in the synth, otherwise bends come out too wide or too narrow.\n"
                "+/- 2 is the usual default; +/- 12 lets you bend further than a tone and play wide slides.",
                "Musi się zgadzać z zakresem pitch bendu w syntezatorze, inaczej podciągnięcia wyjdą za szerokie albo za wąskie.\n"
                "+/- 2 to zwykle domyślne; +/- 12 pozwala podciągać dalej niż o cały ton i grać szerokie slajdy."));
        dl->AddText(ImVec2(wp.x + mx + S(512), wp.y + r1 + S(2)), kColDim, T("Legato", "Legato"));
        combo(pLegato, kLegatoNames, kLegatoCount, mx + S(512), r1 + S(22), S(126),
              T("What a note change without a pick (hammer-on, pull-off, slide) sends.\n"
                "Retrigger: note-off, then note-on - every note starts its envelope again.\n"
                "Overlap: note-on first, then note-off - a mono synth in legato mode glides and keeps its envelope.",
                "Co wysyła zmiana nuty bez uderzenia (hammer-on, pull-off, slajd).\n"
                "Retrigger: note-off, potem note-on - każda nuta startuje obwiednię od nowa.\n"
                "Overlap: najpierw note-on, potem note-off - monofoniczny syntezator w trybie legato płynnie przechodzi i trzyma obwiednię."));
        if (fP[pBends] < 0.5f)
            dl->AddText(ImVec2(wp.x + mx + S(232), wp.y + r1 + S(52)), kColDim,
                        T("set the same range in the synth", "ustaw ten sam zakres w syntezatorze"));

        const float r2 = by + S(112);
        dl->AddText(ImVec2(wp.x + mx + S(12), wp.y + r2 - S(8)), kColAccent, "VELOCITY");
        knob("Full at", pVelFull, mx + S(8), r2 + S(12), kColAccent,
             T("Input peak at which a pick reaches the top velocity. Set it to your hardest picks.\n"
               "In the recordings tested, picks peaked between -30 and -12 dBFS.",
               "Szczyt wejścia, przy którym uderzenie daje najwyższe velocity. Ustaw na swoje najmocniejsze uderzenia.\n"
               "W sprawdzanych nagraniach uderzenia miały szczyty od -30 do -12 dBFS."));
        knob("Range", pVelRange, mx + S(74), r2 + S(12), kColAccent,
             T("How many dB below \"Full at\" the softest pick is (it gets the minimum velocity).\n"
               "Smaller = small changes in picking strength make big velocity changes.",
               "O ile dB poniżej \"Full at\" jest najsłabsze uderzenie (dostaje minimalne velocity).\n"
               "Mniej = niewielkie zmiany siły uderzenia dają duże zmiany velocity."));
        knob("Curve", pVelCurve, mx + S(140), r2 + S(12), kColAccent,
             T("Soft: medium picks already give high velocity. Hard: high velocity only for strong picks.",
               "Soft: średnie uderzenia dają już wysokie velocity. Hard: wysokie velocity tylko przy mocnych uderzeniach."));
        knob("Min", pVelMin, mx + S(206), r2 + S(12), kColAccent,
             T("Lowest velocity sent. Set Min = Max for a fixed velocity.", "Najniższe wysyłane velocity. Min = Max daje stałe velocity."));
        knob("Max", pVelMax, mx + S(272), r2 + S(12), kColAccent, T("Highest velocity sent.", "Najwyższe wysyłane velocity."));

        dl->AddText(ImVec2(wp.x + mx + S(370), wp.y + r2 - S(8)), kColAccent, T("EXPRESSION", "EKSPRESJA"));
        dl->AddText(ImVec2(wp.x + mx + S(370), wp.y + r2 + S(12)), kColDim, T("Playing level as", "Poziom grania jako"));
        combo(pExpr, kExprNames, kExprCount, mx + S(370), r2 + S(32), S(176),
              T("Sends the playing level continuously as a controller, so the synth can follow how the string fades or swells.\n"
                "CC 11 (expression) or CC 7 (volume) for loudness, CC 74 for filter brightness, CC 1 / CC 2 for whatever the synth maps them to.",
                "Wysyła poziom grania na bieżąco jako kontroler, żeby syntezator podążał za wybrzmiewaniem i narastaniem struny.\n"
                "CC 11 (expression) albo CC 7 (głośność) dla głośności, CC 74 dla jasności filtra, CC 1 / CC 2 do tego, co syntezator do nich przypisze."));
        knob("Gain", pExprGain, mx + S(556), r2 + S(12), kColAccent,
             T("Boosts the level before it becomes the controller value. More = the controller stays high for longer as the string fades.",
               "Wzmacnia poziom, zanim stanie się wartością kontrolera. Więcej = kontroler dłużej zostaje wysoko, gdy struna wybrzmiewa."),
             fP[pExpr] < 0.5f);

        toggle(T("Guitar through", "Gitara dalej"), pThru, mx + S(12), by + bh - S(34),
               T("On: the guitar sound goes on to the next plugin. Off: only MIDI goes on (the plugin outputs silence).\n"
                 "Note that most synths replace the audio they receive with their own sound.",
                 "Włączone: dźwięk gitary idzie dalej do następnej wtyczki. Wyłączone: dalej idzie tylko MIDI (wtyczka wypuszcza ciszę).\n"
                 "Uwaga: większość syntezatorów zastępuje otrzymany dźwięk własnym."));
        dl->AddText(ImVec2(wp.x + mx + S(180), wp.y + by + bh - S(31)), kColDim,
                    T("MIDI coming in on the track is passed on unchanged.", "MIDI przychodzące na tor przechodzi dalej bez zmian."));

        detectorWindow();

        dl->AddText(ImVec2(wp.x + S(16), wp.y + S(594)), kColDim,
                    T("Knobs: drag up/down, Shift = fine, double-click = default, mouse wheel.     Hold the mouse over a control for an explanation.",
                      "Pokrętła: przeciągnij góra/dół, Shift = dokładnie, podwójne kliknięcie = domyślnie, kółko myszy.     Przytrzymaj mysz nad elementem, żeby zobaczyć opis."));
        ImGui::End();
    }

    void uiFileBrowserSelected(const char* filename) override
    {
        const int mode = fBrowse;
        fBrowse = kBrowseNone;
        if (filename == nullptr || filename[0] == '\0' || mode == kBrowseNone) return;
        detFileSelected(mode, filename);
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GuitarToMidiLiveUI)
};

UI* createUI() { return new GuitarToMidiLiveUI(); }

END_NAMESPACE_DISTRHO
