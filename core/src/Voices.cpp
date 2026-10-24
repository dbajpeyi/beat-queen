#include "beatqueen/Voices.h"

#include "beatqueen/Primitives.h"

// PLACEHOLDER VOICES
//
// Nothing in this file is derived from the MRK-1. These are generic
// "resonator + noise" drum sounds that make the sequencer, UI and plugin
// testable while the service manual is unavailable. Every parameter marked
// TODO(manual) must be replaced by a model of the corresponding circuit from
// the schematic. See docs/DESIGN.md for the plan.

namespace beatqueen {

namespace {

// A single ringing resonator: stand-in for a pulse-excited tuned circuit.
class ResonatorVoice final : public Voice
{
public:
    ResonatorVoice(double frequencyHz, double t60Seconds) : frequency_(frequencyHz), t60_(t60Seconds) {}

    void prepare(double sampleRate) override
    {
        resonator_.setup(frequency_, t60_, sampleRate);
        reset();
    }

    void reset() override { resonator_.reset(); }
    void trigger(float level) override { resonator_.excite(level); }
    float process() override { return resonator_.next(); }

private:
    double frequency_, t60_;
    dsp::Resonator resonator_;
};

// Enveloped, filtered noise: stand-in for a noise-source voice.
class NoiseVoice final : public Voice
{
public:
    enum class Shape { Highpass, Bandpass };

    NoiseVoice(Shape shape, double cutoffHz, double q, double t60Seconds, uint32_t seed)
        : shape_(shape), cutoff_(cutoffHz), q_(q), t60_(t60Seconds), noise_(seed)
    {
    }

    void prepare(double sampleRate) override
    {
        filter_.setup(cutoff_, q_, sampleRate);
        envelope_.setDecay(t60_, sampleRate);
        reset();
    }

    void reset() override
    {
        filter_.reset();
        envelope_.reset();
    }

    void trigger(float level) override { envelope_.trigger(level); }

    float process() override
    {
        const auto out = filter_.process(noise_.next());
        return envelope_.next() * (shape_ == Shape::Highpass ? out.highpass : out.bandpass);
    }

private:
    Shape shape_;
    double cutoff_, q_, t60_;
    dsp::WhiteNoise noise_;
    dsp::StateVariableFilter filter_;
    dsp::DecayEnvelope envelope_;
};

// Tuned body plus noise snares.
class SnareVoice final : public Voice
{
public:
    void prepare(double sampleRate) override
    {
        body_.prepare(sampleRate);
        snares_.prepare(sampleRate);
    }

    void reset() override
    {
        body_.reset();
        snares_.reset();
    }

    void trigger(float level) override
    {
        body_.trigger(level);
        snares_.trigger(level);
    }

    float process() override { return 0.5f * body_.process() + 0.8f * snares_.process(); }

private:
    ResonatorVoice body_ { 190.0, 0.12 };                                      // TODO(manual)
    NoiseVoice snares_ { NoiseVoice::Shape::Highpass, 1800.0, 0.7, 0.18, 0x51A8E }; // TODO(manual)
};

} // namespace

const char* voiceName(VoiceId id)
{
    switch (id)
    {
        case VoiceId::BassDrum: return "Bass Drum";
        case VoiceId::Snare: return "Snare";
        case VoiceId::HiHat: return "Hi-Hat";
        case VoiceId::Cymbal: return "Cymbal";
        case VoiceId::Tom: return "Tom";
        case VoiceId::Bongo: return "Bongo";
        case VoiceId::Blocks: return "Blocks";
        case VoiceId::Clave: return "Clave";
    }
    return "?";
}

std::unique_ptr<Voice> makeVoice(VoiceId id)
{
    using Shape = NoiseVoice::Shape;

    // All frequencies and decay times below are TODO(manual).
    switch (id)
    {
        case VoiceId::BassDrum: return std::make_unique<ResonatorVoice>(62.0, 0.35);
        case VoiceId::Snare: return std::make_unique<SnareVoice>();
        case VoiceId::HiHat: return std::make_unique<NoiseVoice>(Shape::Highpass, 7000.0, 0.7, 0.06, 0x4A7);
        case VoiceId::Cymbal: return std::make_unique<NoiseVoice>(Shape::Bandpass, 5500.0, 1.2, 0.9, 0xC1B);
        case VoiceId::Tom: return std::make_unique<ResonatorVoice>(130.0, 0.3);
        case VoiceId::Bongo: return std::make_unique<ResonatorVoice>(420.0, 0.12);
        case VoiceId::Blocks: return std::make_unique<ResonatorVoice>(850.0, 0.07);
        case VoiceId::Clave: return std::make_unique<ResonatorVoice>(2500.0, 0.05);
    }
    return nullptr;
}

} // namespace beatqueen
