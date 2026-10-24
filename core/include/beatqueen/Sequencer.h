#pragma once

#include "beatqueen/Pattern.h"

#include <climits>
#include <cmath>

namespace beatqueen {

// Steps through a Pattern with sample-accurate timing.
//
// Two clock sources:
//  - advance():       free-running internal clock at the set tempo.
//  - advanceSynced(): locked to a host transport's beat (PPQ) position.
//
// For each step reached, onStep(sampleOffset, stepIndex, mask) is called.
// Audio thread only.
class Sequencer
{
public:
    void prepare(double sampleRate) { sampleRate_ = sampleRate; }
    void setTempo(double bpm) { bpm_ = bpm > 1.0 ? bpm : 1.0; }

    // The pattern must outlive the sequencer (presets are static).
    void setPattern(const Pattern* pattern)
    {
        pattern_ = pattern;
        if (pattern_ != nullptr)
            nextStep_ %= pattern_->length;
    }

    // Restart from step 0; the next advance() fires it at offset 0.
    void start()
    {
        running_ = true;
        nextStep_ = 0;
        samplesToNextStep_ = 0.0;
    }

    void stop() { running_ = false; }
    bool isRunning() const { return running_; }

    // Forget the last synced position, e.g. when the host transport restarts.
    void resetSync() { lastSyncedTick_ = LLONG_MIN; }

    double samplesPerStep() const
    {
        const int spb = pattern_ != nullptr ? pattern_->stepsPerBeat : 4;
        return sampleRate_ * 60.0 / (bpm_ * spb);
    }

    template <typename OnStep>
    void advance(int numSamples, OnStep&& onStep)
    {
        if (!running_ || pattern_ == nullptr)
            return;

        double pos = samplesToNextStep_;
        while (pos < numSamples)
        {
            fire(static_cast<int>(pos), nextStep_, onStep);
            nextStep_ = (nextStep_ + 1) % pattern_->length;
            pos += samplesPerStep();
        }
        samplesToNextStep_ = pos - numSamples;
    }

    template <typename OnStep>
    void advanceSynced(double ppqAtStart, double bpm, int numSamples, OnStep&& onStep)
    {
        if (pattern_ == nullptr || numSamples <= 0 || bpm <= 0.0)
            return;

        const int spb = pattern_->stepsPerBeat;
        const double ticksPerSample = bpm * spb / (60.0 * sampleRate_);
        const double t0 = ppqAtStart * spb;
        const double t1 = t0 + ticksPerSample * numSamples;

        for (long long tick = static_cast<long long>(std::ceil(t0 - 1e-9)); tick < t1; ++tick)
        {
            if (tick == lastSyncedTick_)
                continue;
            lastSyncedTick_ = tick;

            const int offset = tick <= t0 ? 0 : static_cast<int>((tick - t0) / ticksPerSample);
            const long long len = pattern_->length;
            fire(offset < numSamples ? offset : numSamples - 1, static_cast<int>(((tick % len) + len) % len), onStep);
        }
    }

private:
    template <typename OnStep>
    void fire(int offset, int step, OnStep& onStep)
    {
        onStep(offset, step, pattern_->steps[static_cast<size_t>(step)]);
    }

    double sampleRate_ = 48000.0;
    double bpm_ = 120.0;
    const Pattern* pattern_ = nullptr;
    bool running_ = false;
    int nextStep_ = 0;
    double samplesToNextStep_ = 0.0;
    long long lastSyncedTick_ = LLONG_MIN;
};

} // namespace beatqueen
