// Cleaver Slicer - DPF plugin wrapper around cleaver::Engine.

#include "DistrhoPlugin.hpp"
#include "SlicerEngine.hpp"

START_NAMESPACE_DISTRHO

using namespace cleaver;

class CleaverPlugin : public Plugin
{
    Engine fEngine;

public:
    CleaverPlugin()
        : Plugin(kNumParams, 0, 2) // parameters, programs, states
    {
        fEngine.setSampleRate(getSampleRate());
        Pattern p;
        buildPresetPattern(kPresets[0], p);
        fEngine.setPattern(p);
    }

protected:
    const char* getLabel() const noexcept override { return "CleaverSlicer"; }
    const char* getDescription() const override
    {
        return "Pattern slicer / stutter effect for guitar: step sequenced gate with per-step pitch, filter and pan.";
    }
    const char* getMaker() const noexcept override { return "Rafal Rowinski"; }
    const char* getLicense() const noexcept override { return "ISC"; }
    uint32_t getVersion() const noexcept override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const noexcept override { return d_cconst('C', 'l', 'v', 'S'); }

    // ------------------------------------------------------------------------------------------
    // Init

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= kNumParams) return;
        const ParamDef& d = kParams[index];
        parameter.name = d.name;
        parameter.shortName = d.name;
        parameter.symbol = d.symbol;
        parameter.unit = d.unit;
        parameter.ranges.min = d.min;
        parameter.ranges.max = d.max;
        parameter.ranges.def = d.def;

        uint32_t hints = index < kNumInputParams ? kParameterIsAutomatable : kParameterIsOutput;
        if (d.kind == fBool) hints |= kParameterIsBoolean;
        if (d.kind == fInt || d.kind == fEnum) hints |= kParameterIsInteger;
        parameter.hints = hints;

        const char* const* names = nullptr;
        int count = 0;
        if (index == pRate) { names = kRateNames; count = kNumRates; }
        else if (index == pStereo) { names = kStereoNames; count = kNumStereo; }
        else if (index == pFiltMode) { names = kFiltNames; count = kNumFiltModes; }
        if (names != nullptr) {
            ParameterEnumerationValue* const values = new ParameterEnumerationValue[count];
            for (int i = 0; i < count; ++i) {
                values[i].value = (float)i;
                values[i].label = names[i];
            }
            parameter.enumValues.count = (uint8_t)count;
            parameter.enumValues.restrictedMode = true;
            parameter.enumValues.values = values;
        }
    }

    void initState(uint32_t index, State& state) override
    {
        if (index == 0) {
            Pattern p;
            buildPresetPattern(kPresets[0], p);
            state.key = "pattern";
            state.label = "Pattern";
            state.defaultValue = encodePattern(p).c_str();
        } else {
            state.key = "name";
            state.label = "Preset name";
            state.defaultValue = kPresets[0].name;
        }
        state.hints = 0;
    }

    // ------------------------------------------------------------------------------------------
    // Internal data

    float getParameterValue(uint32_t index) const override { return fEngine.getParam((int)index); }
    void setParameterValue(uint32_t index, float value) override { fEngine.setParam((int)index, value); }

    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, "pattern") == 0) {
            Pattern p;
            if (decodePattern(value, p)) fEngine.setPattern(p);
        }
    }

    // ------------------------------------------------------------------------------------------
    // Processing

    void activate() override { fEngine.reset(); }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const TimePosition& tp = getTimePosition();
        HostTime ht;
        if (tp.bbt.valid && tp.bbt.beatsPerMinute > 1.0) {
            ht.valid = true;
            ht.playing = tp.playing;
            ht.bpm = tp.bbt.beatsPerMinute;
            const double qnPerBeat = tp.bbt.beatType > 0.f ? 4.0 / tp.bbt.beatType : 1.0;
            const double tpb = tp.bbt.ticksPerBeat > 0.0 ? tp.bbt.ticksPerBeat : 1920.0;
            ht.ppq = ((double)(tp.bbt.bar - 1) * tp.bbt.beatsPerBar + (double)(tp.bbt.beat - 1) + tp.bbt.tick / tpb) * qnPerBeat;
        }
        fEngine.process(inputs[0], inputs[1], outputs[0], outputs[1], frames, ht);
    }

    void sampleRateChanged(double newSampleRate) override { fEngine.setSampleRate(newSampleRate); }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CleaverPlugin)
};

Plugin* createPlugin() { return new CleaverPlugin(); }

END_NAMESPACE_DISTRHO
