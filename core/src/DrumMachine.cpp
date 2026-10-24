#include "beatqueen/DrumMachine.h"

#include <algorithm>

namespace beatqueen {

namespace {
// Upper bound on steps landing in one block; far above any realistic tempo/block size.
constexpr int kMaxEventsPerBlock = 128;
} // namespace

DrumMachine::DrumMachine()
{
    for (int i = 0; i < kNumVoices; ++i)
    {
        voices_[static_cast<size_t>(i)] = makeVoice(static_cast<VoiceId>(i));
        levels_[static_cast<size_t>(i)] = 0.8f;
    }
    selectPattern(0);
}

void DrumMachine::prepare(double sampleRate)
{
    for (auto& v : voices_)
        v->prepare(sampleRate);
    sequencer_.prepare(sampleRate);
}

void DrumMachine::selectPattern(int presetIndex)
{
    const auto& bank = presetPatterns();
    const auto& pattern = bank[static_cast<size_t>(std::clamp(presetIndex, 0, static_cast<int>(bank.size()) - 1))];
    sequencer_.setPattern(&pattern);
    patternLength_.store(pattern.length, std::memory_order_relaxed);
}

void DrumMachine::setRunning(bool shouldRun)
{
    if (shouldRun == running_)
        return;
    running_ = shouldRun;
    if (running_)
        sequencer_.start();
    else
        sequencer_.stop();
}

void DrumMachine::fireMask(StepMask mask)
{
    for (int i = 0; i < kNumVoices; ++i)
        if ((mask & (1u << i)) != 0)
            voices_[static_cast<size_t>(i)]->trigger(1.0f);
}

void DrumMachine::render(float* out, int numSamples, const HostTransport* host)
{
    struct Event
    {
        int offset;
        StepMask mask;
    };
    std::array<Event, kMaxEventsPerBlock> events;
    int numEvents = 0;

    auto collect = [&](int offset, int step, StepMask mask) {
        if (numEvents < kMaxEventsPerBlock)
            events[static_cast<size_t>(numEvents++)] = { offset, mask };
        currentStep_.store(step, std::memory_order_relaxed);
    };

    const bool hostPlaying = host != nullptr && host->playing;
    if (hostPlaying)
    {
        if (!hostWasPlaying_)
            sequencer_.resetSync();
        sequencer_.advanceSynced(host->ppqPosition, host->bpm, numSamples, collect);
    }
    else if (running_)
    {
        if (hostWasPlaying_)
            sequencer_.start();
        sequencer_.advance(numSamples, collect);
    }
    else
    {
        currentStep_.store(-1, std::memory_order_relaxed);
    }
    hostWasPlaying_ = hostPlaying;

    fireMask(static_cast<StepMask>(pendingTaps_.exchange(0)));

    int next = 0;
    for (int i = 0; i < numSamples; ++i)
    {
        while (next < numEvents && events[static_cast<size_t>(next)].offset <= i)
            fireMask(events[static_cast<size_t>(next++)].mask);

        float mix = 0.0f;
        for (int v = 0; v < kNumVoices; ++v)
            mix += levels_[static_cast<size_t>(v)] * voices_[static_cast<size_t>(v)]->process();
        out[i] = master_ * mix;
    }
}

} // namespace beatqueen
