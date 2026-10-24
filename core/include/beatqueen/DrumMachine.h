#pragma once

#include "beatqueen/mrk1/Instrumentation.h"
#include "beatqueen/mrk1/Rhythms.h"

#include <atomic>
#include <climits>
#include <cmath>
#include <cstdint>

namespace beatqueen {

// Host transport state for one render call.
struct HostTransport
{
    bool playing = false;
    double ppqPosition = 0.0; // beat position at the first sample of the block
    double bpm = 120.0;
};

// The rhythm clock: the 20 pulses of each 8-beat cycle, sample-accurate,
// running free or locked to a host's beat position. Audio thread only.
class Clock
{
public:
    void prepare(double sampleRate) { sampleRate_ = sampleRate; }
    void setTempo(double bpm) { bpm_ = bpm > 1.0 ? bpm : 1.0; }
    double tempo() const { return bpm_; }
    void setTriplet(bool triplet) { triplet_ = triplet; }
    bool triplet() const { return triplet_; }

    // The reset foot switch: restart the cycle at pulse 0.
    void start()
    {
        running_ = true;
        beat_ = 0.0;
        nextPulse_ = 0;
    }
    void stop() { running_ = false; }
    bool running() const { return running_; }

    // Forget the last synced position, e.g. when the host transport restarts.
    void resetSync() { lastSyncedPulse_ = LLONG_MIN; }

    // Calls onPulse(sampleOffset, pulseIndex) for each pulse in the next block.
    template <typename OnPulse>
    void advance(int numSamples, OnPulse&& onPulse)
    {
        if (!running_)
            return;
        const double beatsPerSample = bpm_ / (60.0 * sampleRate_);
        const double end = beat_ + beatsPerSample * numSamples;
        for (double t = pulseTime(nextPulse_); t < end; t = pulseTime(++nextPulse_))
        {
            const int offset = t <= beat_ ? 0 : static_cast<int>((t - beat_) / beatsPerSample);
            onPulse(offset < numSamples ? offset : numSamples - 1, static_cast<int>(nextPulse_ % mrk1::kPulsesPerCycle));
        }
        beat_ = end;
    }

    template <typename OnPulse>
    void advanceSynced(double ppqAtStart, double bpm, int numSamples, OnPulse&& onPulse)
    {
        if (numSamples <= 0 || bpm <= 0.0)
            return;
        const double beatsPerSample = bpm / (60.0 * sampleRate_);
        const double end = ppqAtStart + beatsPerSample * numSamples;
        const long long firstCycle = static_cast<long long>(std::floor(ppqAtStart / mrk1::kBeatsPerCycle));

        for (long long k = firstCycle * mrk1::kPulsesPerCycle;; ++k)
        {
            const double t = pulseTime(k);
            if (t >= end)
                break;
            if (t < ppqAtStart - 1e-9 || k == lastSyncedPulse_)
                continue;
            lastSyncedPulse_ = k;
            const int offset = t <= ppqAtStart ? 0 : static_cast<int>((t - ppqAtStart) / beatsPerSample);
            onPulse(offset < numSamples ? offset : numSamples - 1,
                    static_cast<int>(((k % mrk1::kPulsesPerCycle) + mrk1::kPulsesPerCycle) % mrk1::kPulsesPerCycle));
        }
    }

private:
    double pulseTime(long long k) const
    {
        const long long n = mrk1::kPulsesPerCycle;
        const long long cycle = k >= 0 ? k / n : -((-k + n - 1) / n);
        return static_cast<double>(cycle) * mrk1::kBeatsPerCycle
               + mrk1::pulseBeat(static_cast<int>(k - cycle * n), triplet_);
    }

    double sampleRate_ = 48000.0;
    double bpm_ = 120.0;
    bool triplet_ = false;
    bool running_ = false;
    double beat_ = 0.0;
    long long nextPulse_ = 0;
    long long lastSyncedPulse_ = LLONG_MIN;
};

// The whole Rhythm King: rhythm selector switches, clock, matrix and the
// instrumentation board.
//
// Everything except requestButton() and the UI getters must be called from
// the audio thread.
class DrumMachine
{
public:
    DrumMachine();

    void prepare(double sampleRate);

    void setTempo(double bpm) { clock_.setTempo(bpm); }

    // Bit i selects rhythms()[i]; several may be combined, as on the real panel.
    void setRhythms(uint32_t selection);

    // Runs the internal clock (the reset foot switch latched on).
    void setRunning(bool shouldRun);

    // VR13, 0..1.
    void setVolume(float position) { volume_ = position; }

    void setTrim(mrk1::Trim t, double position) { instrumentation_.setTrim(t, position); }

    // A keying pulse now (MIDI notes).
    void key(mrk1::KeyPoint k) { instrumentation_.key(k); }

    // Thread-safe: a voice push button pressed or released (UI).
    void requestButton(mrk1::Button b, bool down);

    // Renders mono audio. Pass a transport to lock to a playing host; pass
    // nullptr (or a stopped transport) to run from the internal clock.
    void render(float* out, int numSamples, const HostTransport* host);

    // For the UI.
    int currentPulse() const { return currentPulse_.load(std::memory_order_relaxed); }
    bool downBeatLamp() const { return lampSamples_.load(std::memory_order_relaxed) > 0; }

    // Output scaling: this many volts at the output plug is digital full scale.
    static constexpr float kFullScaleVolts = 10.0f;

private:
    void firePulse(int index, double secondsPerBeat);
    void applyButtons();

    mrk1::Instrumentation instrumentation_;
    Clock clock_;
    double sampleRate_ = 48000.0;
    uint32_t selection_ = 0;
    bool running_ = false;
    bool hostWasPlaying_ = false;
    float volume_ = 0.8f;

    std::atomic<uint32_t> buttonsRequested_ { 0 };
    uint32_t buttonsApplied_ = 0;

    std::atomic<int> currentPulse_ { -1 };
    std::atomic<int> lampSamples_ { 0 };
};

} // namespace beatqueen
