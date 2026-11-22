// Swarm Chorus - plugin wrapper around roj::Engine
#include "DistrhoPlugin.hpp"
#include "Engine.hpp"

START_NAMESPACE_DISTRHO

using namespace roj;

class RojPlugin : public Plugin
{
    float fP[kInputCount];
    float fOut[kOutputCount];
    Engine fEngine;

public:
    RojPlugin() : Plugin(kParamCount, 0, 1)
    {
        presetDefaults(fP);
        std::memset(fOut, 0, sizeof(fOut));
        fEngine.setSampleRate(getSampleRate());
    }

protected:
    const char* getLabel() const noexcept override { return "SwarmChorus"; }
    const char* getDescription() const override { return "Multi-voice chorus with detune, staggered LFOs, hue/wah filters and EQ"; }
    const char* getMaker() const noexcept override { return "Rafal"; }
    const char* getLicense() const noexcept override { return "MIT"; }
    uint32_t getVersion() const noexcept override { return d_version(1, 3, 0); }
    int64_t getUniqueId() const noexcept override { return d_cconst('R', 'o', 'j', 'C'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= kInputCount)
        {
            parameter.hints = kParameterIsOutput;
            parameter.ranges.min = 0.f;
            if (index == kOutTrig)
            {
                parameter.name = "Triggered"; parameter.symbol = "out_trig";
                parameter.ranges.max = 1.f;
            }
            else if (index == kOutEnv)
            {
                parameter.name = "Envelope level"; parameter.symbol = "out_env";
                parameter.ranges.max = 1.f;
            }
            else if (index == kOutBpm)
            {
                parameter.name = "Host tempo"; parameter.symbol = "out_bpm";
                parameter.ranges.max = 999.f;
            }
            else
            {
                const int v = int(index) - kOutNow;
                parameter.name = String("V") + String(v + 1) + " delay now";
                parameter.symbol = String("out_now") + String(v + 1);
                parameter.unit = "ms";
                parameter.ranges.max = 200.f;
            }
            parameter.ranges.def = 0.f;
            return;
        }

        const Def& d = defOf(int(index));
        char sym[32];
        symbolOf(int(index), sym, sizeof(sym));

        parameter.hints = kParameterIsAutomatable;
        if (d.flags & fBool) parameter.hints |= kParameterIsBoolean | kParameterIsInteger;
        if (d.flags & fInt)  parameter.hints |= kParameterIsInteger;
        if (d.flags & fLog)  parameter.hints |= kParameterIsLogarithmic;

        if (isVoiceParam(int(index)))
        {
            const int v = (int(index) - kGlobalCount) / kVoiceParamCount;
            parameter.name = String("V") + String(v + 1) + " " + d.name;
            const int vp = (int(index) - kGlobalCount) % kVoiceParamCount;
            if (vp == vShape || vp == vDiv)
            {
                const int count = vp == vShape ? int(kShapeCount) : int(kDivCount);
                const char* const* const names = vp == vShape ? kShapeNames : kDivNames;
                parameter.enumValues.count = count;
                parameter.enumValues.restrictedMode = true;
                ParameterEnumerationValue* const values = new ParameterEnumerationValue[count];
                for (int i = 0; i < count; ++i) { values[i].value = float(i); values[i].label = names[i]; }
                parameter.enumValues.values = values;
            }
        }
        else
            parameter.name = d.name;

        parameter.symbol = sym;
        parameter.unit = d.unit;
        parameter.ranges.min = d.min;
        parameter.ranges.max = d.max;
        parameter.ranges.def = defaultOf(int(index));
    }

    void initState(uint32_t index, State& state) override
    {
        if (index != 0) return;
        state.key = "wave";
        state.label = "Custom wave";
        state.defaultValue = kDefaultWave;
    }

    // the custom wave arrives as text and is turned into a lookup table here, off the audio thread
    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, "wave") != 0) return;
        float table[kWaveSize];
        if (compileWave(value, table)) fEngine.setWave(table);
    }

    float getParameterValue(uint32_t index) const override
    {
        if (index < kInputCount) return fP[index];
        if (index < kParamCount) return fOut[index - kInputCount];
        return 0.f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= kInputCount) return;
        const Def& d = defOf(int(index));
        fP[index] = std::min(d.max, std::max(d.min, value));
    }

    void activate() override { fEngine.reset(); }

    void run(const float** inputs, float** outputs, uint32_t frames) override
    {
        const TimePosition& tp = getTimePosition();
        const float bpm = tp.bbt.valid ? float(tp.bbt.beatsPerMinute) : 0.f;
        fEngine.process(inputs[0], inputs[1], outputs[0], outputs[1], frames, fP, bpm, fOut);
    }

    void sampleRateChanged(double newSampleRate) override { fEngine.setSampleRate(newSampleRate); }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RojPlugin)
};

Plugin* createPlugin() { return new RojPlugin(); }

END_NAMESPACE_DISTRHO
