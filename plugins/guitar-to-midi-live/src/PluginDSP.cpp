// Guitar to MIDI Live - plugin wrapper around swmidi::MidiCore
#include "DistrhoPlugin.hpp"
#include "MidiCore.hpp"

START_NAMESPACE_DISTRHO

using namespace swmidi;

class GuitarToMidiLivePlugin : public Plugin
{
    float fP[kInputCount];
    MidiCore fCore;

public:
    GuitarToMidiLivePlugin() : Plugin(kParamCount, 0, 0)
    {
        presetDefaults(fP);
        fCore.setSampleRate(getSampleRate());
    }

protected:
    const char* getLabel() const noexcept override { return "GuitarToMIDILive"; }
    const char* getDescription() const override { return "Guitar to MIDI: notes, velocity, pitch bend and an expression CC from a single-note guitar line"; }
    const char* getMaker() const noexcept override { return "Rafal"; }
    const char* getLicense() const noexcept override { return "MIT"; }
    uint32_t getVersion() const noexcept override { return d_version(1, 1, 0); }
    int64_t getUniqueId() const noexcept override { return d_cconst('G', '2', 'M', 'L'); }

    void initParameter(uint32_t index, Parameter& parameter) override
    {
        if (index >= kInputCount)
        {
            static const struct { const char* name; const char* sym; const char* unit; float min, max; } kOut[kOutputCount] = {
                { "Note out", "out_note", "", -1.f, 127.f }, { "Bend out", "out_bend", "st", -48.f, 48.f },
                { "Velocity out", "out_vel", "", 0.f, 127.f }, { "Pitch", "out_hz", "Hz", 0.f, 5000.f },
                { "Input level", "out_level", "", 0.f, 1.f }, { "Gate open", "out_gate", "", 0.f, 1.f },
                { "Detection delay", "out_latency", "ms", 0.f, 1000.f }, { "Note counter", "out_serial", "", 0.f, 1000000.f },
                { "MIDI activity", "out_activity", "", 0.f, 1000000.f },
            };
            const auto& o = kOut[index - kInputCount];
            parameter.hints = kParameterIsOutput;
            parameter.name = o.name; parameter.symbol = o.sym; parameter.unit = o.unit;
            parameter.ranges.min = o.min; parameter.ranges.max = o.max; parameter.ranges.def = o.min;
            return;
        }

        const Def& d = defOf(int(index));
        parameter.hints = kParameterIsAutomatable;
        if (d.flags & fBool) parameter.hints |= kParameterIsBoolean | kParameterIsInteger;
        if (d.flags & fInt)  parameter.hints |= kParameterIsInteger;
        if (d.flags & fLog)  parameter.hints |= kParameterIsLogarithmic;

        const char* const* names = nullptr;
        int count = 0;
        switch (index)
        {
        case pChannel:   names = kChannelNames; count = 16; break;
        case pBends:     names = kBendNames; count = kBendsCount; break;
        case pBendRange: names = kBendRangeNames; count = kBendRangeCount; break;
        case pLegato:    names = kLegatoNames; count = kLegatoCount; break;
        case pExpr:      names = kExprNames; count = kExprCount; break;
        default: break;
        }
        if (names != nullptr)
        {
            parameter.enumValues.count = uint8_t(count);
            parameter.enumValues.restrictedMode = true;
            ParameterEnumerationValue* const values = new ParameterEnumerationValue[count];
            for (int i = 0; i < count; ++i) { values[i].value = float(i); values[i].label = names[i]; }
            parameter.enumValues.values = values;
        }
        char sym[32];
        symbolOf(int(index), sym, sizeof(sym));
        parameter.name = d.name;
        parameter.symbol = sym;
        parameter.unit = d.unit;
        parameter.ranges.min = d.min;
        parameter.ranges.max = d.max;
        parameter.ranges.def = d.def;
    }

    float getParameterValue(uint32_t index) const override
    {
        if (index < kInputCount) return fP[index];
        if (index < kParamCount) return fCore.out[index - kInputCount];
        return 0.f;
    }

    void setParameterValue(uint32_t index, float value) override
    {
        if (index >= kInputCount) return;
        const Def& d = defOf(int(index));
        fP[index] = std::min(d.max, std::max(d.min, value));
    }

    void activate() override { fCore.reset(); }

    void run(const float** inputs, float** outputs, uint32_t frames, const MidiEvent* midiIn, uint32_t midiInCount) override
    {
        fCore.process(inputs[0], inputs[1], outputs[0], outputs[1], frames, fP);

        // our messages and whatever MIDI came in on the track, merged in time order
        uint32_t a = 0;
        int b = 0;
        while (a < midiInCount || b < fCore.count())
        {
            const bool takeIn = b >= fCore.count() || (a < midiInCount && midiIn[a].frame <= fCore.msg(b).frame);
            if (takeIn) { writeMidiEvent(midiIn[a++]); continue; }
            const Msg& m = fCore.msg(b++);
            MidiEvent e;
            e.frame = std::min(m.frame, frames - 1);
            e.size = m.size;
            e.data[0] = m.data[0]; e.data[1] = m.data[1]; e.data[2] = m.data[2]; e.data[3] = 0;
            e.dataExt = nullptr;
            writeMidiEvent(e);
        }
    }

    void sampleRateChanged(double newSampleRate) override { fCore.setSampleRate(newSampleRate); }

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GuitarToMidiLivePlugin)
};

Plugin* createPlugin() { return new GuitarToMidiLivePlugin(); }

END_NAMESPACE_DISTRHO
