#pragma once

// The MRK-1 instrumentation board (service manual Drawing No. 4), simulated
// at component level: eight twin-T "ringing oscillator" generators, three
// gated-noise voices, the snare drum trigger multivibrator and the audio
// output preamp.

#include "beatqueen/circuit/Circuit.h"

#include <array>
#include <cstdint>
#include <memory>

namespace beatqueen::mrk1 {

// Boxed keying points on Dwg 4 that the rhythm selector switches drive.
enum class KeyPoint : uint8_t
{
    BassDrum,   // BD
    TomTom,     // TT
    Bongo,      // Bon
    Blocks,     // BL
    Clave,      // CL
    RimShot,    // RIM
    HiDrum,     // HiD
    LoDrum,     // LoD
    LoDrumSoft, // SOFT (Bolero only)
    Snare,      // SD / X: one stroke of the snare trigger
    HiHat,      // HH
    Cymbal,     // CY
    Count
};
constexpr int kNumKeyPoints = static_cast<int>(KeyPoint::Count);

using KeyMask = uint16_t;
constexpr KeyMask keyBit(KeyPoint k) { return static_cast<KeyMask>(1u << static_cast<int>(k)); }

// Static control functions applied by the rhythm selector switches (Dwg 3).
struct ControlLines
{
    bool hiHatMed = false;    // "med" keying point grounded: adds 0.068 uF to the hi-hat envelope
    bool hiHatLong = false;   // "long" grounded: adds 0.22 uF
    bool hiHatChoke = false;  // +6 V on "A": diode clamp drains the envelope to ~1/3
    bool hiHatDampen = false; // clock output (17) on "C" (Swing)
    bool cymbalY = false;     // Y on "B": diode clamp to +6 V
    bool cymbalZ = false;     // Z on "B": adds 0.1 uF

    bool operator==(const ControlLines& o) const
    {
        return hiHatMed == o.hiHatMed && hiHatLong == o.hiHatLong && hiHatChoke == o.hiHatChoke
               && hiHatDampen == o.hiHatDampen && cymbalY == o.cymbalY && cymbalZ == o.cymbalZ;
    }
    bool operator!=(const ControlLines& o) const { return !(*this == o); }
};

// The eight black voice push buttons, in front-panel order.
enum class Button : uint8_t
{
    BassDrum,
    Snare,
    HiHat,
    Cymbal,
    TomTom,
    Bongo,
    Blocks,
    Clave,
    Count
};
constexpr int kNumButtons = static_cast<int>(Button::Count);

// Service trimmers VR1-VR12. Positions are 0..1.
enum class Trim : uint8_t
{
    BassDrumLength,   // VR1
    TomTomLength,     // VR2
    BongoLength,      // VR3
    BlocksLength,     // VR4
    ClaveLength,      // VR5
    RimShotLength,    // VR6
    HiDrumLength,     // VR7
    LoDrumLength,     // VR8
    SnareSpeed,       // VR9
    CymbalVolume,     // VR10
    HiHatVolume,      // VR11
    SnareNoiseVolume, // VR12
    Count
};
constexpr int kNumTrims = static_cast<int>(Trim::Count);

const char* buttonName(Button b);
const char* trimName(Trim t);
double defaultTrim(Trim t);

class Instrumentation
{
public:
    Instrumentation();
    ~Instrumentation();

    // Builds and solves every circuit. Not real-time safe.
    void prepare(double sampleRate);

    // --- Real-time controls (audio thread) -------------------------------
    void setControlLines(const ControlLines& lines);

    // A clock pulse arriving at a keying point.
    void key(KeyPoint k);

    // Holds the snare trigger enabled (a roll) for the given time.
    void snareRoll(double seconds);

    // Clock output (2) on the cymbal "D" keying point (Samba, Mambo).
    void cymbalAccent(double seconds);

    // Clock output (10) firing the hi-hat dampen transistor Q17.
    void hiHatDampenPulse();

    void setButton(Button b, bool down);

    // Moving a trimmer re-solves that circuit's operating point.
    void setTrim(Trim t, double position);
    double trim(Trim t) const { return trims_[static_cast<size_t>(t)]; }

    // Output: the voltage at the output plug with the volume control (VR13) full up.
    float process();

    // Bit i set if circuit i is being simulated (idle circuits sleep): generators
    // in KeyPoint order (bass drum .. lo drum), then snare noise, hi-hat, cymbal.
    uint32_t awakeCircuits() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::array<double, kNumTrims> trims_ {};
};

} // namespace beatqueen::mrk1
