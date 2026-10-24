// Tests for the JUCE-free core. Run with ctest.

#include "beatqueen/DrumMachine.h"
#include "beatqueen/circuit/Circuit.h"
#include "beatqueen/mrk1/Instrumentation.h"
#include "beatqueen/mrk1/Rhythms.h"

#include <algorithm>
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

#define CHECK_NEAR(value, expected, tolerance)                                                         \
    do                                                                                                 \
    {                                                                                                  \
        const double v_ = (value), e_ = (expected), t_ = (tolerance);                                  \
        if (!(std::fabs(v_ - e_) <= t_))                                                               \
        {                                                                                              \
            std::printf("  FAILED %s:%d: %s = %g, expected %g +- %g\n", __FILE__, __LINE__, #value, v_, \
                        e_, t_);                                                                       \
            ++failures;                                                                                \
        }                                                                                              \
    } while (false)

using namespace beatqueen;
using namespace beatqueen::mrk1;
using circuit::Circuit;
using circuit::kGround;
using circuit::NodeId;

constexpr int kRate = 48000;

// --- Signal measurements ---------------------------------------------------

double peakOf(const std::vector<float>& x)
{
    double p = 0.0;
    for (float s : x)
        p = std::max(p, static_cast<double>(std::fabs(s)));
    return p;
}

// Frequency from zero crossings over [start, start + length) samples.
double zeroCrossingHz(const std::vector<float>& x, int start, int length)
{
    int first = -1, last = -1, count = 0;
    for (int i = start + 1; i < std::min(start + length, static_cast<int>(x.size())); ++i)
        if ((x[static_cast<size_t>(i - 1)] < 0) != (x[static_cast<size_t>(i)] < 0))
        {
            if (first < 0)
                first = i;
            last = i;
            ++count;
        }
    return count > 2 ? (count - 1) / 2.0 / ((last - first) / static_cast<double>(kRate)) : 0.0;
}

// Seconds from the loudest 10 ms window until the envelope is 40 dB below it.
double decaySeconds(const std::vector<float>& x)
{
    const int win = kRate / 100;
    std::vector<double> env;
    for (size_t i = 0; i + static_cast<size_t>(win) <= x.size(); i += static_cast<size_t>(win))
    {
        double m = 0.0;
        for (int j = 0; j < win; ++j)
            m = std::max(m, static_cast<double>(std::fabs(x[i + static_cast<size_t>(j)])));
        env.push_back(m);
    }
    const auto peak = std::max_element(env.begin(), env.end());
    for (auto it = peak; it != env.end(); ++it)
        if (*it < *peak * 0.01)
            return static_cast<double>(it - peak) * 0.01;
    return 1e9;
}

std::vector<float> renderKey(Instrumentation& inst, KeyPoint k, double seconds)
{
    std::vector<float> out;
    for (int i = 0; i < kRate / 4; ++i) // settle
        inst.process();
    inst.key(k);
    for (int i = 0; i < static_cast<int>(seconds * kRate); ++i)
        out.push_back(inst.process());
    return out;
}

// --- Circuit engine ----------------------------------------------------------

void testRcStepResponse()
{
    // 10 V through 10K into 1 uF: v(t) = 10 (1 - exp(-t / 10 ms)).
    Circuit c;
    const NodeId rail = c.addRail(10.0), a = c.addNode(), b = c.addNode();
    const int sw = c.addSwitch(rail, a, 1e-3);
    c.addResistor(a, b, 10e3);
    c.addCapacitor(b, kGround, 1e-6);
    c.prepare(kRate);
    CHECK_NEAR(c.voltage(b), 0.0, 1e-6);
    c.setSwitch(sw, true);
    for (int i = 0; i < kRate / 100; ++i) // 10 ms
        c.step();
    // The trapezoidal rule sees the switch close half a sample late.
    CHECK_NEAR(c.voltage(b), 10.0 * (1.0 - std::exp(-1.0)), 1e-2);
}

void testLcResonance()
{
    // 27 mH with 0.068 uF (the hi-hat tank L2): f = 1 / (2 pi sqrt(LC)) = 3714 Hz,
    // exact once the simulation is pre-warped there.
    Circuit c;
    c.setPrewarpFrequency(3714.0);
    const NodeId a = c.addNode();
    c.addInductor(a, kGround, 27e-3);
    c.addCapacitor(a, kGround, 0.068e-6);
    c.addResistor(a, kGround, 1e9);
    const int kick = c.addCurrentSource(kGround, a);
    c.prepare(kRate);
    c.setCurrent(kick, 1e-3);
    c.step();
    c.setCurrent(kick, 0.0);
    std::vector<float> v;
    for (int i = 0; i < kRate / 10; ++i)
    {
        c.step();
        v.push_back(static_cast<float>(c.voltage(a)));
    }
    CHECK_NEAR(zeroCrossingHz(v, 0, static_cast<int>(v.size())), 3714.0, 3.0);
}

void testDiodeClamp()
{
    // 10 V through 1K into a diode: about 0.65 V across it.
    Circuit c;
    const NodeId rail = c.addRail(10.0), a = c.addNode();
    c.addResistor(rail, a, 1e3);
    c.addDiode(a, kGround);
    c.prepare(kRate);
    CHECK(c.voltage(a) > 0.55 && c.voltage(a) < 0.8);
}

void testBassDrumOperatingPointMatchesSpice()
{
    // Bass drum generator netlist with the trimmer at mid travel. ngspice (same
    // netlist and transistor model as tools/spice/generators.py, `.op`) gives
    // Vc = 6.511 V, Vb = 0.730 V, Ve = 0.1156 V.
    Circuit c;
    const NodeId vcc = c.addRail(17.0);
    const NodeId bpa = c.addNode(), cpa = c.addNode(), p = c.addNode(), m = c.addNode(), a = c.addNode(),
                 n = c.addNode(), b = c.addNode(), col = c.addNode(), e = c.addNode(), o = c.addNode(),
                 bus = c.addNode();
    c.addResistor(bpa, kGround, 100e3);
    c.addBjt(cpa, bpa, kGround);
    c.addCapacitor(cpa, kGround, 0.1e-6);
    c.addResistor(p, cpa, 33e3);
    c.addResistor(vcc, p, 2.2e3);
    c.addResistor(p, m, 12e3);
    c.addCapacitor(a, m, 0.082e-6);
    c.addCapacitor(m, b, 0.082e-6);
    c.addResistor(a, n, 56e3);
    c.addResistor(n, b, 56e3);
    c.addCapacitor(n, kGround, 0.082e-6);
    c.addBjt(col, b, e);
    c.addResistor(vcc, col, 68e3);
    c.addResistor(col, b, 1.2e6);
    c.addCapacitor(col, a, 0.12e-6);
    c.addResistor(e, kGround, 770.0);
    c.addResistor(a, o, 33e3);
    c.addCapacitor(o, kGround, 0.15e-6);
    c.addResistor(o, bus, 22e3);
    c.addResistor(bus, kGround, 10e3);
    c.prepare(kRate);
    CHECK_NEAR(c.voltage(col), 6.511, 0.02);
    CHECK_NEAR(c.voltage(b), 0.730, 0.005);
    CHECK_NEAR(c.voltage(e), 0.1156, 0.001);
}

// --- Instrumentation -----------------------------------------------------------

struct SpiceReference
{
    KeyPoint key;
    Trim trim;
    double trimPosition; // 1 - x in tools/spice/generators.py
    double hz;           // ngspice ringing frequency
    double decay;        // ngspice -40 dB decay (s)
};

void testGeneratorsMatchSpice()
{
    // Reference values from ngspice (tools/spice/generators.py).
    const SpiceReference refs[] = {
        { KeyPoint::BassDrum, Trim::BassDrumLength, 0.5, 53.2, 0.10 },
        { KeyPoint::TomTom, Trim::TomTomLength, 0.5, 159.8, 0.23 },
        { KeyPoint::Bongo, Trim::BongoLength, 0.5, 237.9, 0.09 },
        { KeyPoint::Blocks, Trim::BlocksLength, 0.0, 1026.8, 0.03 },
        { KeyPoint::Clave, Trim::ClaveLength, 0.0, 2217.4, 0.01 },
        { KeyPoint::RimShot, Trim::RimShotLength, 0.5, 2004.0, 0.02 },
        { KeyPoint::HiDrum, Trim::HiDrumLength, 0.5, 482.1, 0.06 },
        { KeyPoint::LoDrum, Trim::LoDrumLength, 0.5, 318.4, 0.15 },
    };
    for (const auto& ref : refs)
    {
        Instrumentation inst;
        inst.setTrim(ref.trim, ref.trimPosition);
        inst.prepare(kRate);
        const auto out = renderKey(inst, ref.key, 1.0);
        const double decay = decaySeconds(out);
        // Skip the keying transient and pulse amp recovery, then measure over the ring.
        const double start = std::clamp(0.25 * decay, 0.006, 0.04);
        const double window = std::clamp(0.6 * decay, 0.01, 0.2);
        const double hz = zeroCrossingHz(out, static_cast<int>(start * kRate), static_cast<int>(window * kRate));
        std::printf("  %-16s %7.1f Hz (spice %7.1f)  decay %.2f s (spice %.2f)  peak %.2f V\n", trimName(ref.trim), hz,
                    ref.hz, decay, ref.decay, peakOf(out));
        CHECK_NEAR(hz, ref.hz, ref.hz * 0.02);
        CHECK_NEAR(decay, ref.decay, std::max(0.02, ref.decay * 0.3));
        CHECK(peakOf(out) > 0.5);
    }
}

void testLengthTrimmerRangeDeadToContinuous()
{
    // Manual: "Too little ... will cause the Tom Tom to sound dead, too much and
    // the Tom Tom will sound continuously."
    // Trims moved after prepare(), as the plugin does.
    Instrumentation dead, ringing;
    dead.prepare(kRate);
    ringing.prepare(kRate);
    dead.setTrim(Trim::TomTomLength, 0.0);
    ringing.setTrim(Trim::TomTomLength, 1.0);
    const auto a = renderKey(dead, KeyPoint::TomTom, 1.5);
    const auto b = renderKey(ringing, KeyPoint::TomTom, 1.5);
    CHECK(decaySeconds(a) < 0.15);
    const std::vector<float> tail(b.end() - kRate / 10, b.end());
    CHECK(peakOf(tail) > 0.2 * peakOf(b));
}

void testSoftLoDrumIsQuieter()
{
    Instrumentation inst;
    inst.prepare(kRate);
    const double normal = peakOf(renderKey(inst, KeyPoint::LoDrum, 0.5));
    const double soft = peakOf(renderKey(inst, KeyPoint::LoDrumSoft, 0.5));
    CHECK(soft < 0.7 * normal);
    CHECK(soft > 0.1 * normal);
}

std::vector<float> hiHat(const ControlLines& lines)
{
    Instrumentation inst;
    inst.prepare(kRate);
    inst.setControlLines(lines);
    return renderKey(inst, KeyPoint::HiHat, 1.5);
}

// Loudest level between t0 and t1 seconds, relative to the overall peak.
double levelBetween(const std::vector<float>& x, double t0, double t1)
{
    return peakOf(std::vector<float>(x.begin() + static_cast<long>(t0 * kRate), x.begin() + static_cast<long>(t1 * kRate)))
           / peakOf(x);
}

void testHiHatEnvelopeModes()
{
    ControlLines normal, med, lng, choked;
    med.hiHatMed = true;
    lng.hiHatLong = true;
    choked.hiHatChoke = true;
    const auto n = hiHat(normal), m = hiHat(med), l = hiHat(lng), c = hiHat(choked);
    std::printf("  hi-hat decay: normal %.2f, med %.2f, long %.2f s; level at 40-60 ms: normal %.2f, choked %.2f\n",
                decaySeconds(n), decaySeconds(m), decaySeconds(l), levelBetween(n, 0.04, 0.06),
                levelBetween(c, 0.04, 0.06));
    CHECK(decaySeconds(n) < decaySeconds(m));
    CHECK(decaySeconds(m) < decaySeconds(l));
    // Manual: choked is "a strong initial signal which quickly drops to about one third intensity".
    CHECK(levelBetween(c, 0.04, 0.06) < 0.6 * levelBetween(n, 0.04, 0.06));
}

double cymbalDecay(const ControlLines& lines)
{
    Instrumentation inst;
    inst.prepare(kRate);
    inst.setControlLines(lines);
    return decaySeconds(renderKey(inst, KeyPoint::Cymbal, 2.0));
}

void testCymbalEnvelopeModes()
{
    ControlLines normal, maracas, lng, choked;
    maracas.cymbalY = true;
    lng.cymbalZ = true;
    choked.cymbalY = choked.cymbalZ = true;
    const double dn = cymbalDecay(normal), dm = cymbalDecay(maracas), dl = cymbalDecay(lng), dc = cymbalDecay(choked);
    std::printf("  cymbal decay: maracas %.2f, choked %.2f, normal %.2f, long %.2f s\n", dm, dc, dn, dl);
    CHECK(dm < dn);
    CHECK(dc < dl);
    CHECK(dn < dl);
}

void testSnareRollRate()
{
    // Manual: "Proper speed is approximately 15 to 20 beats per second."
    Instrumentation inst;
    inst.prepare(kRate);
    for (int i = 0; i < kRate / 4; ++i)
        inst.process();
    inst.snareRoll(1.0);
    std::vector<float> out;
    for (int i = 0; i < kRate; ++i)
        out.push_back(inst.process());

    // Count strokes: onsets where the 2 ms envelope jumps up.
    const int win = kRate / 500;
    std::vector<double> env;
    for (size_t i = 0; i + static_cast<size_t>(win) <= out.size(); i += static_cast<size_t>(win))
    {
        double m = 0.0;
        for (int j = 0; j < win; ++j)
            m = std::max(m, static_cast<double>(std::fabs(out[i + static_cast<size_t>(j)])));
        env.push_back(m);
    }
    const double peak = *std::max_element(env.begin(), env.end());
    int strokes = 0;
    for (size_t i = 3; i < env.size(); ++i)
        if (env[i] > 0.5 * peak && env[i - 3] < 0.6 * env[i])
        {
            ++strokes;
            i += 10;
        }
    std::printf("  snare roll: %d strokes in 1 s\n", strokes);
    CHECK(strokes >= 14 && strokes <= 22);
}

void testIdleCircuitsSleep()
{
    Instrumentation inst;
    inst.prepare(kRate);
    for (int k = 0; k < kNumKeyPoints; ++k)
        inst.key(static_cast<KeyPoint>(k));
    for (int i = 0; i < kRate / 10; ++i)
        inst.process();
    CHECK(inst.awakeCircuits() != 0);
    for (int i = 0; i < kRate * 4; ++i)
        inst.process();
    CHECK(inst.awakeCircuits() == 0);
}

// --- Rhythms and clock ------------------------------------------------------

void testRhythmBank()
{
    for (const auto& e : rhythmBankErrors())
        std::printf("  %s\n", e.c_str());
    CHECK(rhythmBankErrors().empty());

    int latin = 0, american = 0, traditional = 0, triplet = 0;
    for (const auto& r : rhythms())
    {
        latin += r.category == Category::Latin;
        american += r.category == Category::American;
        traditional += r.category == Category::Traditional;
        triplet += r.triplet;
        int events = 0;
        for (const auto& p : r.pulses)
            events += p.keys != 0;
        CHECK(events > 0);
    }
    // Spec sheet: 8 Latin, 7 American, 3 Traditional; manual: eight triplet rhythms.
    CHECK(latin == 8);
    CHECK(american == 7);
    CHECK(traditional == 3);
    CHECK(triplet == 8);
}

void testBossaNovaDecodesToClockPulses()
{
    // Dwg 3 keys the Bossa Nova blocks from clock outputs 23, 29 and 18. Per the
    // clock description these fire at pulses (ring, stage) (3,1); (1,3), (0,0);
    // (0,4), (2,3).
    const auto& bossa = rhythms()[0];
    CHECK(bossa.name == "Bossa Nova");
    std::vector<int> blocks;
    for (int i = 0; i < kPulsesPerCycle; ++i)
        if ((bossa.pulses[static_cast<size_t>(i)].keys & keyBit(KeyPoint::Blocks)) != 0)
            blocks.push_back(i);
    const std::vector<int> expected = { 0 * 5 + 0, 0 * 5 + 4, 1 * 5 + 3, 2 * 5 + 3, 3 * 5 + 1 };
    CHECK(blocks == expected);
}

void testPulseTiming()
{
    // Straight ring cycle: stages at 0, 2, 3, 4, 6 sixteenths; triplet: 0, 2, 3, 4, 5 triplet-eighths.
    CHECK_NEAR(pulseBeat(1, false), 0.5, 1e-12);
    CHECK_NEAR(pulseBeat(4, false), 1.5, 1e-12);
    CHECK_NEAR(pulseBeat(9, false), 3.5, 1e-12);
    CHECK_NEAR(pulseBeat(1, true), 2.0 / 3.0, 1e-12);
    CHECK_NEAR(pulseBeat(4, true), 5.0 / 3.0, 1e-12);
    CHECK_NEAR(pulseBeat(19, true), 7.0 + 2.0 / 3.0, 1e-12);
}

void testFreeRunningClock()
{
    for (bool triplet : { false, true })
    {
        Clock clock;
        clock.prepare(kRate);
        clock.setTempo(120.0); // 24000 samples per beat
        clock.setTriplet(triplet);
        clock.start();

        std::vector<std::pair<long long, int>> hits;
        long long blockStart = 0;
        for (int block = 0; block < 2000; ++block)
        {
            const int n = 333 + (block % 7) * 100;
            clock.advance(n, [&](int offset, int index) { hits.push_back({ blockStart + offset, index }); });
            blockStart += n;
        }
        CHECK(hits.size() > 20);
        for (size_t i = 0; i < hits.size(); ++i)
        {
            const int index = static_cast<int>(i % kPulsesPerCycle);
            const double beat = static_cast<double>(i / kPulsesPerCycle) * kBeatsPerCycle + pulseBeat(index, triplet);
            CHECK(hits[i].second == index);
            CHECK(std::llabs(hits[i].first - static_cast<long long>(beat * 24000.0)) <= 1);
        }
    }
}

void testHostSyncedClock()
{
    Clock clock;
    clock.prepare(kRate);
    std::vector<std::pair<long long, int>> hits;
    long long blockStart = 0;
    for (int block = 0; block < 300; ++block)
    {
        const int n = 512;
        clock.advanceSynced(blockStart / 24000.0, 120.0, n,
                            [&](int offset, int index) { hits.push_back({ blockStart + offset, index }); });
        blockStart += n;
    }
    CHECK(!hits.empty());
    for (size_t i = 0; i < hits.size(); ++i)
    {
        const int index = static_cast<int>(i % kPulsesPerCycle);
        const double beat = static_cast<double>(i / kPulsesPerCycle) * kBeatsPerCycle + pulseBeat(index, false);
        CHECK(hits[i].second == index);
        CHECK(std::llabs(hits[i].first - static_cast<long long>(beat * 24000.0)) <= 1);
    }

    // Starting mid-cycle lands on the right pulse: beat 4.75 is ring 2, stage 2.
    clock.resetSync();
    int first = -1;
    clock.advanceSynced(4.74, 120.0, 2048, [&](int, int index) {
        if (first < 0)
            first = index;
    });
    CHECK(first == 12);
}

void testDrumMachinePlaysAndStops()
{
    DrumMachine dm;
    dm.prepare(kRate);
    dm.setTempo(140.0);
    dm.setRhythms(1u << 0); // Bossa Nova
    dm.setRunning(true);

    std::vector<float> buf(static_cast<size_t>(kRate * 2));
    dm.render(buf.data(), static_cast<int>(buf.size()), nullptr);
    const double peak = peakOf(buf);
    std::printf("  bossa nova peak %.3f FS\n", peak);
    CHECK(peak > 0.05);
    CHECK(peak < 1.0);
    CHECK(dm.currentPulse() >= 0);

    dm.setRunning(false);
    dm.render(buf.data(), static_cast<int>(buf.size()), nullptr);
    dm.render(buf.data(), static_cast<int>(buf.size()), nullptr);
    CHECK(dm.currentPulse() == -1);
    CHECK(peakOf(std::vector<float>(buf.end() - kRate / 2, buf.end())) < 1e-4);
}

void testPushButtonsSound()
{
    DrumMachine dm;
    dm.prepare(kRate);
    std::vector<float> buf(static_cast<size_t>(kRate / 2));
    for (int b = 0; b < kNumButtons; ++b)
    {
        dm.render(buf.data(), static_cast<int>(buf.size()), nullptr);
        dm.requestButton(static_cast<Button>(b), true);
        dm.render(buf.data(), kRate / 20, nullptr);
        const double pressed = peakOf(std::vector<float>(buf.begin(), buf.begin() + kRate / 20));
        dm.requestButton(static_cast<Button>(b), false);
        dm.render(buf.data(), static_cast<int>(buf.size()), nullptr);
        const double peak = std::max(pressed, peakOf(buf));
        std::printf("  %-14s button peak %.3f FS\n", buttonName(static_cast<Button>(b)), peak);
        CHECK(peak > 0.01);
    }
}

} // namespace

int main()
{
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        { "circuit: RC step response", testRcStepResponse },
        { "circuit: LC resonance", testLcResonance },
        { "circuit: diode clamp", testDiodeClamp },
        { "circuit: bass drum operating point vs ngspice", testBassDrumOperatingPointMatchesSpice },
        { "generators match ngspice pitch and decay", testGeneratorsMatchSpice },
        { "length trimmer: dead to continuous", testLengthTrimmerRangeDeadToContinuous },
        { "soft lo drum", testSoftLoDrumIsQuieter },
        { "hi-hat envelope modes", testHiHatEnvelopeModes },
        { "cymbal envelope modes", testCymbalEnvelopeModes },
        { "snare roll rate", testSnareRollRate },
        { "idle circuits sleep", testIdleCircuitsSleep },
        { "rhythm bank", testRhythmBank },
        { "bossa nova decodes to clock pulses", testBossaNovaDecodesToClockPulses },
        { "pulse timing", testPulseTiming },
        { "free-running clock", testFreeRunningClock },
        { "host-synced clock", testHostSyncedClock },
        { "drum machine plays and stops", testDrumMachinePlaysAndStops },
        { "push buttons", testPushButtonsSound },
    };

    for (const auto& [name, fn] : tests)
    {
        std::printf("%s\n", name);
        fn();
    }

    std::printf(failures == 0 ? "All tests passed\n" : "%d check(s) failed\n", failures);
    return failures == 0 ? 0 : 1;
}
