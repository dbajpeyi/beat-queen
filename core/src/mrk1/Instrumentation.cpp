#include "beatqueen/mrk1/Instrumentation.h"

#include <algorithm>
#include <cmath>
#include <vector>

// Component values are from the MRK-1 service manual, Drawing No. 4
// ("Instrumentation and Power Supply"), with capacitor decimals confirmed
// against the parts list (all instrumentation capacitors are Mylar <= 0.56 uF,
// so a printed "1" is 0.1 uF, "12" is 0.12 uF, "22" is 0.22 uF, ...).
// Values the manual does not give are marked ASSUMPTION; docs/DESIGN.md
// explains each one.

namespace beatqueen::mrk1 {

using circuit::Circuit;
using circuit::kGround;
using circuit::NodeId;

namespace {

constexpr double kVcc = 17.0; // regulated supply (Q63)
constexpr double kSixVolt = 6.0;
constexpr double kOneVolt = 1.0; // gate transistor emitter rail (12K / 820 divider)

// ASSUMPTION: a clock pulse at a keying point, as delivered by a clock pulse
// amplifier (Q6-Q10, 2.2K collector load) through the matrix.
constexpr double kKeyVolts = 15.0;
constexpr double kKeySourceOhms = 2.2e3;
constexpr double kKeySeconds = 0.005;

// ASSUMPTION: the summing bus (all voice outputs, the 47K bus resistor and the
// preamp input) seen from any one voice.
constexpr double kBusOhms = 10e3;

// ASSUMPTION: noise generator Q55, a reverse-biased base-emitter junction fed
// from +17 V through 1M, modelled as a 7 V zener with white noise. The noise
// level is calibrated, not documented: at 10 mV rms the noise volume trimmers
// can reach the balance the manual asks for (cymbal level with the drums,
// hi-hat "quite a bit louder" than the cymbal) without the gates saturating.
constexpr double kZenerVolts = 7.0;
constexpr double kZenerOhms = 4.7e3;
constexpr double kNoiseRmsVolts = 0.01;

// Snare drum trigger (Q52/Q53 astable, enabled by Q54).
constexpr double kSnareKeySeconds = 0.01e-6 * 220e3;   // Q54 base network stretches a keying pulse
constexpr double kSnarePhaseBSeconds = 0.693 * 100e3 * 0.56e-6;
double snarePhaseASeconds(double speedTrim) { return 0.693 * (120e3 + (1.0 - speedTrim) * 250e3) * 0.1e-6; }

// One-shot trigger widths: 0.693 x 100K x timing capacitor.
constexpr double kHiHatOneShotSeconds = 0.693 * 100e3 * 0.22e-6;
constexpr double kCymbalOneShotSeconds = 0.693 * 100e3 * 0.1e-6;

// ASSUMPTION: hi-hat dampen transistor Q17 stays on while its 0.1 uF base
// capacitor discharges through 220K.
constexpr double kQ17OnSeconds = 0.1;

// ASSUMPTION: the (2) accent pulse on the cymbal "D" keying point.
constexpr double kAccentSourceOhms = 10e3;

constexpr double kSwitchOhms = 1.0;

// A noise voice's gate needs its envelope line at roughly 2.2-2.8 V before it
// conducts (two diode drops above the gate's +1 V emitter rail). Diode leakage
// leaves the line hovering around a volt for a long time after a hit, so below
// this the voice is silent and may sleep.
constexpr double kSilentEnvelopeVolts = 1.2;

// ---------------------------------------------------------------------------

class SwitchTimers
{
public:
    void schedule(int samples, int sw, bool closed)
    {
        for (int i = 0; i < count_; ++i)
            if (items_[static_cast<size_t>(i)].sw == sw)
            {
                items_[static_cast<size_t>(i)] = { samples, sw, closed };
                return;
            }
        if (count_ < static_cast<int>(items_.size()))
            items_[static_cast<size_t>(count_++)] = { samples, sw, closed };
    }

    void cancel(int sw)
    {
        for (int i = 0; i < count_; ++i)
            if (items_[static_cast<size_t>(i)].sw == sw)
            {
                items_[static_cast<size_t>(i)] = items_[static_cast<size_t>(--count_)];
                return;
            }
    }

    void tick(Circuit& c)
    {
        for (int i = 0; i < count_;)
        {
            auto& p = items_[static_cast<size_t>(i)];
            if (--p.samples <= 0)
            {
                c.setSwitch(p.sw, p.closed);
                p = items_[static_cast<size_t>(--count_)];
            }
            else
                ++i;
        }
    }

    bool empty() const { return count_ == 0; }

private:
    struct Pending
    {
        int samples, sw;
        bool closed;
    };
    std::array<Pending, 12> items_ {};
    int count_ = 0;
};

// A large resistor joining two separately simulated stages of one voice. Each
// side sees the other through a Norton equivalent driven by the other side's
// previous-sample voltage. Every node it touches has an RC time constant of
// milliseconds or more, so the one-sample lag is negligible.
struct Bridge
{
    Circuit* a = nullptr;
    Circuit* b = nullptr;
    NodeId na = kGround, nb = kGround;
    int sa = -1, sb = -1;
    double ohms = 1.0;

    void exchange() const
    {
        a->setCurrent(sa, b->voltage(nb) / ohms);
        b->setCurrent(sb, a->voltage(na) / ohms);
    }
};

// One simulated voice feeding the summing bus. Generators are one circuit;
// noise voices are split into an envelope stage (`c`, which holds every
// switch) and an audio stage, joined by bridges.
struct Voice
{
    struct Watch
    {
        const Circuit* circuit;
        NodeId node;
        double threshold;
    };

    Circuit c;
    Circuit audio;
    bool twoStage = false;
    std::vector<Bridge> bridges;
    NodeId bus = kGround;
    double busOp = 0.0;
    SwitchTimers timers;
    std::vector<Watch> watch; // all below threshold (and nothing pending) = silent
    int holds = 0;            // held inputs that keep the voice awake
    bool awake = true;
    int quietSamples = 0;

    Circuit& busCircuit() { return twoStage ? audio : c; }
    const Circuit& busCircuit() const { return twoStage ? audio : c; }

    NodeId addBus()
    {
        bus = busCircuit().addNode();
        busCircuit().addResistor(bus, kGround, kBusOhms);
        return bus;
    }

    void bridge(NodeId envelopeNode, NodeId audioNode, double ohms)
    {
        Bridge br;
        br.a = &c;
        br.b = &audio;
        br.na = envelopeNode;
        br.nb = audioNode;
        br.ohms = ohms;
        c.addResistor(envelopeNode, kGround, ohms);
        br.sa = c.addCurrentSource(kGround, envelopeNode);
        audio.addResistor(audioNode, kGround, ohms);
        br.sb = audio.addCurrentSource(kGround, audioNode);
        bridges.push_back(br);
    }

    void exchange() const
    {
        for (const auto& br : bridges)
            br.exchange();
    }

    // Brings coupled stages to a common operating point (Gauss-Seidel).
    void settle()
    {
        if (twoStage)
            for (int i = 0; i < 12; ++i)
            {
                exchange();
                c.updateOperatingPoint();
                exchange();
                audio.updateOperatingPoint();
            }
        exchange();
        busOp = busCircuit().operatingPointVoltage(bus);
        awake = false;
    }

    void finishPrepare(double sampleRate)
    {
        c.prepare(sampleRate);
        if (twoStage)
            audio.prepare(sampleRate);
        settle();
    }

    void retrim(Circuit& circuit, int resistor, double ohms)
    {
        circuit.setResistor(resistor, ohms);
        settle();
    }

    void wake()
    {
        if (!awake)
        {
            c.resetToOperatingPoint();
            if (twoStage)
                audio.resetToOperatingPoint();
            exchange();
            awake = true;
        }
        quietSamples = 0;
    }

    double output() const { return awake ? busCircuit().voltage(bus) - busOp : 0.0; }

    void tick(int sampleRateInt)
    {
        if (!awake)
            return;
        timers.tick(c);
        if (twoStage)
        {
            exchange();
            c.step();
            audio.step();
        }
        else
            c.step();

        if ((++quietSamples & 63) == 0)
        {
            bool quiet = timers.empty() && holds == 0;
            for (const auto& w : watch)
                quiet = quiet
                        && std::fabs(w.circuit->voltage(w.node) - w.circuit->operatingPointVoltage(w.node)) < w.threshold;
            if (!quiet)
                quietSamples = 0;
            else if (quietSamples > sampleRateInt / 10)
                awake = false;
        }
    }
};

// ---------------------------------------------------------------------------
// Twin-T generators (bass drum, tom tom, bongo, blocks, clave, rim shot, hi drum, lo drum)

struct GeneratorSpec
{
    KeyPoint key;
    Trim trim;
    double inR, inC;   // keying input into the pulse amp base
    double buttonC;    // push button coupling capacitor (0 = no push button)
    double cpa;        // pulse amp collector capacitor
    double rp;         // pulse amp to twin-T centre
    double c1, cs;     // twin-T series capacitors and R-arm shunt capacitor (56K arms)
    double cc;         // collector coupling capacitor
    double rs;         // emitter resistor in series with the 1K length trimmer
    enum class Out { RcR, R, RSeriesC } out;
    double outR1, outC, outR2;
    bool snareBranch; // hi drum / lo drum: keyed by the snare drum trigger too
    bool softInput;   // lo drum: SOFT keying point on the emitter (serial 09001 on)
    double ringHz;    // nominal ringing frequency (ngspice), used to pre-warp the simulation
};

// clang-format off
const GeneratorSpec kGenerators[] = {
    // key                 trim                  inR    inC      button   cpa      rp     c1        cs        cc       rs    output                                       snare  soft   ring Hz
    { KeyPoint::BassDrum, Trim::BassDrumLength, 15e3, .22e-6,  .02e-6,  .1e-6,  12e3,  .082e-6,  .082e-6,  .12e-6,  270, GeneratorSpec::Out::RcR,      33e3, .15e-6,  22e3, false, false, 53 }, // Q32/Q33
    { KeyPoint::TomTom,   Trim::TomTomLength,   10e3, .1e-6,   .02e-6,  .12e-6, 6.8e3, .027e-6,  .047e-6,  .047e-6, 470, GeneratorSpec::Out::RcR,      33e3, .01e-6,  82e3, false, false, 160 }, // Q34/Q35
    { KeyPoint::Bongo,    Trim::BongoLength,    10e3, .047e-6, .02e-6,  .1e-6,  6.8e3, .015e-6,  .047e-6,  .047e-6, 820, GeneratorSpec::Out::RcR,      33e3, .01e-6,  47e3, false, false, 238 }, // Q36/Q37
    { KeyPoint::Blocks,   Trim::BlocksLength,   10e3, .022e-6, .02e-6,  .1e-6,  6.8e3, .0033e-6, .01e-6,   .047e-6, 820, GeneratorSpec::Out::R,        220e3, 0,      0,    false, false, 1040 }, // Q38/Q39
    { KeyPoint::Clave,    Trim::ClaveLength,    10e3, .01e-6,  .022e-6, .022e-6, 6.8e3, .0015e-6, .0047e-6, .047e-6, 820, GeneratorSpec::Out::R,        180e3, 0,      0,    false, false, 2240 }, // Q40/Q41
    { KeyPoint::RimShot,  Trim::RimShotLength,  10e3, .022e-6, 0,       .1e-6,  6.8e3, .0015e-6, .0082e-6, .047e-6, 560, GeneratorSpec::Out::R,        47e3,  0,      0,    false, false, 2000 }, // Q42/Q43
    { KeyPoint::HiDrum,   Trim::HiDrumLength,   10e3, .022e-6, 0,       .1e-6,  3.3e3, .0082e-6, .022e-6,  .047e-6, 1200, GeneratorSpec::Out::RSeriesC, 150e3, .0015e-6, 0,  true,  false, 482 }, // Q44/Q45
    { KeyPoint::LoDrum,   Trim::LoDrumLength,   10e3, .022e-6, 0,       .1e-6,  4.7e3, .012e-6,  .033e-6,  .047e-6, 1000, GeneratorSpec::Out::RSeriesC, 100e3, .0033e-6, 0,  true,  true , 318 }, // Q46/Q47
};
// clang-format on
constexpr int kNumGenerators = static_cast<int>(sizeof(kGenerators) / sizeof(kGenerators[0]));

struct Generator
{
    Voice v;
    const GeneratorSpec* spec = nullptr;
    int keyLow = -1, keyHigh = -1, buttonLow = -1, buttonHigh = -1, snare = -1, soft = -1;
    int emitterResistor = -1;

    static double emitterOhms(const GeneratorSpec& s, double trim) { return s.rs + (1.0 - trim) * 1e3; }

    void build(const GeneratorSpec& s, double trim)
    {
        spec = &s;
        auto& c = v.c;
        c.setPrewarpFrequency(s.ringHz);
        const NodeId vcc = c.addRail(kVcc), vkey = c.addRail(kKeyVolts);

        // Pulse amp: a keying pulse saturates the transistor, which dumps its
        // collector capacitor and pulls the twin-T centre down through Rp.
        const NodeId key = c.addNode(), k1 = c.addNode(), base = c.addNode(), col = c.addNode(), p = c.addNode();
        keyLow = c.addSwitch(key, kGround, kSwitchOhms);
        keyHigh = c.addSwitch(key, vkey, kKeySourceOhms);
        c.addResistor(key, k1, s.inR);
        c.addCapacitor(k1, base, s.inC);
        c.addResistor(base, kGround, 100e3);
        if (s.buttonC > 0)
        {
            const NodeId b = c.addNode(), b2 = c.addNode();
            buttonLow = c.addSwitch(b, kGround, kSwitchOhms);
            buttonHigh = c.addSwitch(b, vcc, kSwitchOhms);
            c.addResistor(b, b2, 47e3);
            c.addCapacitor(b2, base, s.buttonC);
        }
        c.addBjt(col, base, kGround);
        c.addCapacitor(col, kGround, s.cpa);
        c.addResistor(p, col, 33e3);
        c.addResistor(vcc, p, 2.2e3);

        // Twin-T oscillator held just below oscillation; the emitter trimmer sets loop gain.
        const NodeId m = c.addNode(), a = c.addNode(), n = c.addNode(), b = c.addNode(), gc = c.addNode(),
                     ge = c.addNode();
        c.addResistor(p, m, s.rp);
        c.addCapacitor(a, m, s.c1);
        c.addCapacitor(m, b, s.c1);
        c.addResistor(a, n, 56e3);
        c.addResistor(n, b, 56e3);
        c.addCapacitor(n, kGround, s.cs);
        c.addBjt(gc, b, ge);
        c.addResistor(vcc, gc, 68e3);
        c.addResistor(gc, b, 1.2e6);
        c.addCapacitor(gc, a, s.cc);
        emitterResistor = c.addResistor(ge, kGround, emitterOhms(s, trim));

        const NodeId bus = v.addBus();
        switch (s.out)
        {
            case GeneratorSpec::Out::RcR:
            {
                const NodeId o = c.addNode();
                c.addResistor(a, o, s.outR1);
                c.addCapacitor(o, kGround, s.outC);
                c.addResistor(o, bus, s.outR2);
                break;
            }
            case GeneratorSpec::Out::R:
                c.addResistor(a, bus, s.outR1);
                break;
            case GeneratorSpec::Out::RSeriesC:
            {
                const NodeId o = c.addNode();
                c.addResistor(a, o, s.outR1);
                c.addCapacitor(o, bus, s.outC);
                break;
            }
        }

        if (s.snareBranch)
        {
            // Snare drum trigger Q52 pulls this node down through D19 (one copy per generator).
            const NodeId sn = c.addNode(), q52 = c.addNode();
            c.addResistor(p, sn, 33e3);
            c.addCapacitor(sn, kGround, 0.12e-6);
            c.addDiode(sn, q52);
            c.addResistor(vcc, q52, 4.7e3);
            snare = c.addSwitch(q52, kGround, kSwitchOhms);
        }
        if (s.softInput)
        {
            const NodeId ks = c.addNode();
            c.addResistor(ks, ge, 47e3);
            soft = c.addSwitch(ks, vkey, kKeySourceOhms);
        }

        c.setSwitch(keyLow, true);
        if (buttonLow >= 0)
            c.setSwitch(buttonLow, true);
        v.watch = { { &c, bus, 1e-6 }, { &c, gc, 1e-5 } };
    }

    void key(int samples)
    {
        v.wake();
        v.c.setSwitch(keyLow, false);
        v.c.setSwitch(keyHigh, true);
        v.timers.schedule(samples, keyHigh, false);
        v.timers.schedule(samples, keyLow, true);
    }

    void softKey(int samples)
    {
        v.wake();
        v.c.setSwitch(soft, true);
        v.timers.schedule(samples, soft, false);
    }

    void setButton(bool down)
    {
        if (buttonLow < 0)
            return;
        v.wake();
        v.c.setSwitch(buttonLow, !down);
        v.c.setSwitch(buttonHigh, down);
        v.holds = down ? 1 : 0;
    }

    void setSnareStroke(bool on)
    {
        v.wake();
        v.c.setSwitch(snare, on);
    }
};

// ---------------------------------------------------------------------------
// Noise voices

// Shared noise generator (Q55) as a Norton source in each noise circuit.
struct NoiseInput
{
    int source = -1;
    void build(Circuit& c, NodeId vcc, NodeId nz)
    {
        c.addResistor(vcc, nz, 1e6);
        c.addResistor(nz, kGround, kZenerOhms);
        source = c.addCurrentSource(kGround, nz);
        c.setCurrent(source, kZenerVolts / kZenerOhms);
    }
    void set(Circuit& c, double noiseVolts) const { c.setCurrent(source, (kZenerVolts + noiseVolts) / kZenerOhms); }
};

double noiseVolumeOhms(double trim) { return 1.0 + (1.0 - trim) * 1e3; }

double tankHz(double henries, double farads) { return 1.0 / (2.0 * 3.14159265358979323846 * std::sqrt(henries * farads)); }

struct SnareNoise
{
    Voice v;
    NoiseInput noise;
    int q53 = -1, volume = -1;

    void build(double volumeTrim)
    {
        v.twoStage = true;
        auto& e = v.c;
        auto& a = v.audio;

        // Envelope stage: Q53 (on at rest) releases its collector during a
        // stroke and D20 charges the 0.068 uF envelope capacitor.
        const NodeId vcc = e.addRail(kVcc);
        const NodeId q53c = e.addNode(), env = e.addNode();
        e.addResistor(vcc, q53c, 1e3);
        q53 = e.addSwitch(q53c, kGround, kSwitchOhms);
        e.addDiode(q53c, env); // D20
        e.addCapacitor(env, kGround, 0.068e-6);

        // Audio stage: snare drum noise amp Q56 (biased only by the envelope
        // through 10M) and snare drum gate Q57 with the L3 tank.
        a.setPrewarpFrequency(tankHz(27e-3, 0.082e-6));
        const NodeId avcc = a.addRail(kVcc), one = a.addRail(kOneVolt);
        const NodeId nz = a.addNode(), b56 = a.addNode(), c56 = a.addNode(), e56 = a.addNode(), x = a.addNode(),
                     w = a.addNode(), g = a.addNode(), c57 = a.addNode();
        noise.build(a, avcc, nz);
        a.addCapacitor(nz, b56, 0.01e-6);
        a.addBjt(c56, b56, e56);
        a.addResistor(avcc, c56, 3.3e3);
        volume = a.addResistor(e56, kGround, noiseVolumeOhms(volumeTrim)); // VR12
        a.addCapacitor(c56, x, 0.0068e-6);
        a.addDiode(x, w); // D21
        a.addResistor(w, g, 27e3);
        a.addResistor(g, one, 470e3);
        a.addCapacitor(g, kGround, 0.0033e-6);
        a.addBjt(c57, g, one);
        a.addInductor(avcc, c57, 27e-3);
        a.addCapacitor(avcc, c57, 0.082e-6);
        a.addResistor(avcc, c57, 390.0);
        const NodeId bus = v.addBus();
        a.addCapacitor(c57, bus, 0.0047e-6);

        v.bridge(env, b56, 10e6);
        v.bridge(env, x, 560e3);

        e.setSwitch(q53, true);
        v.watch = { { &a, bus, 1e-6 }, { &e, env, kSilentEnvelopeVolts } };
    }
};

struct HiHat
{
    Voice v;
    NoiseInput noise;
    int q51 = -1, med = -1, lng = -1, choke = -1, dampenLink = -1, q17 = -1, buttonRest = -1, buttonPress = -1;
    int volume = -1;

    void build(double volumeTrim)
    {
        v.twoStage = true;
        auto& e = v.c;
        auto& a = v.audio;

        const NodeId vcc = e.addRail(kVcc), six = e.addRail(kSixVolt);
        const NodeId q51c = e.addNode(), line = e.addNode();

        // Trigger one-shot Q50/Q51: Q51 (on at rest) releases, D22 charges the envelope line.
        e.addResistor(vcc, q51c, 4.7e3);
        q51 = e.addSwitch(q51c, kGround, kSwitchOhms);
        e.addDiode(q51c, line); // D22
        e.addCapacitor(line, kGround, 0.1e-6);

        // Control keying points "med", "long" and "A".
        const NodeId km = e.addNode(), kl = e.addNode(), ka = e.addNode(), d25 = e.addNode();
        e.addCapacitor(line, km, 0.068e-6);
        med = e.addSwitch(km, kGround, kSwitchOhms);
        e.addCapacitor(line, kl, 0.22e-6);
        lng = e.addSwitch(kl, kGround, kSwitchOhms);
        e.addDiode(line, d25); // D25
        e.addResistor(d25, ka, 47e3);
        e.addCapacitor(line, ka, 0.1e-6);
        choke = e.addSwitch(ka, six, kSwitchOhms);

        // "C": the clock board dampen network (Q17, Dwg 1).
        const NodeId n17 = e.addNode(), n10 = e.addNode();
        dampenLink = e.addSwitch(line, n17, kSwitchOhms);
        e.addCapacitor(n17, kGround, 0.033e-6);
        e.addDiode(n17, n10); // D10
        e.addResistor(n17, n10, 4.7e6);
        e.addCapacitor(n10, kGround, 0.15e-6);
        q17 = e.addSwitch(n17, kGround, 390e3);

        // Push button: 0.22 uF charged from +17 V through 6.8K, dumped onto the line.
        const NodeId kb = e.addNode(), kc = e.addNode();
        buttonRest = e.addSwitch(kb, vcc, kSwitchOhms);
        buttonPress = e.addSwitch(kb, line, kSwitchOhms);
        e.addResistor(kb, kc, 6.8e3);
        e.addCapacitor(kc, kGround, 0.22e-6);

        const NodeId u = e.addNode();
        e.addDiode(line, u); // D23

        // Audio stage: hi-hat noise amp Q58 and hi-hat gate Q59 with the L2 tank.
        a.setPrewarpFrequency(tankHz(27e-3, 0.068e-6));
        const NodeId avcc = a.addRail(kVcc), one = a.addRail(kOneVolt);
        const NodeId nz = a.addNode(), b58 = a.addNode(), c58 = a.addNode(), e58 = a.addNode(), vn = a.addNode(),
                     w = a.addNode(), g = a.addNode(), c59 = a.addNode(), ho = a.addNode();
        noise.build(a, avcc, nz);
        a.addCapacitor(nz, b58, 0.02e-6);
        a.addBjt(c58, b58, e58);
        a.addResistor(avcc, c58, 10e3);
        volume = a.addResistor(e58, kGround, noiseVolumeOhms(volumeTrim)); // VR11
        a.addCapacitor(c58, vn, 0.0068e-6);
        a.addDiode(vn, w); // D24
        a.addResistor(w, g, 27e3);
        a.addResistor(g, one, 470e3);
        a.addCapacitor(g, kGround, 0.0033e-6);
        a.addBjt(c59, g, one);
        a.addInductor(avcc, c59, 27e-3);
        a.addCapacitor(avcc, c59, 0.068e-6);
        a.addResistor(avcc, c59, 3.3e3);
        a.addResistor(c59, ho, 180e3);
        a.addCapacitor(c59, ho, 200e-12);
        const NodeId bus = v.addBus();
        a.addCapacitor(ho, bus, 0.01e-6);

        v.bridge(u, b58, 10e6);
        v.bridge(u, vn, 820e3);

        e.setSwitch(q51, true);
        e.setSwitch(buttonRest, true);
        v.watch = { { &a, bus, 1e-6 }, { &e, line, kSilentEnvelopeVolts } };
    }

    void trigger(int samples)
    {
        v.wake();
        v.c.setSwitch(q51, false);
        v.timers.schedule(samples, q51, true);
    }

    void setButton(bool down)
    {
        v.wake();
        v.c.setSwitch(buttonRest, !down);
        v.c.setSwitch(buttonPress, down);
        v.holds = down ? 1 : 0;
    }
};

struct Cymbal
{
    Voice v;
    NoiseInput noise;
    int q49 = -1, y = -1, z = -1, accent = -1, buttonRest = -1, buttonPress = -1, volume = -1;

    void build(double volumeTrim)
    {
        v.twoStage = true;
        auto& e = v.c;
        auto& a = v.audio;

        const NodeId vcc = e.addRail(kVcc), six = e.addRail(kSixVolt), vkey = e.addRail(kKeyVolts);
        const NodeId q49c = e.addNode(), line = e.addNode();

        // Trigger one-shot Q48/Q49 and D29 onto the envelope line ("B").
        e.addResistor(vcc, q49c, 4.7e3);
        q49 = e.addSwitch(q49c, kGround, kSwitchOhms);
        e.addDiode(q49c, line); // D29
        e.addCapacitor(line, kGround, 0.056e-6);

        // Y: +6 V through 47K and D17. Z: 0.1 uF to ground.
        const NodeId ky = e.addNode(), k17 = e.addNode(), kz = e.addNode();
        y = e.addSwitch(line, ky, kSwitchOhms);
        e.addDiode(ky, k17); // D17
        e.addResistor(k17, six, 47e3);
        z = e.addSwitch(line, kz, kSwitchOhms);
        e.addCapacitor(kz, kGround, 0.1e-6);

        // Push button: 0.39 uF charged through 6.8K, dumped onto the line.
        const NodeId kb = e.addNode(), kc = e.addNode();
        buttonRest = e.addSwitch(kb, vcc, kSwitchOhms);
        buttonPress = e.addSwitch(kb, line, kSwitchOhms);
        e.addResistor(kb, kc, 6.8e3);
        e.addCapacitor(kc, kGround, 0.39e-6);

        // Noise amp bias: from the line through D30, or from "D" through D26/D27.
        const NodeId f = e.addNode(), kd = e.addNode(), dl = e.addNode();
        e.addDiode(line, f); // D30
        accent = e.addSwitch(kd, vkey, kAccentSourceOhms);
        e.addDiode(kd, dl); // D26
        e.addDiode(dl, f);  // D27

        // Audio stage: cymbal noise amp Q60, the two diode paths into the cymbal
        // gate Q61, its L1 tank and the 15K / 0.0022 uF low-pass.
        a.setPrewarpFrequency(tankHz(27e-3, 0.01e-6));
        const NodeId avcc = a.addRail(kVcc), one = a.addRail(kOneVolt);
        const NodeId nz = a.addNode(), b60 = a.addNode(), c60 = a.addNode(), e60 = a.addNode(), y2 = a.addNode(),
                     y3 = a.addNode(), zn = a.addNode(), g = a.addNode(), c61 = a.addNode(), o = a.addNode();
        noise.build(a, avcc, nz);
        a.addCapacitor(nz, b60, 0.01e-6);
        a.addBjt(c60, b60, e60);
        a.addResistor(avcc, c60, 10e3);
        volume = a.addResistor(e60, kGround, noiseVolumeOhms(volumeTrim)); // VR10
        a.addCapacitor(c60, y2, 0.0047e-6);
        a.addDiode(y2, zn); // D31
        a.addCapacitor(c60, y3, 0.0068e-6);
        a.addDiode(y3, zn); // D28
        a.addResistor(zn, g, 100e3);
        a.addResistor(g, one, 470e3);
        a.addCapacitor(g, kGround, 0.0033e-6);
        a.addBjt(c61, g, one);
        a.addInductor(avcc, c61, 27e-3);
        a.addCapacitor(avcc, c61, 0.01e-6);
        a.addResistor(avcc, c61, 6.8e3);
        a.addResistor(c61, o, 15e3);
        a.addCapacitor(o, kGround, 0.0022e-6);
        const NodeId bus = v.addBus();
        a.addCapacitor(o, bus, 0.0022e-6);

        v.bridge(f, b60, 10e6);
        v.bridge(line, y2, 1e6);
        v.bridge(dl, y3, 680e3);

        e.setSwitch(q49, true);
        e.setSwitch(buttonRest, true);
        v.watch = { { &a, bus, 1e-6 }, { &e, line, kSilentEnvelopeVolts } };
    }

    void trigger(int samples)
    {
        v.wake();
        v.c.setSwitch(q49, false);
        v.timers.schedule(samples, q49, true);
    }

    void setButton(bool down)
    {
        v.wake();
        v.c.setSwitch(buttonRest, !down);
        v.c.setSwitch(buttonPress, down);
        v.holds = down ? 1 : 0;
    }
};

// ---------------------------------------------------------------------------
// Audio output preamp Q62, driven by the summed bus.

struct Preamp
{
    Circuit c;
    int input = -1;
    NodeId out = kGround;

    void build()
    {
        const NodeId vcc = c.addRail(kVcc);
        const NodeId bus = c.addNode(), n1 = c.addNode(), b = c.addNode(), col = c.addNode(), e = c.addNode();
        out = c.addNode();
        c.addResistor(bus, kGround, kBusOhms);
        input = c.addCurrentSource(kGround, bus);
        c.addResistor(bus, n1, 22e3);
        c.addCapacitor(n1, b, 0.12e-6);
        c.addBjt(col, b, e);
        c.addResistor(vcc, col, 10e3);
        c.addResistor(col, b, 1.5e6);
        c.addResistor(e, kGround, 100.0);
        c.addCapacitor(col, out, 0.12e-6);
        c.addResistor(out, kGround, 100e3); // VR13 volume control (wiper applied outside)
    }
};

// Snare drum trigger: Q54 enables the Q52/Q53 astable.
struct SnareTrigger
{
    int enabledSamples = 0;
    bool buttonHeld = false;
    bool running = false, phaseA = false;
    int phaseRemaining = 0;
};

// Small fast Gaussian noise source.
class GaussianNoise
{
public:
    double next()
    {
        if (hasSpare_)
        {
            hasSpare_ = false;
            return spare_;
        }
        double u, v, s;
        do
        {
            u = uniform() * 2.0 - 1.0;
            v = uniform() * 2.0 - 1.0;
            s = u * u + v * v;
        } while (s >= 1.0 || s == 0.0);
        const double f = std::sqrt(-2.0 * std::log(s) / s);
        spare_ = v * f;
        hasSpare_ = true;
        return u * f;
    }

private:
    double uniform()
    {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 7;
        state_ ^= state_ << 17;
        return static_cast<double>(state_ >> 11) * (1.0 / 9007199254740992.0);
    }
    uint64_t state_ = 0x9E3779B97F4A7C15ull;
    double spare_ = 0.0;
    bool hasSpare_ = false;
};

} // namespace

// ---------------------------------------------------------------------------

const char* buttonName(Button b)
{
    switch (b)
    {
        case Button::BassDrum: return "Bass Drum";
        case Button::Snare: return "Snare Drum";
        case Button::HiHat: return "Hi-Hat Cymbal";
        case Button::Cymbal: return "Cymbal";
        case Button::TomTom: return "Tom Tom";
        case Button::Bongo: return "Bongo";
        case Button::Blocks: return "Blocks";
        case Button::Clave: return "Clave";
        case Button::Count: break;
    }
    return "?";
}

const char* trimName(Trim t)
{
    switch (t)
    {
        case Trim::BassDrumLength: return "VR1 Bass Drum";
        case Trim::TomTomLength: return "VR2 Tom Tom";
        case Trim::BongoLength: return "VR3 Bongo";
        case Trim::BlocksLength: return "VR4 Block";
        case Trim::ClaveLength: return "VR5 Clave";
        case Trim::RimShotLength: return "VR6 Rim Shot";
        case Trim::HiDrumLength: return "VR7 Hi Drum";
        case Trim::LoDrumLength: return "VR8 Lo Drum";
        case Trim::SnareSpeed: return "VR9 Snare Drum Speed";
        case Trim::CymbalVolume: return "VR10 Cymbal Volume";
        case Trim::HiHatVolume: return "VR11 Hi-Hat Volume";
        case Trim::SnareNoiseVolume: return "VR12 Snare Noise Volume";
        case Trim::Count: break;
    }
    return "?";
}

double defaultTrim(Trim t)
{
    // Length trimmers: 1 = longest ring. See docs/DESIGN.md for how these were chosen.
    switch (t)
    {
        case Trim::BassDrumLength: return 1.0;
        case Trim::TomTomLength: return 0.5;
        case Trim::BongoLength: return 0.5;
        case Trim::BlocksLength: return 0.25;
        case Trim::ClaveLength: return 0.3;
        case Trim::RimShotLength: return 0.5;
        case Trim::HiDrumLength: return 0.5;
        case Trim::LoDrumLength: return 0.5;
        case Trim::SnareSpeed: return 0.5;
        case Trim::CymbalVolume: return 0.125;
        case Trim::HiHatVolume: return 0.9;
        case Trim::SnareNoiseVolume: return 0.7;
        case Trim::Count: break;
    }
    return 0.5;
}

struct Instrumentation::Impl
{
    double sampleRate = 48000.0;
    int sampleRateInt = 48000;
    std::array<Generator, kNumGenerators> generators;
    SnareNoise snareNoise;
    HiHat hiHat;
    Cymbal cymbal;
    Preamp preamp;
    SnareTrigger snare;
    GaussianNoise noise;
    ControlLines controls;
    bool prepared = false;

    int samples(double seconds) const { return std::max(1, static_cast<int>(std::lround(seconds * sampleRate))); }

    Generator& generator(KeyPoint k)
    {
        for (auto& g : generators)
            if (g.spec->key == k)
                return g;
        return generators[0];
    }

    void setStroke(bool on)
    {
        generator(KeyPoint::HiDrum).setSnareStroke(on);
        generator(KeyPoint::LoDrum).setSnareStroke(on);
        snareNoise.v.wake();
        snareNoise.v.c.setSwitch(snareNoise.q53, !on);
    }

    void tickSnareTrigger(double speedTrim)
    {
        auto& s = snare;
        const bool enabled = s.buttonHeld || s.enabledSamples > 0;
        if (s.enabledSamples > 0)
            --s.enabledSamples;

        if (!enabled)
        {
            if (s.running)
            {
                s.running = false;
                if (s.phaseA)
                    setStroke(false);
            }
            return;
        }
        if (!s.running || --s.phaseRemaining <= 0)
        {
            s.phaseA = !s.running || !s.phaseA;
            s.running = true;
            s.phaseRemaining = samples(s.phaseA ? snarePhaseASeconds(speedTrim) : kSnarePhaseBSeconds);
            setStroke(s.phaseA);
        }
    }
};

Instrumentation::Instrumentation() : impl_(std::make_unique<Impl>())
{
    for (int t = 0; t < kNumTrims; ++t)
        trims_[static_cast<size_t>(t)] = defaultTrim(static_cast<Trim>(t));
}

Instrumentation::~Instrumentation() = default;

void Instrumentation::prepare(double sampleRate)
{
    impl_ = std::make_unique<Impl>();
    auto& d = *impl_;
    d.sampleRate = sampleRate;
    d.sampleRateInt = static_cast<int>(sampleRate);

    for (int i = 0; i < kNumGenerators; ++i)
    {
        d.generators[static_cast<size_t>(i)].build(kGenerators[i], trims_[static_cast<size_t>(kGenerators[i].trim)]);
        d.generators[static_cast<size_t>(i)].v.finishPrepare(sampleRate);
    }
    d.snareNoise.build(trims_[static_cast<size_t>(Trim::SnareNoiseVolume)]);
    d.snareNoise.v.finishPrepare(sampleRate);
    d.hiHat.build(trims_[static_cast<size_t>(Trim::HiHatVolume)]);
    d.hiHat.v.finishPrepare(sampleRate);
    d.cymbal.build(trims_[static_cast<size_t>(Trim::CymbalVolume)]);
    d.cymbal.v.finishPrepare(sampleRate);
    d.preamp.build();
    d.preamp.c.prepare(sampleRate);
    d.prepared = true;
}

void Instrumentation::setControlLines(const ControlLines& lines)
{
    auto& d = *impl_;
    if (lines == d.controls)
        return;
    d.controls = lines;

    d.hiHat.v.wake();
    d.hiHat.v.c.setSwitch(d.hiHat.med, lines.hiHatMed);
    d.hiHat.v.c.setSwitch(d.hiHat.lng, lines.hiHatLong);
    d.hiHat.v.c.setSwitch(d.hiHat.choke, lines.hiHatChoke);
    d.hiHat.v.c.setSwitch(d.hiHat.dampenLink, lines.hiHatDampen);
    d.cymbal.v.wake();
    d.cymbal.v.c.setSwitch(d.cymbal.y, lines.cymbalY);
    d.cymbal.v.c.setSwitch(d.cymbal.z, lines.cymbalZ);
}

void Instrumentation::key(KeyPoint k)
{
    auto& d = *impl_;
    switch (k)
    {
        case KeyPoint::Snare:
            d.snare.enabledSamples = std::max(d.snare.enabledSamples, d.samples(kSnareKeySeconds));
            break;
        case KeyPoint::HiHat:
            d.hiHat.trigger(d.samples(kHiHatOneShotSeconds));
            break;
        case KeyPoint::Cymbal:
            d.cymbal.trigger(d.samples(kCymbalOneShotSeconds));
            break;
        case KeyPoint::LoDrumSoft:
            d.generator(KeyPoint::LoDrum).softKey(d.samples(kKeySeconds));
            break;
        case KeyPoint::Count:
            break;
        default:
            d.generator(k).key(d.samples(kKeySeconds));
            break;
    }
}

void Instrumentation::snareRoll(double seconds)
{
    auto& d = *impl_;
    d.snare.enabledSamples = std::max(d.snare.enabledSamples, d.samples(seconds));
}

void Instrumentation::cymbalAccent(double seconds)
{
    auto& d = *impl_;
    auto& v = d.cymbal.v;
    v.wake();
    v.c.setSwitch(d.cymbal.accent, true);
    v.timers.schedule(d.samples(seconds), d.cymbal.accent, false);
}

void Instrumentation::hiHatDampenPulse()
{
    auto& d = *impl_;
    if (!d.controls.hiHatDampen)
        return;
    auto& v = d.hiHat.v;
    v.wake();
    v.c.setSwitch(d.hiHat.q17, true);
    v.timers.schedule(d.samples(kQ17OnSeconds), d.hiHat.q17, false);
}

void Instrumentation::setButton(Button b, bool down)
{
    auto& d = *impl_;
    switch (b)
    {
        case Button::BassDrum: d.generator(KeyPoint::BassDrum).setButton(down); break;
        case Button::TomTom: d.generator(KeyPoint::TomTom).setButton(down); break;
        case Button::Bongo: d.generator(KeyPoint::Bongo).setButton(down); break;
        case Button::Blocks: d.generator(KeyPoint::Blocks).setButton(down); break;
        case Button::Clave: d.generator(KeyPoint::Clave).setButton(down); break;
        case Button::Snare: d.snare.buttonHeld = down; break;
        case Button::HiHat: d.hiHat.setButton(down); break;
        case Button::Cymbal: d.cymbal.setButton(down); break;
        case Button::Count: break;
    }
}

void Instrumentation::setTrim(Trim t, double position)
{
    position = std::clamp(position, 0.0, 1.0);
    trims_[static_cast<size_t>(t)] = position;
    auto& d = *impl_;
    if (!d.prepared)
        return;

    for (auto& g : d.generators)
        if (g.spec != nullptr && g.spec->trim == t)
        {
            g.v.retrim(g.v.c, g.emitterResistor, Generator::emitterOhms(*g.spec, position));
            return;
        }

    auto retrim = [&](Voice& v, int resistor) { v.retrim(v.audio, resistor, noiseVolumeOhms(position)); };
    switch (t)
    {
        case Trim::CymbalVolume: retrim(d.cymbal.v, d.cymbal.volume); break;
        case Trim::HiHatVolume: retrim(d.hiHat.v, d.hiHat.volume); break;
        case Trim::SnareNoiseVolume: retrim(d.snareNoise.v, d.snareNoise.volume); break;
        default: break; // VR9 is read every sample by the snare trigger
    }
}

uint32_t Instrumentation::awakeCircuits() const
{
    const auto& d = *impl_;
    uint32_t mask = 0;
    int bit = 0;
    for (const auto& g : d.generators)
        mask |= (g.v.awake ? 1u : 0u) << bit++;
    for (const Voice* v : { &d.snareNoise.v, &d.hiHat.v, &d.cymbal.v })
        mask |= (v->awake ? 1u : 0u) << bit++;
    return mask;
}

float Instrumentation::process()
{
    auto& d = *impl_;
    d.tickSnareTrigger(trims_[static_cast<size_t>(Trim::SnareSpeed)]);

    const double n = d.noise.next() * kNoiseRmsVolts;
    d.snareNoise.noise.set(d.snareNoise.v.audio, n);
    d.hiHat.noise.set(d.hiHat.v.audio, n);
    d.cymbal.noise.set(d.cymbal.v.audio, n);

    double bus = 0.0;
    for (auto& g : d.generators)
    {
        g.v.tick(d.sampleRateInt);
        bus += g.v.output();
    }
    for (Voice* v : { &d.snareNoise.v, &d.hiHat.v, &d.cymbal.v })
    {
        v->tick(d.sampleRateInt);
        bus += v->output();
    }

    d.preamp.c.setCurrent(d.preamp.input, bus / kBusOhms);
    d.preamp.c.step();
    return static_cast<float>(d.preamp.c.voltage(d.preamp.out));
}

} // namespace beatqueen::mrk1
