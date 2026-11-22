// Guitar to MIDI Live - turns a guitar signal into MIDI: notes, velocity, pitch bend and an expression CC.
//
// The note detection is the Swarm Synth detector (Engine.hpp, Tracker.hpp), used with all of its oscillators off.
// This file only turns what the detector reports into MIDI messages. No plugin-framework dependency, so it can be
// run offline over a recording (see tests/).
#pragma once

#include "../../../shared/guitar-engine/Engine.hpp"
#include "MidiParams.hpp"

namespace swmidi {

struct Msg { uint32_t frame; uint8_t size; uint8_t data[3]; };

class MidiCore
{
    enum { kSub = 32, kMaxOut = 2048 };

    swsyn::Engine fEng;
    float fE[swsyn::kInputCount];
    float fEOut[swsyn::kOutputCount];
    float fScrL[kSub], fScrR[kSub];
    double fSr = 48000.0;

    int fEngNote = -1;        // note number reported by the detector
    int fOutNote = -1;        // note number we have sent a note-on for (after transpose / steps), -1 = none
    int fOutChan = 0;         // channel that note was sent on
    int fStep = 0;            // "Note steps": semitones the bend has moved the note
    int fVel = 100;
    int fLastBend = 8192;
    int fLastCC = -1, fLastCCNum = -1;
    uint32_t fSinceBend = 0, fSinceCC = 0;
    int fLastBendsMode = bPitchBend;
    int fSerial = 0, fActivity = 0;
    bool fFlush = false;      // a note may still be on in the receiving synth after a reset

    Msg fMsgs[kMaxOut];
    int fCount = 0;

    void push(uint32_t frame, uint8_t a, uint8_t b, uint8_t c)
    {
        if (fCount >= kMaxOut) return;
        Msg& m = fMsgs[fCount++];
        m.frame = frame; m.size = 3; m.data[0] = a; m.data[1] = b; m.data[2] = c;
        ++fActivity;
    }
    void noteOn(uint32_t frame, int ch, int note, int vel) { push(frame, uint8_t(0x90 | ch), uint8_t(note), uint8_t(vel)); ++fSerial; }
    void noteOff(uint32_t frame, int ch, int note) { push(frame, uint8_t(0x80 | ch), uint8_t(note), 0); }
    void bend(uint32_t frame, int ch, int value)
    {
        value = std::min(16383, std::max(0, value));
        push(frame, uint8_t(0xE0 | ch), uint8_t(value & 127), uint8_t(value >> 7));
        fLastBend = value; fSinceBend = 0;
    }

    static int clampNote(int n) { return std::min(127, std::max(0, n)); }

    int velocityFrom(float level, const float* p) const
    {
        const float db = 20.f * std::log10(std::max(1e-6f, level));
        const float lo = p[pVelFull] - p[pVelRange];
        float s = std::min(1.f, std::max(0.f, (db - lo) / std::max(1.f, p[pVelRange])));
        s = std::pow(s, std::pow(3.f, p[pVelCurve] * 0.01f));
        const float v = p[pVelMin] + (p[pVelMax] - p[pVelMin]) * s;
        return std::min(127, std::max(1, int(std::lround(v))));
    }

    // pitch relative to the detector's note, in semitones (follows bends and vibrato)
    float bendSemis() const
    {
        if (fEngNote < 0) return 0.f;
        const float hz = fEng.trackedHz();
        if (!(hz > 10.f)) return 0.f;
        return 69.f + 12.f * std::log2(hz / 440.f) - float(fEngNote);
    }

    int bendValue(float semis, const float* p) const
    {
        const int range = kBendRanges[std::min(kBendRangeCount - 1, std::max(0, int(p[pBendRange] + 0.5f)))];
        const float b = std::min(1.f, std::max(-1.f, semis / float(range)));
        return 8192 + int(std::lround(b * (b < 0.f ? 8192.f : 8191.f)));
    }

    // moves the sounding note to another number without a pick (legato change)
    void changeNote(uint32_t frame, int ch, int newNote, int vel, bool overlap)
    {
        if (fOutNote == newNote && fOutChan == ch) return;
        if (fOutNote >= 0 && !overlap) noteOff(frame, fOutChan, fOutNote);
        noteOn(frame, ch, newNote, vel);
        if (fOutNote >= 0 && overlap) noteOff(frame, fOutChan, fOutNote);
        fOutNote = newNote; fOutChan = ch;
    }

public:
    float out[kOutputCount];

    MidiCore()
    {
        using namespace swsyn;
        swsyn::presetDefaults(fE);
        setVoices(fE, 0);
        fE[gMode] = float(mTrack); fE[gMidiOut] = 1.f; fE[gFltOn] = 0.f; fE[gEnvMode] = float(eFollow);
        fE[gComp] = 0.f; fE[gQuant] = 0.f; fE[gGlide] = 0.f; fE[gVibDepth] = 0.f; fE[gDrift] = 0.f;
        fEng.setEndOnMute(true);
        std::memset(out, 0, sizeof(out));
        out[oNote - kInputCount] = -1.f;
    }

    void setSampleRate(double sr) { fSr = sr; fEng.setSampleRate(sr); reset(); }

    void reset()
    {
        fEng.reset();
        if (fOutNote >= 0) fFlush = true;
        fEngNote = -1; fStep = 0; fSinceBend = fSinceCC = 1u << 30;
    }

    int count() const { return fCount; }
    const Msg& msg(int i) const { return fMsgs[i]; }

    // p: kInputCount values. Audio passes through when "Guitar through" is on.
    void process(const float* inL, const float* inR, float* outL, float* outR, uint32_t frames, const float* p)
    {
        using namespace swsyn;
        fCount = 0;
        const int ch = std::min(15, std::max(0, int(p[pChannel] + 0.5f)));
        const int transpose = 12 * int(std::lround(p[pOctave])) + int(std::lround(p[pSemi]));
        const int bends = std::min(2, std::max(0, int(p[pBends] + 0.5f)));
        const bool overlap = p[pLegato] > 0.5f;
        const int exprIdx = std::min(kExprCount - 1, std::max(0, int(p[pExpr] + 0.5f)));
        const int cc = kExprCC[exprIdx];
        const uint32_t minGap = uint32_t(0.002 * fSr), ccGap = uint32_t(0.004 * fSr);
        const float exprGain = std::pow(10.f, p[pExprGain] / 20.f);

        if (fFlush)
        {
            if (fOutNote >= 0) noteOff(0, fOutChan, fOutNote);
            fOutNote = -1; fFlush = false;
        }
        if (fOutNote >= 0 && fOutChan != ch) { noteOff(0, fOutChan, fOutNote); fOutNote = -1; }
        // leaving pitch-bend mode: put the bend back in the middle once
        if (bends != bPitchBend && fLastBendsMode == bPitchBend && fLastBend != 8192) bend(0, ch, 8192);
        fLastBendsMode = bends;
        if (cc != fLastCCNum) { fLastCCNum = cc; fLastCC = -1; }

        fE[gGate] = p[pGate]; fE[gCut] = p[pCut]; fE[gTrack] = p[pWindow]; fE[gHold] = p[pHold];
        fE[gDetClarity] = p[pDetClarity]; fE[gDetLegato] = p[pDetLegato]; fE[gDetLow] = p[pDetLow];
        fE[gDetHarm] = p[pDetHarm]; fE[gDetProbation] = p[pDetProbation]; fE[gDetRepick] = p[pDetRepick];
        fE[gDetRefract] = p[pDetRefract]; fE[gDetTail] = p[pDetTail]; fE[gDetBridge] = p[pDetBridge];

        for (uint32_t base = 0; base < frames; base += kSub)
        {
            const uint32_t n = std::min(uint32_t(kSub), frames - base);
            fEng.process(inL + base, inR + base, fScrL, fScrR, n, fE, fEOut);

            for (int k = 0; k < fEng.midiCount(); ++k)
            {
                const MidiEv& e = fEng.midiEvent(k);
                const uint32_t fr = base + std::min(e.frame, n - 1);
                if (e.status == 0x90)
                {
                    // the off for the previous note comes with the on (see changeNote), so it is skipped below
                    fEngNote = e.note; fStep = 0;
                    const int newNote = clampNote(e.note + transpose);
                    const bool legato = e.legato != 0;
                    if (!legato) fVel = velocityFrom(e.level, p);
                    if (bends == bPitchBend)
                    {
                        // the bend for the new note goes out just before it, so it starts at the right pitch
                        const int bv = bendValue(bendSemis(), p);
                        if (std::abs(bv - fLastBend) > 2) bend(fr, ch, bv);
                    }
                    if (legato) changeNote(fr, ch, newNote, fVel, overlap);
                    else
                    {
                        if (fOutNote >= 0) noteOff(fr, fOutChan, fOutNote);
                        noteOn(fr, ch, newNote, fVel);
                        fOutNote = newNote; fOutChan = ch;
                    }
                }
                else
                {
                    // a note-off right before a note-on in the same event list belongs to that change
                    const bool partOfChange = k + 1 < fEng.midiCount() && fEng.midiEvent(k + 1).status == 0x90;
                    if (!partOfChange)
                    {
                        if (fOutNote >= 0) noteOff(fr, fOutChan, fOutNote);
                        fOutNote = -1; fEngNote = -1; fStep = 0;
                    }
                }
            }

            fSinceBend += n; fSinceCC += n;
            const uint32_t endFr = base + n - 1;
            if (fOutNote >= 0 && fEngNote >= 0)
            {
                const float semis = bendSemis();
                if (bends == bPitchBend)
                {
                    const int bv = bendValue(semis, p);
                    const int range = kBendRanges[std::min(kBendRangeCount - 1, std::max(0, int(p[pBendRange] + 0.5f)))];
                    const int step = std::max(1, int(8192.f / (100.f * float(range)) * 0.5f));   // about half a cent
                    if (std::abs(bv - fLastBend) >= step && fSinceBend >= minGap) bend(endFr, ch, bv);
                }
                else if (bends == bSteps)
                {
                    // the bend becomes the nearest semitone, with some stickiness so it does not flicker
                    int target = fStep;
                    if (std::fabs(semis - float(fStep)) > 0.6f) target = int(std::lround(semis));
                    if (target != fStep)
                    {
                        fStep = target;
                        changeNote(endFr, ch, clampNote(fEngNote + fStep + transpose), fVel, overlap);
                    }
                }
            }

            if (cc >= 0)
            {
                // expression follows the playing level, in dB over a 40 dB span
                const float lv = fEOut[kOutEnv - swsyn::kInputCount] * exprGain;
                const float s = lv > 1e-6f ? std::min(1.f, std::max(0.f, 1.f + 20.f * std::log10(lv) / 40.f)) : 0.f;
                const int v = int(std::lround(s * 127.f));
                if (v != fLastCC && (fSinceCC >= ccGap || v == 0 || fLastCC < 0))
                {
                    push(endFr, uint8_t(0xB0 | ch), uint8_t(cc), uint8_t(v));
                    fLastCC = v; fSinceCC = 0;
                }
            }
        }

        if (p[pThru] > 0.5f)
        {
            if (outL != inL) std::memcpy(outL, inL, sizeof(float) * frames);
            if (outR != inR) std::memcpy(outR, inR, sizeof(float) * frames);
        }
        else
        {
            std::memset(outL, 0, sizeof(float) * frames);
            std::memset(outR, 0, sizeof(float) * frames);
        }

        out[oNote - kInputCount] = float(fOutNote);
        out[oBend - kInputCount] = (fOutNote >= 0 && bends == bPitchBend) ? bendSemis() : 0.f;
        out[oVelocity - kInputCount] = float(fVel);
        out[oHz - kInputCount] = fOutNote >= 0 ? fEng.trackedHz() : 0.f;
        out[oLevel - kInputCount] = fEOut[kOutEnv - swsyn::kInputCount];
        out[oGate - kInputCount] = fEOut[kOutGate - swsyn::kInputCount];
        out[oLatency - kInputCount] = fEOut[kOutLatency - swsyn::kInputCount];
        out[oSerial - kInputCount] = float(fSerial % 1000000);
        out[oActivity - kInputCount] = float(fActivity % 1000000);
    }
};

} // namespace swmidi
