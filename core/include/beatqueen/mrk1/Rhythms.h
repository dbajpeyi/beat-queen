#pragma once

// The MRK-1's 18 preset rhythms, expressed the way the machine plays them:
// as keying-point events on the 20 pulses of the clock cycle.
//
// The clock (Dwg 1) is a 5-stage ring multivibrator Q1-Q5 that goes round 4
// times per cycle, so a cycle has 20 pulses, indexed cycle * 5 + stage. One
// ring cycle spans 2 beats, so a clock cycle is 8 beats. Stage timing:
//   straight: stages at 0, 2, 3, 4, 6 sixteenths of the 8-sixteenth ring cycle
//   triplet:  stages at 0, 2, 3, 4, 5 triplet-eighths of the 6-triplet ring cycle
// The triplet contacts on eight rhythm switches change the stage timing, and
// the tempo regulator (Q64/Q65) keeps the beat length the same.

#include "beatqueen/mrk1/Instrumentation.h"

#include <array>
#include <string>
#include <vector>

namespace beatqueen::mrk1 {

constexpr int kStages = 5;
constexpr int kRingCycles = 4;
constexpr int kPulsesPerCycle = kStages * kRingCycles; // 20
constexpr double kBeatsPerRingCycle = 2.0;
constexpr double kBeatsPerCycle = kBeatsPerRingCycle * kRingCycles; // 8

// Beat position of pulse `index` within the 8-beat clock cycle.
double pulseBeat(int index, bool triplet);

enum class Category
{
    Latin,      // red buttons
    American,   // gold buttons
    Traditional // blue buttons
};

struct PulseEvents
{
    KeyMask keys = 0;
    bool snareRoll = false;    // snare trigger held until the next ring cycle starts
    bool cymbalAccent = false; // clock output (2) on "D" until the next pulse
};

struct Rhythm
{
    std::string name;
    Category category;
    bool triplet;
    ControlLines controls;
    std::array<PulseEvents, kPulsesPerCycle> pulses {};
};

constexpr int kNumRhythms = 18;

// In front-panel order: Latin, American, Traditional.
const std::array<Rhythm, kNumRhythms>& rhythms();

const char* categoryName(Category c);

// --- Building rhythms from grids ------------------------------------------

// Pseudo keying rows used only in rhythm grids.
enum class GridRow
{
    Key,          // a KeyPoint
    SnareRoll,    // roll start
    CymbalAccent, // accent window start
};

struct GridLine
{
    GridRow row;
    KeyPoint key;
    const char* grid; // 'x' = event, anything else = rest
};

GridLine row(KeyPoint k, const char* grid);
GridLine rollRow(const char* grid);
GridLine accentRow(const char* grid);

// Places a pattern drawn on an even grid onto the clock pulses. Each grid
// position spans `slotsPerPosition` clock slots (sixteenths when straight,
// triplet-eighths when triplet); a pattern shorter than the cycle repeats.
// Events that do not land on a clock pulse are reported in `errors`.
Rhythm makeRhythm(std::string name,
                  Category category,
                  bool triplet,
                  ControlLines controls,
                  int slotsPerPosition,
                  std::initializer_list<GridLine> lines,
                  std::vector<std::string>* errors = nullptr);

// Problems found while building the preset bank (empty when all is well).
const std::vector<std::string>& rhythmBankErrors();

} // namespace beatqueen::mrk1
