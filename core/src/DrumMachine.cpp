#include "beatqueen/DrumMachine.h"

#include <algorithm>
#include <array>

namespace beatqueen {

namespace {
// Upper bound on clock pulses landing in one block; far above any realistic tempo/block size.
constexpr int kMaxPulsesPerBlock = 256;
constexpr double kLampSeconds = 0.1;
} // namespace

DrumMachine::DrumMachine() = default;

void DrumMachine::prepare(double sampleRate)
{
    sampleRate_ = sampleRate;
    instrumentation_.prepare(sampleRate);
    clock_.prepare(sampleRate);
    const uint32_t selection = selection_;
    selection_ = ~selection;
    setRhythms(selection);
}

void DrumMachine::setRhythms(uint32_t selection)
{
    if (selection == selection_)
        return;
    selection_ = selection;

    // Selected switches connect their control functions and triplet contacts in parallel.
    mrk1::ControlLines lines;
    bool triplet = false;
    for (int r = 0; r < mrk1::kNumRhythms; ++r)
    {
        if ((selection & (1u << r)) == 0)
            continue;
        const auto& rhythm = mrk1::rhythms()[static_cast<size_t>(r)];
        triplet = triplet || rhythm.triplet;
        lines.hiHatMed = lines.hiHatMed || rhythm.controls.hiHatMed;
        lines.hiHatLong = lines.hiHatLong || rhythm.controls.hiHatLong;
        lines.hiHatChoke = lines.hiHatChoke || rhythm.controls.hiHatChoke;
        lines.hiHatDampen = lines.hiHatDampen || rhythm.controls.hiHatDampen;
        lines.cymbalY = lines.cymbalY || rhythm.controls.cymbalY;
        lines.cymbalZ = lines.cymbalZ || rhythm.controls.cymbalZ;
    }
    clock_.setTriplet(triplet);
    instrumentation_.setControlLines(lines);
}

void DrumMachine::setRunning(bool shouldRun)
{
    if (shouldRun == running_)
        return;
    running_ = shouldRun;
    if (running_)
        clock_.start();
    else
        clock_.stop();
}

void DrumMachine::requestButton(mrk1::Button b, bool down)
{
    const uint32_t bit = 1u << static_cast<int>(b);
    if (down)
        buttonsRequested_.fetch_or(bit);
    else
        buttonsRequested_.fetch_and(~bit);
}

void DrumMachine::applyButtons()
{
    const uint32_t requested = buttonsRequested_.load();
    const uint32_t changed = requested ^ buttonsApplied_;
    for (int b = 0; b < mrk1::kNumButtons; ++b)
        if ((changed & (1u << b)) != 0)
            instrumentation_.setButton(static_cast<mrk1::Button>(b), (requested & (1u << b)) != 0);
    buttonsApplied_ = requested;
}

void DrumMachine::firePulse(int index, double secondsPerBeat)
{
    using namespace mrk1;

    // The matrix: every selected rhythm switch keys its instruments in parallel.
    PulseEvents events;
    for (int r = 0; r < kNumRhythms; ++r)
        if ((selection_ & (1u << r)) != 0)
        {
            const auto& e = rhythms()[static_cast<size_t>(r)].pulses[static_cast<size_t>(index)];
            events.keys |= e.keys;
            events.snareRoll = events.snareRoll || e.snareRoll;
            events.cymbalAccent = events.cymbalAccent || e.cymbalAccent;
        }

    for (int k = 0; k < kNumKeyPoints; ++k)
        if ((events.keys & (1u << k)) != 0)
            instrumentation_.key(static_cast<KeyPoint>(k));

    const bool triplet = clock_.triplet();
    const double now = pulseBeat(index, triplet);
    if (events.snareRoll)
    {
        // Held until the ring multivibrator starts its next cycle.
        const double ringEnd = (index / kStages + 1) * kBeatsPerRingCycle;
        instrumentation_.snareRoll((ringEnd - now) * secondsPerBeat);
    }
    if (events.cymbalAccent)
    {
        const double next = index + 1 < kPulsesPerCycle ? pulseBeat(index + 1, triplet) : kBeatsPerCycle;
        instrumentation_.cymbalAccent((next - now) * secondsPerBeat);
    }

    // Q10 fires the hi-hat dampen transistor Q17 on every ring cycle; it only
    // reaches the hi-hat when a rhythm connects (17) to "C".
    if (index % kStages == kStages - 1)
        instrumentation_.hiHatDampenPulse();

    // The down beat lamp follows Q28: pulses 0 and 10.
    if (index == 0 || index == kPulsesPerCycle / 2)
        lampSamples_.store(static_cast<int>(kLampSeconds * sampleRate_), std::memory_order_relaxed);
    currentPulse_.store(index, std::memory_order_relaxed);
}

void DrumMachine::render(float* out, int numSamples, const HostTransport* host)
{
    struct Pulse
    {
        int offset, index;
    };
    std::array<Pulse, kMaxPulsesPerBlock> pulses;
    int numPulses = 0;
    auto collect = [&](int offset, int index) {
        if (numPulses < kMaxPulsesPerBlock)
            pulses[static_cast<size_t>(numPulses++)] = { offset, index };
    };

    applyButtons();

    const bool hostPlaying = host != nullptr && host->playing;
    double secondsPerBeat = 60.0 / clock_.tempo();
    if (hostPlaying)
    {
        if (!hostWasPlaying_)
            clock_.resetSync();
        clock_.advanceSynced(host->ppqPosition, host->bpm, numSamples, collect);
        secondsPerBeat = 60.0 / std::max(host->bpm, 1.0);
    }
    else if (running_)
    {
        if (hostWasPlaying_)
            clock_.start();
        clock_.advance(numSamples, collect);
    }
    else
    {
        currentPulse_.store(-1, std::memory_order_relaxed);
    }
    hostWasPlaying_ = hostPlaying;

    const float gain = volume_ * volume_ / kFullScaleVolts; // VR13, roughly audio taper
    int next = 0;
    for (int i = 0; i < numSamples; ++i)
    {
        while (next < numPulses && pulses[static_cast<size_t>(next)].offset <= i)
            firePulse(pulses[static_cast<size_t>(next++)].index, secondsPerBeat);
        out[i] = gain * instrumentation_.process();
    }

    if (const int lamp = lampSamples_.load(std::memory_order_relaxed); lamp > 0)
        lampSamples_.store(std::max(0, lamp - numSamples), std::memory_order_relaxed);
}

} // namespace beatqueen
