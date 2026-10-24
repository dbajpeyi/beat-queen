#pragma once

#include <memory>

namespace beatqueen {

// The eight MRK-1 instruments, in front-panel order.
enum class VoiceId : int
{
    BassDrum,
    Snare,
    HiHat,
    Cymbal,
    Tom,
    Bongo,
    Blocks,
    Clave,
};

constexpr int kNumVoices = 8;

const char* voiceName(VoiceId id);

// One instrument circuit. Called from the audio thread only.
class Voice
{
public:
    virtual ~Voice() = default;

    virtual void prepare(double sampleRate) = 0;
    virtual void reset() = 0;

    // Fire the voice, as the sequencer's trigger pulse or a manual tap would.
    virtual void trigger(float level) = 0;

    virtual float process() = 0;
};

std::unique_ptr<Voice> makeVoice(VoiceId id);

} // namespace beatqueen
