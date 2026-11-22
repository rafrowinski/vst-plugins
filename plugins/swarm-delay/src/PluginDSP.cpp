// Swarm Delay - plugin wrapper around swd::Engine
#include "DistrhoPlugin.hpp"
#include "Engine.hpp"

START_NAMESPACE_DISTRHO

using namespace swd;

class SwarmDelayPlugin : public Plugin
{
    float fP[kInputCount];
    float fOut[kOutputCount];
    Engine fEngine;

public:
    SwarmDelayPlugin() : Plugin(kParamCount, 0, 0)
    {
        presetDefaults(fP);
        std::memset(fOut, 0, sizeof(fOut));
        fEngine.setSampleRate(getSampleRate());
    }

protected:
    const char* getLabel() const noexcept override { return "SwarmDelay"; }
    const char* getDescription() const override { return "Multi-voice delay with reverse, tape and jump time changes, chorus, crusher, freeze, ducking and random glitches"; }
    const char* getMaker() const noexcept override { return "Rafal"; }
    const char* getLicense() const noexcept override { return "ISC"; }
    uint32_t getVersion() const noexcept override { return d_version(1, 2, 0); }
    int64_t getUniqueId() const noexcept override { return d_cconst('R', 'o', 'j', 'D'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= kInputCount)
        {
            parameter.hints = kParameterIsOutput;
            parameter.ranges.min = 0.f;
            if (index == kOutDuck)
            {
                parameter.name = "Ducking now"; parameter.symbol = "out_duck";
                parameter.ranges.max = 1.f;
            }
            else if (index == kOutBpm)
            {
                parameter.name = "Host tempo"; parameter.symbol = "out_bpm";
                parameter.ranges.max = 999.f;
            }
            else if (index >= kOutState)
            {
                const int v = int(index) - kOutState;
                parameter.name = String("V") + String(v + 1) + " glitch state";
                parameter.symbol = String("out_state") + String(v + 1);
                parameter.ranges.max = 3.f;
            }
            else
            {
                const int v = int(index) - kOutNow;
                parameter.name = String("V") + String(v + 1) + " delay now";
                parameter.symbol = String("out_now") + String(v + 1);
                parameter.unit = "ms";
                parameter.ranges.max = 4000.f;
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

        const char* const* names = nullptr;
        int count = 0;
        if (isVoiceParam(int(index)))
        {
            const int v = (int(index) - kGlobalCount) / kVoiceParamCount;
            parameter.name = String("V") + String(v + 1) + " " + d.name;
            if ((int(index) - kGlobalCount) % kVoiceParamCount == vDiv) { names = kDivNames; count = kDivCount; }
        }
        else
        {
            parameter.name = d.name;
            if (index == gTimeMode) { names = kTimeModeNames; count = kTimeModeCount; }
            else if (index == gSwMode) { names = kSweepModeNames; count = kSweepModeCount; }
            else if (index == gSwDiv) { names = kDivNames; count = kDivCount; }
        }
        if (names != nullptr)
        {
            parameter.enumValues.count = count;
            parameter.enumValues.restrictedMode = true;
            ParameterEnumerationValue* const values = new ParameterEnumerationValue[count];
            for (int i = 0; i < count; ++i) { values[i].value = float(i); values[i].label = names[i]; }
            parameter.enumValues.values = values;
        }

        parameter.symbol = sym;
        parameter.unit = d.unit;
        parameter.ranges.min = d.min;
        parameter.ranges.max = d.max;
        parameter.ranges.def = defaultOf(int(index));
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

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SwarmDelayPlugin)
};

Plugin* createPlugin() { return new SwarmDelayPlugin(); }

END_NAMESPACE_DISTRHO
