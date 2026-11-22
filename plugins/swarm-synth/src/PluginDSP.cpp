// Swarm Synth - plugin wrapper around swsyn::Engine
#include "DistrhoPlugin.hpp"
#include "../../../shared/guitar-engine/Engine.hpp"

START_NAMESPACE_DISTRHO

using namespace swsyn;

class SwarmSynthPlugin : public Plugin
{
    float fP[kInputCount];
    float fOut[kOutputCount];
    Engine fEngine;

public:
    SwarmSynthPlugin() : Plugin(kParamCount, 0, 1)
    {
        presetDefaults(fP);
        std::memset(fOut, 0, sizeof(fOut));
        fEngine.setSampleRate(getSampleRate());
    }

protected:
    const char* getLabel() const noexcept override { return "SwarmSynth"; }
    const char* getDescription() const override { return "Guitar synth: up to 8 detuned, panned oscillators that follow the guitar, by pitch tracking or by reshaping the signal"; }
    const char* getMaker() const noexcept override { return "Rafal"; }
    const char* getLicense() const noexcept override { return "MIT"; }
    uint32_t getVersion() const noexcept override { return d_version(1, 4, 0); }
    int64_t getUniqueId() const noexcept override { return d_cconst('S', 'w', 'S', 'y'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= kInputCount)
        {
            parameter.hints = kParameterIsOutput;
            parameter.ranges.min = 0.f;
            parameter.ranges.def = 0.f;
            switch (index)
            {
            case kOutFreq:    parameter.name = "Playing pitch"; parameter.symbol = "out_freq"; parameter.unit = "Hz"; parameter.ranges.max = 5000.f; break;
            case kOutEnv:     parameter.name = "Input level"; parameter.symbol = "out_env"; parameter.ranges.max = 1.f; break;
            case kOutLatency: parameter.name = "Detection delay"; parameter.symbol = "out_latency"; parameter.unit = "ms"; parameter.ranges.max = 1000.f; break;
            default:          parameter.name = "Gate open"; parameter.symbol = "out_gate"; parameter.ranges.max = 1.f; break;
            }
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
            if ((int(index) - kGlobalCount) % kVoiceParamCount == vWave) { names = kWaveNames; count = kWaveCount; }
        }
        else
        {
            parameter.name = d.name;
            if (index == gMode) { names = kModeNames; count = kModeCount; }
            else if (index == gEnvMode) { names = kEnvModeNames; count = kEnvModeCount; }
            else if (index == gSlope) { names = kSlopeNames; count = 4; }
        }
        if (names != nullptr)
        {
            parameter.enumValues.count = uint8_t(count);
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

    void initState(uint32_t index, State& state) override
    {
        if (index != 0) return;
        state.key = "wave";
        state.label = "Custom wave";
        state.defaultValue = kDefaultWave;
    }

    // the custom wave arrives as text and is turned into band-limited tables here, off the audio thread
    void setState(const char* key, const char* value) override
    {
        if (std::strcmp(key, "wave") != 0) return;
        std::vector<float> table(kWaveSize);
        if (compileWave(value, table.data())) fEngine.setWave(table.data());
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
        fEngine.process(inputs[0], inputs[1], outputs[0], outputs[1], frames, fP, fOut);
        for (int i = 0; i < fEngine.midiCount(); ++i)
        {
            const MidiEv& e = fEngine.midiEvent(i);
            MidiEvent m;
            m.frame = e.frame;
            m.size = 3;
            m.data[0] = e.status; m.data[1] = e.note; m.data[2] = e.velocity; m.data[3] = 0;
            m.dataExt = nullptr;
            writeMidiEvent(m);
        }
    }

    void sampleRateChanged(double newSampleRate) override { fEngine.setSampleRate(newSampleRate); }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SwarmSynthPlugin)
};

Plugin* createPlugin() { return new SwarmSynthPlugin(); }

END_NAMESPACE_DISTRHO
