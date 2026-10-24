#include "beatqueen/mrk1/Rhythms.h"

#include <cstring>

// Sources (MRK-1 service manual):
//  - Rhythm Pattern Diagrams, pp. 10-11: the instruments on each beat.
//  - Drawing No. 3, Rhythm Selector Switches: which clock pulses key which
//    instrument, plus the control functions (Y, Z, A, med, long, (17), TRIP).
//  - Description, pp. 3-4: when each clock logic gate fires.
//
// Decoding the selector wiring against the clock confirms the diagrams almost
// everywhere and fixes each rhythm's grid (e.g. March/Polka is sixteenths
// repeating every half cycle). Where the wiring clearly adds a hit that the
// diagram leaves out, the hit is included and noted below.

namespace beatqueen::mrk1 {

namespace {

constexpr int kStraightSlots[kStages] = { 0, 2, 3, 4, 6 }; // of 8 per ring cycle
constexpr int kTripletSlots[kStages] = { 0, 2, 3, 4, 5 };  // of 6 per ring cycle

int slotsPerRingCycle(bool triplet) { return triplet ? 6 : 8; }

std::vector<std::string>& bankErrors()
{
    static std::vector<std::string> errors;
    return errors;
}

ControlLines controls(bool hiHatMed, bool hiHatLong, bool hiHatChoke, bool hiHatDampen, bool cymbalY, bool cymbalZ)
{
    ControlLines c;
    c.hiHatMed = hiHatMed;
    c.hiHatLong = hiHatLong;
    c.hiHatChoke = hiHatChoke;
    c.hiHatDampen = hiHatDampen;
    c.cymbalY = cymbalY;
    c.cymbalZ = cymbalZ;
    return c;
}

const ControlLines kNone {};
const ControlLines kChokedCymbal = controls(false, false, false, false, true, true); // Y + Z
const ControlLines kMaracas = controls(false, false, false, false, true, false);      // Y
const ControlLines kLongCymbal = controls(false, false, false, false, false, true);   // Z
const ControlLines kMedHiHat = controls(true, false, false, false, false, false);     // med -> ground
const ControlLines kLongHiHat = controls(false, true, false, false, false, false);    // long -> ground
const ControlLines kChokedHiHat = controls(false, false, true, false, false, false);  // +6 V -> A
const ControlLines kMedChokedHiHat = controls(true, false, true, false, false, false);
const ControlLines kSwingHiHat = controls(true, false, false, true, false, false); // med, (17) -> C

using K = KeyPoint;

std::array<Rhythm, kNumRhythms> buildBank()
{
    auto& errs = bankErrors();
    constexpr bool S = false, T = true; // straight / triplet
    const auto latin = Category::Latin, american = Category::American, traditional = Category::Traditional;

    // clang-format off
    return {
        // ----- Latin American (red) -----
        // Clave also keyed with blocks/lo drum (Dwg 3: 23,29,18 -> LoD, BL, CL).
        makeRhythm("Bossa Nova", latin, S, kChokedCymbal, 2, {
            row(K::BassDrum, "x...x..xx...x..x"),
            row(K::Cymbal,   "xxxxxxxxxxxxxxxx"),
            row(K::HiHat,    "..x...x...x...x."),
            row(K::Blocks,   "x..x..x...x..x.."),
            row(K::LoDrum,   "x..x..x...x..x.."),
            row(K::Clave,    "x..x..x...x..x.."),
        }, &errs),
        // Bongo on position 11 and blocks off it, per Dwg 3 (Bon 28,20; BL 23,29,18).
        makeRhythm("Samba", latin, S, kNone, 2, {
            row(K::BassDrum, "x...x...x...x..."),
            row(K::Cymbal,   "xxxxxxxxxxxxxxxx"),
            accentRow(       "x...x...x...x..."),
            row(K::HiHat,    "x..xx..xx..xx..x"),
            row(K::Blocks,   "x..x..x...x..x.."),
            row(K::Bongo,    "x..x....x..x...."),
            row(K::TomTom,   "....x.......x..."),
        }, &errs),
        makeRhythm("Bolero", latin, S, kMaracas, 1, {
            row(K::BassDrum,   "x.......x...x..."),
            row(K::Cymbal,     "x.xxx.x.x.xxx.x."),
            row(K::LoDrumSoft, "..x.x.x...x...x."),
            row(K::HiDrum,     "x.......x......."),
            row(K::Bongo,      "............x..."),
        }, &errs),
        // Maracas also on position 24 (Dwg 3: CY 6,7,9,10).
        makeRhythm("Rumba", latin, S, kMaracas, 1, {
            row(K::BassDrum, "x.......x...x...x.......x...x..."),
            row(K::Cymbal,   "x.xxx.x.x.x.x.x.x.xxx.x.x.x.x.x."),
            row(K::Clave,    "x.....x.....x.......x...x......."),
            row(K::HiDrum,   "..x.x...x.x.......x.x...x.x....."),
            row(K::LoDrum,   "...xx.x.....x.x....xx.x.....x.x."),
            row(K::Bongo,    "........x.x.............x.x....."),
            row(K::TomTom,   "............x...............x..."),
        }, &errs),
        // Cymbal also on position 6 (Dwg 3: CY 6,7,9,10).
        makeRhythm("Cha Cha", latin, S, kNone, 2, {
            row(K::BassDrum, "x...x.x."),
            row(K::Cymbal,   "xxxxxxxx"),
            row(K::HiHat,    "x.x.x.x."),
            row(K::Blocks,   "x.x.x.xx"),
            row(K::Clave,    "x.x.x.xx"),
            row(K::Bongo,    "..x....."),
            row(K::HiDrum,   "x.x.x.x."),
            row(K::TomTom,   "......xx"),
        }, &errs),
        makeRhythm("Mambo", latin, S, kNone, 2, {
            row(K::BassDrum, "....x.x.....x.x."),
            row(K::Cymbal,   "xxxxxxxxxxxxxxxx"),
            accentRow(       "x...x...x...x..."),
            row(K::Blocks,   "x.xxxx.xx.xxx.xx"),
            row(K::Clave,    "x.xxxx.xx.xxx.xx"),
            row(K::TomTom,   "x.......x......."),
            row(K::HiDrum,   ".xx......xx....."),
            row(K::LoDrum,   "...x.x.....x.x.."),
            row(K::Bongo,    "......xx......xx"),
        }, &errs),
        makeRhythm("Tango", latin, S, kNone, 2, {
            row(K::BassDrum, "x.x.x.xx"),
            row(K::Snare,    "x.x.x.xx"),
            rollRow(         ".......x"),
            row(K::HiHat,    "...x...."),
        }, &errs),
        makeRhythm("Paso Doble", latin, T, kNone, 1, {
            row(K::BassDrum, "x...x...x..."),
            row(K::HiHat,    "x..xx...x..."),
            row(K::LoDrum,   "x..xx...x..."),
            row(K::Snare,    "..x...x...x."),
            row(K::Cymbal,   "..x...x...x."),
        }, &errs),

        // ----- American (gold) -----
        // Cymbal also with the snare on beat 2 (Dwg 3: CY 27).
        makeRhythm("Slow Fox", american, T, kLongHiHat, 2, {
            row(K::BassDrum, "x..x.."),
            row(K::Snare,    "...x.."),
            row(K::Cymbal,   "...x.."),
            row(K::HiHat,    "x....x"),
        }, &errs),
        makeRhythm("Slow Rock", american, T, kChokedCymbal, 2, {
            row(K::BassDrum, "x..x.."),
            row(K::Snare,    "...x.."),
            row(K::Cymbal,   "xxxxxx"),
        }, &errs),
        makeRhythm("Swing", american, T, kSwingHiHat, 1, {
            row(K::BassDrum, "x..x..x..x..x..x..x..x.x"),
            row(K::Snare,    "...x.....x.x...x....xx.x"),
            row(K::Cymbal,   "x..x..x..x..x..x..x.xx.."),
            row(K::HiHat,    "x....xx....xx....xx....x"),
        }, &errs),
        makeRhythm("Dixie", american, T, kMedHiHat, 1, {
            row(K::BassDrum, "x.....x.....x.....x....."),
            row(K::Snare,    "x..x..x..x..x..x..x.xx.."),
            row(K::HiHat,    "...x.....x.....x.....x.."),
            row(K::RimShot,  "...x.....x.....x.....x.."),
        }, &errs),
        // Snare also on position 5 (Dwg 3: SD 8,20).
        makeRhythm("Shuffle", american, T, kMedHiHat, 1, {
            row(K::BassDrum, "x.....x....."),
            row(K::Snare,    "...x.x...x.."),
            row(K::Cymbal,   "x.xx.xx.xx.x"),
            row(K::HiHat,    "...x.....x.."),
        }, &errs),
        makeRhythm("Go Go", american, S, kMedChokedHiHat, 2, {
            row(K::BassDrum, "x..xx...x..xx..."),
            row(K::Snare,    "..x...x...x...xx"),
            row(K::HiHat,    "xxxxxxxxxxxxxxxx"),
        }, &errs),
        makeRhythm("Discotheque", american, S, kChokedHiHat, 2, {
            row(K::BassDrum, "x.x.x.x."),
            row(K::Snare,    "..x...x."),
            row(K::Cymbal,   "..x...x."),
            row(K::HiHat,    "xxxxxxxx"),
            row(K::LoDrum,   "...x...x"),
        }, &errs),

        // ----- Traditional (blue) -----
        makeRhythm("March Polka", traditional, S, kNone, 1, {
            row(K::BassDrum, "x...x...x...x..."),
            row(K::HiHat,    "x...x...x...x..."),
            row(K::Snare,    "x.x.x.x.x.xxx.x."),
            rollRow(         "..............x."),
        }, &errs),
        makeRhythm("Western", traditional, T, kChokedCymbal, 2, {
            row(K::BassDrum, "x....."),
            row(K::Cymbal,   "x.xx.x"),
            row(K::Bongo,    "..x..x"),
            row(K::RimShot,  "..x..x"),
            row(K::Blocks,   "...x.."),
            row(K::Clave,    "...x.."),
        }, &errs),
        makeRhythm("Waltz", traditional, T, kLongCymbal, 4, {
            row(K::BassDrum, "x.."),
            row(K::Snare,    ".xx"),
            row(K::Cymbal,   "..x"),
        }, &errs),
    };
    // clang-format on
}

} // namespace

double pulseBeat(int index, bool triplet)
{
    const int ring = index / kStages, stage = index % kStages;
    const double slot = triplet ? kTripletSlots[stage] : kStraightSlots[stage];
    return ring * kBeatsPerRingCycle + slot * kBeatsPerRingCycle / slotsPerRingCycle(triplet);
}

const char* categoryName(Category c)
{
    switch (c)
    {
        case Category::Latin: return "Latin American";
        case Category::American: return "American";
        case Category::Traditional: return "Traditional";
    }
    return "?";
}

GridLine row(KeyPoint k, const char* grid) { return { GridRow::Key, k, grid }; }
GridLine rollRow(const char* grid) { return { GridRow::SnareRoll, KeyPoint::Snare, grid }; }
GridLine accentRow(const char* grid) { return { GridRow::CymbalAccent, KeyPoint::Cymbal, grid }; }

Rhythm makeRhythm(std::string name,
                  Category category,
                  bool triplet,
                  ControlLines controlLines,
                  int slotsPerPosition,
                  std::initializer_list<GridLine> lines,
                  std::vector<std::string>* errors)
{
    Rhythm r { std::move(name), category, triplet, controlLines, {} };
    const int ringSlots = slotsPerRingCycle(triplet);
    const int cycleSlots = ringSlots * kRingCycles;
    const int* stageSlots = triplet ? kTripletSlots : kStraightSlots;

    auto fail = [&](const std::string& what) {
        if (errors != nullptr)
            errors->push_back(r.name + ": " + what);
    };

    for (const auto& line : lines)
    {
        const int length = static_cast<int>(std::strlen(line.grid));
        const int span = length * slotsPerPosition;
        if (span <= 0 || cycleSlots % span != 0)
        {
            fail("grid of " + std::to_string(length) + " positions does not divide the clock cycle");
            continue;
        }
        for (int rep = 0; rep < cycleSlots / span; ++rep)
            for (int pos = 0; pos < length; ++pos)
            {
                if (line.grid[pos] != 'x')
                    continue;
                const int slot = rep * span + pos * slotsPerPosition;
                const int ring = slot / ringSlots, within = slot % ringSlots;
                int stage = -1;
                for (int s = 0; s < kStages; ++s)
                    if (stageSlots[s] == within)
                        stage = s;
                if (stage < 0)
                {
                    fail("position " + std::to_string(pos) + " falls between clock pulses");
                    continue;
                }
                auto& ev = r.pulses[static_cast<size_t>(ring * kStages + stage)];
                switch (line.row)
                {
                    case GridRow::Key: ev.keys |= keyBit(line.key); break;
                    case GridRow::SnareRoll: ev.snareRoll = true; break;
                    case GridRow::CymbalAccent: ev.cymbalAccent = true; break;
                }
            }
    }
    return r;
}

const std::array<Rhythm, kNumRhythms>& rhythms()
{
    static const std::array<Rhythm, kNumRhythms> bank = buildBank();
    return bank;
}

const std::vector<std::string>& rhythmBankErrors()
{
    rhythms();
    return bankErrors();
}

} // namespace beatqueen::mrk1
