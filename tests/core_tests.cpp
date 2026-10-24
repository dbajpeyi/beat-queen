// Tests for the JUCE-free core. Run with ctest.

#include "beatqueen/DrumMachine.h"
#include "beatqueen/Pattern.h"
#include "beatqueen/Sequencer.h"
#include "beatqueen/Voices.h"

#include <cmath>
#include <cstdio>
#include <functional>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(cond)                                                                \
    do                                                                             \
    {                                                                              \
        if (!(cond))                                                               \
        {                                                                          \
            std::printf("  FAILED %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
            ++failures;                                                            \
        }                                                                          \
    } while (false)

using namespace beatqueen;

struct Hit
{
    long long sample;
    int step;
};

void testMakePattern()
{
    const auto p = makePattern("t", "c", 4, { { VoiceId::BassDrum, "x..x" }, { VoiceId::Clave, "x.x.x.x." } });
    CHECK(p.length == 8);
    CHECK(p.steps[0] == (voiceBit(VoiceId::BassDrum) | voiceBit(VoiceId::Clave)));
    CHECK(p.steps[1] == 0);
    CHECK(p.steps[3] == voiceBit(VoiceId::BassDrum));
    CHECK(p.steps[6] == voiceBit(VoiceId::Clave));
}

void testFreeRunningTiming()
{
    // 120 BPM sixteenths at 48 kHz = 6000 samples per step.
    const auto p = makePattern("t", "c", 4, { { VoiceId::BassDrum, "x..." } });
    Sequencer seq;
    seq.prepare(48000.0);
    seq.setTempo(120.0);
    seq.setPattern(&p);
    seq.start();

    std::vector<Hit> hits;
    long long blockStart = 0;
    for (int block = 0; block < 100; ++block)
    {
        const int n = 333 + (block % 7) * 100; // irregular block sizes
        seq.advance(n, [&](int offset, int step, StepMask) { hits.push_back({ blockStart + offset, step }); });
        blockStart += n;
    }

    CHECK(!hits.empty());
    for (size_t i = 0; i < hits.size(); ++i)
    {
        CHECK(hits[i].sample == static_cast<long long>(i) * 6000);
        CHECK(hits[i].step == static_cast<int>(i % 4));
    }
}

void testHostSyncedTiming()
{
    // Host at 120 BPM: one beat = 24000 samples, one sixteenth = 6000.
    const auto p = makePattern("t", "c", 4, { { VoiceId::BassDrum, "x..............." } });
    Sequencer seq;
    seq.prepare(48000.0);
    seq.setPattern(&p);

    std::vector<Hit> hits;
    long long blockStart = 0;
    for (int block = 0; block < 200; ++block)
    {
        const int n = 512;
        const double ppq = blockStart / 24000.0;
        seq.advanceSynced(ppq, 120.0, n, [&](int offset, int step, StepMask) { hits.push_back({ blockStart + offset, step }); });
        blockStart += n;
    }

    CHECK(hits.size() == static_cast<size_t>((blockStart + 5999) / 6000));
    for (size_t i = 0; i < hits.size(); ++i)
    {
        CHECK(std::llabs(hits[i].sample - static_cast<long long>(i) * 6000) <= 1);
        CHECK(hits[i].step == static_cast<int>(i % 16));
    }

    // Starting mid-bar maps the host position onto the right step.
    seq.resetSync();
    int firstStep = -1;
    seq.advanceSynced(2.5, 120.0, 512, [&](int, int step, StepMask) {
        if (firstStep < 0)
            firstStep = step;
    });
    CHECK(firstStep == 10); // beat 2.5 * 4 steps per beat
}

void testVoicesRingAndDecay()
{
    for (int i = 0; i < kNumVoices; ++i)
    {
        const auto id = static_cast<VoiceId>(i);
        auto voice = makeVoice(id);
        voice->prepare(48000.0);

        float silentPeak = 0.0f;
        for (int n = 0; n < 1000; ++n)
            silentPeak = std::max(silentPeak, std::fabs(voice->process()));

        voice->trigger(1.0f);
        float peak = 0.0f;
        bool finite = true;
        for (int n = 0; n < 4800; ++n)
        {
            const float s = voice->process();
            finite = finite && std::isfinite(s);
            peak = std::max(peak, std::fabs(s));
        }

        float tail = 0.0f;
        for (int n = 0; n < 48000 * 3; ++n)
            tail = std::max(tail, std::fabs(voice->process()));
        float late = 0.0f;
        for (int n = 0; n < 4800; ++n)
            late = std::max(late, std::fabs(voice->process()));

        std::printf("  %-10s peak %.3f, late %.6f\n", voiceName(id), peak, late);
        CHECK(silentPeak == 0.0f);
        CHECK(finite);
        CHECK(peak > 0.05f);
        CHECK(peak < 2.0f);
        CHECK(late < 1e-3f);
    }
}

void testDrumMachineRendersPattern()
{
    DrumMachine dm;
    dm.prepare(48000.0);
    dm.setRunning(true);

    std::vector<float> buffer(48000);
    dm.render(buffer.data(), static_cast<int>(buffer.size()), nullptr);

    float peak = 0.0f;
    for (float s : buffer)
        peak = std::max(peak, std::fabs(s));
    CHECK(peak > 0.05f);
    CHECK(dm.currentStep() >= 0);

    dm.setRunning(false);
    dm.render(buffer.data(), static_cast<int>(buffer.size()), nullptr);
    CHECK(dm.currentStep() == -1);
}

void testManualTrigger()
{
    DrumMachine dm;
    dm.prepare(48000.0);
    std::vector<float> buffer(4800);

    dm.render(buffer.data(), static_cast<int>(buffer.size()), nullptr);
    float before = 0.0f;
    for (float s : buffer)
        before = std::max(before, std::fabs(s));

    dm.requestManualTrigger(VoiceId::Clave);
    dm.render(buffer.data(), static_cast<int>(buffer.size()), nullptr);
    float after = 0.0f;
    for (float s : buffer)
        after = std::max(after, std::fabs(s));

    CHECK(before == 0.0f);
    CHECK(after > 0.05f);
}

} // namespace

int main()
{
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        { "makePattern", testMakePattern },
        { "free-running timing", testFreeRunningTiming },
        { "host-synced timing", testHostSyncedTiming },
        { "voices ring and decay", testVoicesRingAndDecay },
        { "drum machine renders pattern", testDrumMachineRendersPattern },
        { "manual trigger", testManualTrigger },
    };

    for (const auto& [name, fn] : tests)
    {
        std::printf("%s\n", name);
        fn();
    }

    std::printf(failures == 0 ? "All tests passed\n" : "%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
