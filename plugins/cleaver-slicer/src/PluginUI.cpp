// Cleaver Slicer - user interface (Dear ImGui).

#include "DistrhoUI.hpp"
#include "SlicerShared.hpp"
#include "UserPresets.hpp"

#include <string>

START_NAMESPACE_DISTRHO

using namespace cleaver;

namespace {

const ImU32 cBg = IM_COL32(20, 21, 25, 255);
const ImU32 cPanel = IM_COL32(32, 34, 41, 255);
const ImU32 cLane = IM_COL32(24, 25, 30, 255);
const ImU32 cGrid = IM_COL32(58, 61, 72, 255);
const ImU32 cGridBeat = IM_COL32(96, 100, 116, 255);
const ImU32 cText = IM_COL32(228, 230, 236, 255);
const ImU32 cDim = IM_COL32(140, 145, 160, 255);
const ImU32 cAccent = IM_COL32(255, 176, 32, 255);
const ImU32 cLevel = IM_COL32(255, 176, 32, 255);
const ImU32 cPitch = IM_COL32(90, 209, 230, 255);
const ImU32 cFilt = IM_COL32(192, 132, 252, 255);
const ImU32 cPan = IM_COL32(123, 216, 143, 255);

ImU32 withAlpha(ImU32 c, int a) { return (c & 0x00FFFFFFu) | ((ImU32)a << 24); }

const char* const kTips[kNumInputParams] = {
    "Sync: tempo i pozycja z hosta (REAPER). Gdy projekt gra, kroki trzymają się siatki taktu.\nWyłączone: wtyczka używa własnego tempa (pokrętło Tempo / Tap).",
    "Tempo wewnętrzne. Działa, gdy Sync jest wyłączony.",
    "Długość jednego kroku (T = triola).",
    "Liczba kroków w patternie (1–32).",
    "Attack: kształt początku każdego cięcia.\n0 = twarde siekanie, wysoko = miękkie falowanie jak tremolo.",
    "Duty: jak długo trwa cięcie w obrębie kroku.\nNisko = krótkie stuknięcia, 100% = krok gra do końca.",
    "Release: czas wygaszania cięcia.",
    "Depth: jak głęboko bramka zamyka dźwięk między cięciami.\n100% = pełna cisza.",
    "Swing: opóźnia co drugi krok (66% ≈ triolowy shuffle).",
    "Balance: proporcja dźwięku czystego i pociętego\n(0% = sam czysty, 100% = sam efekt).",
    "Głośność wyjściowa.",
    "Tryb stereo:\nFixed – środek\nPattern – panorama z linii PAN\nPing-Pong – cięcia na zmianę L/P\nRandom – losowo\nAuto – płynne przejście L↔P na długości patternu",
    "Szerokość efektu stereo.",
    "Filtr sterowany linią FILTER (wartość na krok).",
    "Rezonans filtra.",
    "Harmonic: głośność dźwięku przestrojonego w krokach z linią PITCH.\n100% = sam dźwięk przestrojony, 50% = razem z oryginałem.",
    "Retrigger: uderzenie w struny zaczyna pattern od pierwszego kroku.\nDziała, gdy host nie gra albo Sync jest wyłączony.",
    "Czułość retriggera – próg głośności uderzenia.",
};

} // namespace

class CleaverUI : public UI
{
    float fPar[kNumParams];
    float fDrag[kNumParams];
    Pattern fPat;
    std::string fName;
    int fPresetIdx = 0;
    bool fModified = false;
    float S = 1.f;

    int fLastPaintStep = -1;
    double fTaps[5] = {0, 0, 0, 0, 0};
    int fTapCount = 0;
    uint32_t fRng = 0xC0FFEEu;

    // user presets
    std::vector<UserPreset> fUser;
    int fUserIdx = -1; // >= 0 when the current preset is a user preset
    char fNameBuf[64] = {0};
    bool fOpenSave = false, fOpenDelete = false, fOpenMsg = false, fFocusName = false;
    std::string fMsg;

public:
    CleaverUI()
        : UI()
    {
        for (int i = 0; i < kNumParams; ++i) fPar[i] = fDrag[i] = kParams[i].def;
        buildPresetPattern(kPresets[0], fPat);
        fName = kPresets[0].name;
        loadUserPresets(fUser);

        const double sf = getScaleFactor();
        S = (float)sf;
        setGeometryConstraints(DISTRHO_UI_DEFAULT_WIDTH * sf, DISTRHO_UI_DEFAULT_HEIGHT * sf, true);
        if (sf != 1.0) setSize(DISTRHO_UI_DEFAULT_WIDTH * sf, DISTRHO_UI_DEFAULT_HEIGHT * sf);
    }

protected:
    // ------------------------------------------------------------------------------------------
    // DSP callbacks

    void parameterChanged(uint32_t index, float value) override
    {
        if (index >= kNumParams) return;
        fPar[index] = value;
        repaint();
    }

    void stateChanged(const char* key, const char* value) override
    {
        if (std::strcmp(key, "pattern") == 0) {
            decodePattern(value, fPat);
        } else if (std::strcmp(key, "name") == 0) {
            fName = value;
            fModified = !fName.empty() && fName.back() == '*';
            for (int i = 0; i < kNumPresets; ++i) {
                const size_t len = std::strlen(kPresets[i].name);
                if (fName.compare(0, len, kPresets[i].name) == 0 && (fName.size() == len || fName[len] == ' ')) fPresetIdx = i;
            }
            fUserIdx = findUserPreset(fUser, cleanPresetName(fName.c_str()));
        }
        repaint();
    }

    // ------------------------------------------------------------------------------------------
    // helpers

    void setP(int pid, float v)
    {
        const ParamDef& d = kParams[pid];
        v = v < d.min ? d.min : v > d.max ? d.max : v;
        if (d.kind != fFloat) v = std::round(v);
        if (v == fPar[pid]) return;
        fPar[pid] = v;
        setParameterValue((uint32_t)pid, v);
    }

    void setPOnce(int pid, float v)
    {
        editParameter((uint32_t)pid, true);
        setP(pid, v);
        editParameter((uint32_t)pid, false);
    }

    void pushPattern(bool modified = true)
    {
        setState("pattern", encodePattern(fPat).c_str());
        if (modified && !fModified) {
            fModified = true;
            fName += " *";
            setState("name", fName.c_str());
        }
    }

    void loadPreset(int i)
    {
        if (i < 0) i = kNumPresets - 1;
        if (i >= kNumPresets) i = 0;
        const Preset& pr = kPresets[i];
        fPresetIdx = i;
        fUserIdx = -1;
        buildPresetPattern(pr, fPat);
        setPOnce(pRate, (float)pr.rate);
        setPOnce(pSteps, (float)pr.steps);
        setPOnce(pAttack, pr.attack);
        setPOnce(pDuty, pr.duty);
        setPOnce(pRelease, pr.release);
        setPOnce(pSwing, pr.swing);
        setPOnce(pStereo, (float)pr.stereo);
        setPOnce(pFiltMode, (float)pr.filtMode);
        setPOnce(pReso, pr.reso);
        fName = pr.name;
        fModified = false;
        setState("name", fName.c_str());
        pushPattern(false);
    }

    // ---- user presets

    void loadUser(int i)
    {
        if (i < 0 || i >= (int)fUser.size()) return;
        const UserPreset u = fUser[i];
        fUserIdx = i;
        fPat = u.pat;
        for (int p = 0; p < kNumInputParams; ++p)
            if (presetStoresParam(p)) setPOnce(p, u.par[p]);
        fName = u.name;
        fModified = false;
        setState("name", fName.c_str());
        pushPattern(false);
    }

    // direction = -1 / +1: walk through factory presets followed by user presets
    void stepPreset(int dir)
    {
        const int total = kNumPresets + (int)fUser.size();
        const int cur = fUserIdx >= 0 ? kNumPresets + fUserIdx : fPresetIdx;
        const int next = ((cur + dir) % total + total) % total;
        if (next < kNumPresets) loadPreset(next);
        else loadUser(next - kNumPresets);
    }

    void saveUser(const std::string& name)
    {
        UserPreset u;
        u.name = name;
        for (int p = 0; p < kNumInputParams; ++p) u.par[p] = fPar[p];
        u.pat = fPat;
        loadUserPresets(fUser); // pick up presets saved by other instances of the plugin
        int idx = findUserPreset(fUser, name);
        if (idx >= 0) fUser[idx] = u;
        else { fUser.push_back(u); idx = (int)fUser.size() - 1; }
        if (!saveUserPresets(fUser)) {
            fMsg = "Nie udało się zapisać pliku presetów:\n" + presetFilePath();
            fOpenMsg = true;
        }
        fUserIdx = idx;
        fName = name;
        fModified = false;
        setState("name", fName.c_str());
    }

    void deleteUser(const std::string& name)
    {
        loadUserPresets(fUser);
        const int idx = findUserPreset(fUser, name);
        if (idx >= 0) {
            fUser.erase(fUser.begin() + idx);
            if (!saveUserPresets(fUser)) {
                fMsg = "Nie udało się zapisać pliku presetów:\n" + presetFilePath();
                fOpenMsg = true;
            }
        }
        fUserIdx = -1;
        if (!fModified) {
            fModified = true;
            fName += " *";
            setState("name", fName.c_str());
        }
    }

    void presetPopups()
    {
        if (fOpenSave) { ImGui::OpenPopup("Zapisz preset"); fOpenSave = false; fFocusName = true; }
        if (fOpenDelete) { ImGui::OpenPopup("Usuń preset"); fOpenDelete = false; }
        if (fOpenMsg) { ImGui::OpenPopup("Błąd"); fOpenMsg = false; }

        const ImVec2 center(getWidth() * 0.5f, getHeight() * 0.4f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(14 * S, 12 * S));
        ImGui::PushStyleColor(ImGuiCol_PopupBg, IM_COL32(38, 40, 49, 255));
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive, IM_COL32(120, 84, 18, 255));

        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Zapisz preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
            ImGui::TextUnformatted("Nazwa presetu:");
            ImGui::SetNextItemWidth(320 * S);
            if (fFocusName) { ImGui::SetKeyboardFocusHere(); fFocusName = false; }
            const bool enter = ImGui::InputText("##name", fNameBuf, sizeof(fNameBuf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
            const std::string name = cleanPresetName(fNameBuf);
            const bool exists = findUserPreset(fUser, name) >= 0;
            ImGui::PushStyleColor(ImGuiCol_Text, exists ? cAccent : cDim);
            ImGui::TextUnformatted(exists ? "Preset o tej nazwie istnieje – zostanie zastąpiony." : "Zapisuje pattern i wszystkie ustawienia poza tempem i Host sync.");
            ImGui::PopStyleColor();
            ImGui::Dummy(ImVec2(0, 4 * S));
            ImGui::BeginDisabled(name.empty());
            if (ImGui::Button(exists ? "Zastąp" : "Zapisz", ImVec2(110 * S, 0)) || (enter && !name.empty())) {
                saveUser(name);
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Anuluj", ImVec2(110 * S, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Usuń preset", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
            const std::string name = cleanPresetName(fName.c_str());
            ImGui::Text("Usunąć preset „%s”?", name.c_str());
            ImGui::Dummy(ImVec2(0, 4 * S));
            if (ImGui::Button("Usuń", ImVec2(110 * S, 0))) {
                deleteUser(name);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Anuluj", ImVec2(110 * S, 0)) || ImGui::IsKeyPressed(ImGuiKey_Escape)) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        if (ImGui::BeginPopupModal("Błąd", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove)) {
            ImGui::TextUnformatted(fMsg.c_str());
            ImGui::Dummy(ImVec2(0, 4 * S));
            if (ImGui::Button("OK", ImVec2(110 * S, 0))) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }

        ImGui::PopStyleColor(2);
        ImGui::PopStyleVar();
    }

    void formatValue(int pid, char* buf, size_t n) const
    {
        const float v = fPar[pid];
        const int iv = (int)std::lround(v);
        switch (pid) {
        case pRate: std::snprintf(buf, n, "%s", kRateNames[iv < 0 ? 0 : iv >= kNumRates ? kNumRates - 1 : iv]); break;
        case pSteps: std::snprintf(buf, n, "%d", iv); break;
        case pBpm: std::snprintf(buf, n, "%.1f", v); break;
        case pRelease: std::snprintf(buf, n, "%.0f ms", v); break;
        case pOutput: std::snprintf(buf, n, "%+.1f dB", v); break;
        case pSens: std::snprintf(buf, n, "%.0f dB", v); break;
        default: std::snprintf(buf, n, "%.0f%%", v); break;
        }
    }

    void textCentered(ImDrawList* dl, float cx, float y, ImU32 col, const char* txt)
    {
        const ImVec2 ts = ImGui::CalcTextSize(txt);
        dl->AddText(ImVec2(std::floor(cx - ts.x * 0.5f), y), col, txt);
    }

    // Rotary knob. (x, y) = top-left of its 64 x 92 cell, in unscaled window coordinates.
    void knob(int pid, float x, float y, const char* label = nullptr, bool dimmed = false)
    {
        const ParamDef& d = kParams[pid];
        const ImVec2 wp = ImGui::GetWindowPos();
        const float cw = 64 * S, r = 20 * S;
        const ImVec2 c(wp.x + x * S + cw * 0.5f, wp.y + y * S + 40 * S);
        ImDrawList* dl = ImGui::GetWindowDrawList();

        ImGui::SetCursorPos(ImVec2(x * S + 6 * S, y * S + 16 * S));
        ImGui::PushID(pid);
        ImGui::InvisibleButton("knob", ImVec2(cw - 12 * S, 48 * S));
        const bool hovered = ImGui::IsItemHovered();
        const bool active = ImGui::IsItemActive();
        const float range = d.max - d.min;

        if (ImGui::IsItemActivated()) {
            editParameter((uint32_t)pid, true);
            fDrag[pid] = fPar[pid];
        }
        if (active) {
            const ImGuiIO& io = ImGui::GetIO();
            if (io.MouseDelta.y != 0.f) {
                fDrag[pid] -= io.MouseDelta.y / S * range * (io.KeyShift ? 0.0008f : 0.005f);
                fDrag[pid] = fDrag[pid] < d.min ? d.min : fDrag[pid] > d.max ? d.max : fDrag[pid];
                setP(pid, fDrag[pid]);
            }
        }
        if (ImGui::IsItemDeactivated()) editParameter((uint32_t)pid, false);
        if (hovered && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            fDrag[pid] = d.def;
            setP(pid, d.def);
        }
        if (hovered && !active) {
            const float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.f) {
                const float step = d.kind != fFloat ? 1.f : range * (ImGui::GetIO().KeyShift ? 0.002f : 0.01f);
                setPOnce(pid, fPar[pid] + (wheel > 0 ? step : -step));
            }
            ImGui::SetTooltip("%s\n\nPrzeciągnij w górę/dół · Shift = precyzyjnie · dwuklik = wartość domyślna", kTips[pid]);
        }
        ImGui::PopID();

        const float t = (fPar[pid] - d.min) / range;
        const float a0 = 2.3561945f, a1 = 7.0685835f; // 135 deg .. 405 deg
        const float a = a0 + (a1 - a0) * t;
        const ImU32 acc = dimmed ? cDim : cAccent;
        dl->PathArcTo(c, r, a0, a1, 32);
        dl->PathStroke(cGrid, 0, 4 * S);
        if (t > 0.001f) {
            dl->PathArcTo(c, r, a0, a, 32);
            dl->PathStroke(acc, 0, 4 * S);
        }
        dl->AddCircleFilled(c, r - 5 * S, (hovered || active) ? IM_COL32(62, 66, 80, 255) : IM_COL32(48, 51, 62, 255), 32);
        dl->AddLine(ImVec2(c.x + std::cos(a) * 5 * S, c.y + std::sin(a) * 5 * S),
                    ImVec2(c.x + std::cos(a) * (r - 6 * S), c.y + std::sin(a) * (r - 6 * S)), cText, 2 * S);

        textCentered(dl, c.x, wp.y + y * S, dimmed ? cDim : cText, label ? label : d.name);
        char buf[32];
        formatValue(pid, buf, sizeof(buf));
        textCentered(dl, c.x, wp.y + y * S + 66 * S, cDim, buf);
    }

    void panel(float x, float y, float w, float h, const char* title)
    {
        const ImVec2 wp = ImGui::GetWindowPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(ImVec2(wp.x + x * S, wp.y + y * S), ImVec2(wp.x + (x + w) * S, wp.y + (y + h) * S), cPanel, 6 * S);
        dl->AddText(ImVec2(wp.x + (x + 10) * S, wp.y + (y + 6) * S), cAccent, title);
    }

    bool checkbox(int pid, float x, float y, const char* label)
    {
        bool v = fPar[pid] > 0.5f;
        ImGui::SetCursorPos(ImVec2(x * S, y * S));
        const bool changed = ImGui::Checkbox(label, &v);
        if (changed) setPOnce(pid, v ? 1.f : 0.f);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kTips[pid]);
        return changed;
    }

    void combo(int pid, float x, float y, float w, const char* const* names, int count)
    {
        int v = (int)std::lround(fPar[pid]);
        ImGui::SetCursorPos(ImVec2(x * S, y * S));
        ImGui::SetNextItemWidth(w * S);
        ImGui::PushID(pid);
        if (ImGui::Combo("##c", &v, names, count)) setPOnce(pid, (float)v);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", kTips[pid]);
        ImGui::PopID();
    }

    // ------------------------------------------------------------------------------------------
    // Pattern editor

    enum { lnLevel = 0, lnPitch, lnFilt, lnPan };

    float laneValueFromY(int lane, float yn) const
    {
        yn = yn < 0.f ? 0.f : yn > 1.f ? 1.f : yn;
        switch (lane) {
        case lnLevel:
        case lnFilt: {
            float v = 1.f - yn;
            if (v > 0.96f) v = 1.f;
            if (v < 0.06f) v = 0.f;
            return std::round(v * 20.f) / 20.f;
        }
        case lnPitch: return std::round((0.5f - yn) * 24.f);
        default: {
            float v = (yn - 0.5f) * 2.f; // top = left, bottom = right
            if (std::fabs(v) < 0.1f) v = 0.f;
            return std::round(v * 10.f) / 10.f;
        }
        }
    }

    void setLane(int lane, int i, float v)
    {
        Step& s = fPat.s[i];
        switch (lane) {
        case lnLevel: s.level = v; break;
        case lnPitch: s.pitch = (int)v; break;
        case lnFilt: s.filt = v; break;
        default: s.pan = v; break;
        }
    }

    void lane(int laneId, const char* title, const char* tip, float y, float h, ImU32 col, bool enabled)
    {
        const ImVec2 wp = ImGui::GetWindowPos();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float x0 = 84 * S, x1 = 944 * S;
        const float cw = (x1 - x0) / kMaxSteps;
        const ImVec2 p0(wp.x + x0, wp.y + y * S), p1(wp.x + x1, wp.y + (y + h) * S);
        const float hh = p1.y - p0.y;
        const int steps = (int)std::lround(fPar[pSteps]);
        const int cur = (int)std::lround(fPar[oStep]);

        dl->AddText(ImVec2(wp.x + 16 * S, p0.y + hh * 0.5f - ImGui::GetFontSize() * 0.5f), enabled ? col : cDim, title);
        dl->AddRectFilled(p0, p1, cLane, 3 * S);

        ImGui::SetCursorPos(ImVec2(x0, y * S));
        ImGui::PushID(laneId + 100);
        ImGui::InvisibleButton("lane", ImVec2(x1 - x0, hh), ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
        const bool active = ImGui::IsItemActive();
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemActivated()) fLastPaintStep = -1;
        if (active) {
            const ImVec2 m = ImGui::GetIO().MousePos;
            int st = (int)std::floor((m.x - p0.x) / cw);
            st = st < 0 ? 0 : st >= kMaxSteps ? kMaxSteps - 1 : st;
            float v;
            if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) v = (laneId == lnLevel) ? 0.f : (laneId == lnFilt) ? 1.f : 0.f;
            else v = laneValueFromY(laneId, (m.y - p0.y) / hh);
            const int from = fLastPaintStep < 0 ? st : fLastPaintStep;
            const int a = from < st ? from : st, b = from < st ? st : from;
            const std::string before = encodePattern(fPat);
            for (int i = a; i <= b; ++i) setLane(laneId, i, v);
            fLastPaintStep = st;
            if (encodePattern(fPat) != before) pushPattern();
        } else if (hovered) {
            ImGui::SetTooltip("%s\n\nLewy przycisk: rysuj · prawy: wyzeruj krok", tip);
        }
        ImGui::PopID();

        for (int i = 0; i < kMaxSteps; ++i) {
            const Step& s = fPat.s[i];
            const float cx0 = p0.x + i * cw + 1.5f * S, cx1 = p0.x + (i + 1) * cw - 1.5f * S;
            const bool inRange = i < steps;
            ImU32 c = withAlpha(col, !inRange ? 45 : !enabled ? 80 : (s.level <= 0.f && laneId != lnLevel) ? 110 : 230);
            if (laneId == lnLevel) {
                if (s.level > 0.f) dl->AddRectFilled(ImVec2(cx0, p1.y - s.level * (hh - 2 * S)), ImVec2(cx1, p1.y), c, 2 * S);
            } else if (laneId == lnFilt) {
                dl->AddRectFilled(ImVec2(cx0, p1.y - (0.08f + s.filt * 0.92f) * (hh - 2 * S)), ImVec2(cx1, p1.y), c, 2 * S);
            } else if (laneId == lnPitch) {
                const float mid = (p0.y + p1.y) * 0.5f;
                if (s.pitch != 0) {
                    const float yy = mid - (float)s.pitch / 12.f * (hh * 0.5f - 2 * S);
                    dl->AddRectFilled(ImVec2(cx0, yy < mid ? yy : mid), ImVec2(cx1, yy < mid ? mid : yy), c, 2 * S);
                    if (inRange) {
                        char b[8];
                        std::snprintf(b, sizeof(b), "%+d", s.pitch);
                        textCentered(dl, (cx0 + cx1) * 0.5f, s.pitch > 0 ? mid + 2 * S : mid - ImGui::GetFontSize() - 2 * S, cText, b);
                    }
                } else {
                    dl->AddRectFilled(ImVec2(cx0, mid - 1.5f * S), ImVec2(cx1, mid + 1.5f * S), c);
                }
            } else {
                const float mid = (p0.y + p1.y) * 0.5f;
                const float yy = mid + s.pan * (hh * 0.5f - 4 * S);
                dl->AddRectFilled(ImVec2(cx0, yy - 3 * S), ImVec2(cx1, yy + 3 * S), c, 2 * S);
            }
            if (i > 0) dl->AddLine(ImVec2(p0.x + i * cw, p0.y), ImVec2(p0.x + i * cw, p1.y), (i % 4 == 0) ? cGridBeat : withAlpha(cGrid, 120), 1.f);
        }
        if (laneId == lnPan) {
            dl->AddText(ImVec2(wp.x + 66 * S, p0.y - 1 * S), cDim, "L");
            dl->AddText(ImVec2(wp.x + 66 * S, p1.y - ImGui::GetFontSize()), cDim, "R");
        }
        // playhead
        if (cur >= 0 && cur < kMaxSteps)
            dl->AddRectFilled(ImVec2(p0.x + cur * cw, p0.y), ImVec2(p0.x + (cur + 1) * cw, p1.y), IM_COL32(255, 255, 255, 34));
        // steps outside the pattern length
        if (steps < kMaxSteps)
            dl->AddRectFilled(ImVec2(p0.x + steps * cw, p0.y), p1, IM_COL32(12, 12, 15, 150));
    }

    uint32_t rnd()
    {
        fRng = fRng * 1664525u + 1013904223u;
        return fRng >> 8;
    }

    // ------------------------------------------------------------------------------------------

    void onImGuiDisplay() override
    {
        const float width = getWidth();
        const float height = getHeight();

        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(width, height));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 4 * S);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, cBg);
        ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(48, 51, 62, 255));
        ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, IM_COL32(62, 66, 80, 255));
        ImGui::PushStyleColor(ImGuiCol_Button, IM_COL32(48, 51, 62, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, IM_COL32(72, 76, 92, 255));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, IM_COL32(160, 110, 20, 255));
        ImGui::PushStyleColor(ImGuiCol_CheckMark, cAccent);
        ImGui::PushStyleColor(ImGuiCol_Header, IM_COL32(120, 84, 18, 255));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(150, 104, 22, 255));
        ImGui::PushStyleColor(ImGuiCol_Text, cText);

        if (ImGui::Begin("Cleaver", nullptr,
                         ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBringToFrontOnFocus)) {
            const ImVec2 wp = ImGui::GetWindowPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();

            // ---- header
            ImGui::SetWindowFontScale(1.5f);
            dl->AddText(ImVec2(wp.x + 16 * S, wp.y + 14 * S), cAccent, "CLEAVER");
            ImGui::SetWindowFontScale(1.0f);
            dl->AddText(ImVec2(wp.x + 124 * S, wp.y + 22 * S), cDim, "pattern slicer");

            ImGui::SetCursorPos(ImVec2(300 * S, 16 * S));
            if (ImGui::Button("<", ImVec2(28 * S, 0))) stepPreset(-1);
            ImGui::SameLine(0, 4 * S);
            ImGui::SetNextItemWidth(250 * S);
            if (ImGui::BeginCombo("##preset", fName.c_str(), ImGuiComboFlags_HeightLarge)) {
                if (ImGui::IsWindowAppearing()) {
                    loadUserPresets(fUser);
                    fUserIdx = findUserPreset(fUser, cleanPresetName(fName.c_str()));
                }
                ImGui::PushStyleColor(ImGuiCol_Text, cAccent);
                ImGui::TextUnformatted(fUser.empty() ? "WŁASNE  (brak – użyj Save)" : "WŁASNE");
                ImGui::PopStyleColor();
                for (int i = 0; i < (int)fUser.size(); ++i) {
                    ImGui::PushID(1000 + i);
                    if (ImGui::Selectable(fUser[i].name.c_str(), i == fUserIdx && !fModified)) loadUser(i);
                    ImGui::PopID();
                }
                ImGui::Separator();
                ImGui::PushStyleColor(ImGuiCol_Text, cAccent);
                ImGui::TextUnformatted("FABRYCZNE");
                ImGui::PopStyleColor();
                for (int i = 0; i < kNumPresets; ++i) {
                    const bool sel = (i == fPresetIdx) && fUserIdx < 0 && !fModified;
                    if (ImGui::Selectable(kPresets[i].name, sel)) loadPreset(i);
                }
                ImGui::EndCombo();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Presety własne i fabryczne. Gwiazdka = zmieniony ręcznie.");
            ImGui::SameLine(0, 4 * S);
            if (ImGui::Button(">", ImVec2(28 * S, 0))) stepPreset(+1);
            ImGui::SameLine(0, 12 * S);
            if (ImGui::Button("Save", ImVec2(56 * S, 0))) {
                const std::string cur = fUserIdx >= 0 ? cleanPresetName(fName.c_str()) : std::string();
                std::snprintf(fNameBuf, sizeof(fNameBuf), "%s", cur.c_str());
                loadUserPresets(fUser);
                fOpenSave = true;
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zapisz bieżące ustawienia jako własny preset.\nPlik: %s\n\nJeśli REAPER przechwytuje klawisze przy wpisywaniu nazwy,\nwłącz w oknie efektu „Send all keyboard input to plug-in”.", presetFilePath().c_str());
            ImGui::SameLine(0, 4 * S);
            ImGui::BeginDisabled(fUserIdx < 0);
            if (ImGui::Button("Delete", ImVec2(62 * S, 0))) fOpenDelete = true;
            ImGui::EndDisabled();
            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Usuń wybrany własny preset (fabrycznych nie da się usunąć).");

            // gate LED
            {
                const float g = fPar[oGate] < 0.f ? 0.f : fPar[oGate] > 1.f ? 1.f : fPar[oGate];
                const ImVec2 c(wp.x + 920 * S, wp.y + 28 * S);
                dl->AddCircleFilled(c, 8 * S, IM_COL32(60, 44, 14, 255), 24);
                dl->AddCircleFilled(c, 8 * S, withAlpha(cAccent, (int)(g * 255)), 24);
                dl->AddText(ImVec2(wp.x + 868 * S, wp.y + 20 * S), cDim, "GATE");
            }

            // ---- pattern editor
            panel(8, 56, 944, 322, "");
            {
                const float x0 = 84 * S, cw = (944 * S - x0) / kMaxSteps;
                char b[8];
                for (int i = 0; i < kMaxSteps; i += 4) {
                    std::snprintf(b, sizeof(b), "%d", i + 1);
                    dl->AddText(ImVec2(wp.x + x0 + i * cw + 3 * S, wp.y + 60 * S), cDim, b);
                }
            }
            const int stereo = (int)std::lround(fPar[pStereo]);
            const int fmode = (int)std::lround(fPar[pFiltMode]);
            lane(lnLevel, "LEVEL", "Głośność każdego kroku. 0 = cisza.", 78, 110, cLevel, true);
            lane(lnPitch, "PITCH", "Przestrojenie kroku w półtonach (−12…+12).\nKroki z wartością ≠ 0 grają dźwięk przestrojony.", 194, 68, cPitch, true);
            lane(lnFilt, "FILTER", "Filtr na krok: wysoko = jasno, nisko = ciemno.\nDziała po włączeniu filtra w sekcji FILTER.", 268, 52, cFilt, fmode != fmOff);
            lane(lnPan, "PAN", "Panorama kroku. Działa w trybie stereo „Pattern”.", 326, 44, cPan, stereo == stPattern);

            // ---- pattern tools
            const int steps = (int)std::lround(fPar[pSteps]);
            ImGui::SetCursorPos(ImVec2(16 * S, 386 * S));
            if (ImGui::Button("All on")) { for (int i = 0; i < kMaxSteps; ++i) fPat.s[i].level = 1.f; pushPattern(); }
            ImGui::SameLine();
            if (ImGui::Button("Clear")) { for (int i = 0; i < kMaxSteps; ++i) fPat.s[i] = Step(); for (int i = 1; i < kMaxSteps; ++i) fPat.s[i].level = 0.f; pushPattern(); }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Zostawia tylko pierwszy krok i zeruje pitch, filtr i panoramę.");
            ImGui::SameLine();
            if (ImGui::Button("Random")) {
                for (int i = 0; i < kMaxSteps; ++i) {
                    const uint32_t r = rnd() % 100;
                    fPat.s[i].level = (i % steps == 0) ? 1.f : r < 45 ? 1.f : r < 60 ? 0.6f : 0.f;
                }
                pushPattern();
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Losuje linię LEVEL.");
            ImGui::SameLine();
            if (ImGui::Button("<< Shift")) { const Step f = fPat.s[0]; for (int i = 0; i < steps - 1; ++i) fPat.s[i] = fPat.s[i + 1]; fPat.s[steps - 1] = f; pushPattern(); }
            ImGui::SameLine();
            if (ImGui::Button("Shift >>")) { const Step l = fPat.s[steps - 1]; for (int i = steps - 1; i > 0; --i) fPat.s[i] = fPat.s[i - 1]; fPat.s[0] = l; pushPattern(); }
            ImGui::SameLine();
            if (ImGui::Button("Reset pitch / filter / pan")) { for (int i = 0; i < kMaxSteps; ++i) { fPat.s[i].pitch = 0; fPat.s[i].filt = 1.f; fPat.s[i].pan = 0.f; } pushPattern(); }

            // ---- row A
            const bool sync = fPar[pSync] > 0.5f;
            panel(8, 416, 468, 114, "SLICE");
            knob(pRate, 14, 436);
            knob(pSteps, 80, 436);
            knob(pAttack, 146, 436);
            knob(pDuty, 212, 436);
            knob(pRelease, 278, 436);
            knob(pDepth, 344, 436);
            knob(pSwing, 410, 436);

            panel(484, 416, 210, 114, "TEMPO");
            checkbox(pSync, 496, 452, "Host sync");
            ImGui::SetCursorPos(ImVec2(496 * S, 488 * S));
            if (ImGui::Button("Tap", ImVec2(96 * S, 0))) tapTempo();
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("Wystukaj tempo (min. 2 kliknięcia). Wyłącza Host sync.");
            knob(pBpm, 620, 436, nullptr, sync);

            panel(702, 416, 250, 114, "MIX");
            knob(pMix, 730, 436);
            knob(pOutput, 800, 436);
            knob(pHarm, 870, 436);

            // ---- row B
            panel(8, 536, 230, 98, "STEREO");
            combo(pStereo, 20, 576, 130, kStereoNames, kNumStereo);
            knob(pWidth, 162, 540, nullptr, stereo == stFixed);

            panel(246, 536, 230, 98, "FILTER");
            combo(pFiltMode, 258, 576, 130, kFiltNames, kNumFiltModes);
            knob(pReso, 400, 540, nullptr, fmode == fmOff);

            panel(484, 536, 230, 98, "TRIGGER");
            checkbox(pRetrig, 496, 576, "Retrigger on pick");
            knob(pSens, 644, 540, nullptr, fPar[pRetrig] < 0.5f);

            panel(722, 536, 230, 98, "HELP");
            ImGui::SetCursorPos(ImVec2(732 * S, 560 * S));
            ImGui::PushStyleColor(ImGuiCol_Text, cDim);
            ImGui::PushTextWrapPos(944 * S);
            ImGui::TextUnformatted("Rysuj myszą po liniach patternu.\nNajedź na kontrolkę, żeby zobaczyć opis.");
            ImGui::PopTextWrapPos();
            ImGui::PopStyleColor();

            presetPopups();
        }
        ImGui::End();
        ImGui::PopStyleColor(10);
        ImGui::PopStyleVar(3);
    }

    void tapTempo()
    {
        const double now = ImGui::GetTime();
        if (fTapCount > 0 && now - fTaps[fTapCount - 1] > 2.5) fTapCount = 0;
        if (fTapCount == 5) {
            for (int i = 0; i < 4; ++i) fTaps[i] = fTaps[i + 1];
            fTapCount = 4;
        }
        fTaps[fTapCount++] = now;
        if (fTapCount >= 2) {
            const double avg = (fTaps[fTapCount - 1] - fTaps[0]) / (fTapCount - 1);
            if (avg > 0.15) {
                setPOnce(pBpm, (float)(60.0 / avg));
                if (fPar[pSync] > 0.5f) setPOnce(pSync, 0.f);
            }
        }
    }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CleaverUI)
};

UI* createUI() { return new CleaverUI(); }

END_NAMESPACE_DISTRHO
