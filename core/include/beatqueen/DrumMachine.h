#pragma once

#include "beatqueen/Sequencer.h"
#include "beatqueen/Voices.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>

namespace beatqueen {

// Host transport state for one render call.
struct HostTransport
{
    bool playing = false;
    double ppqPosition = 0.0; // beat position at the first sample of the block
    double bpm = 120.0;
};

// The whole instrument: eight voices, the rhythm sequencer and the mixer.
//
// Everything except requestManualTrigger(), currentStep() and patternLength()
// must be called from the audio thread.
class DrumMachine
{
public:
    DrumMachine();

    void prepare(double sampleRate);

    void setTempo(double bpm) { sequencer_.setTempo(bpm); }
    void selectPattern(int presetIndex);
    void setRunning(bool shouldRun);
    void setVoiceLevel(VoiceId id, float level) { levels_[static_cast<size_t>(id)] = level; }
    void setMasterLevel(float level) { master_ = level; }

    // Fire a voice at the start of the next rendered sample.
    void triggerVoice(VoiceId id, float level = 1.0f) { voices_[static_cast<size_t>(id)]->trigger(level); }

    // Thread-safe tap (UI buttons); applied at the start of the next block.
    void requestManualTrigger(VoiceId id) { pendingTaps_.fetch_or(1u << static_cast<int>(id)); }

    // Renders mono audio. Pass a transport to lock the rhythm to a playing
    // host; pass nullptr (or a stopped transport) to use the internal clock.
    void render(float* out, int numSamples, const HostTransport* host);

    // For the UI: the step last played, or -1 when stopped.
    int currentStep() const { return currentStep_.load(std::memory_order_relaxed); }
    int patternLength() const { return patternLength_.load(std::memory_order_relaxed); }

private:
    void fireMask(StepMask mask);

    std::array<std::unique_ptr<Voice>, kNumVoices> voices_;
    std::array<float, kNumVoices> levels_ {};
    float master_ = 0.8f;

    Sequencer sequencer_;
    bool running_ = false;
    bool hostWasPlaying_ = false;

    std::atomic<uint32_t> pendingTaps_ { 0 };
    std::atomic<int> currentStep_ { -1 };
    std::atomic<int> patternLength_ { 16 };
};

} // namespace beatqueen
