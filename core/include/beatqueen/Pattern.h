#pragma once

#include "beatqueen/Voices.h"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace beatqueen {

constexpr int kMaxSteps = 32;

// Which voices fire on a step: bit i set means VoiceId(i) fires. This mirrors
// a rhythm machine's diode matrix, where each counter state drives a row of
// diodes into the instrument trigger lines.
using StepMask = uint8_t;

constexpr StepMask voiceBit(VoiceId id) { return static_cast<StepMask>(1u << static_cast<int>(id)); }

struct Pattern
{
    std::string name;
    std::string category;
    int stepsPerBeat = 4; // 4 = sixteenth notes, 3 = triplets (e.g. waltz, shuffle)
    int length = 16;      // steps before the pattern repeats, <= kMaxSteps
    std::array<StepMask, kMaxSteps> steps {};
};

// Builds a pattern from one grid row per voice, 'x' = hit, anything else = rest:
//   makePattern("Name", "Category", 4, {{VoiceId::BassDrum, "x...x...x...x..."}})
// The pattern length is the length of the longest row.
Pattern makePattern(std::string name,
                    std::string category,
                    int stepsPerBeat,
                    std::initializer_list<std::pair<VoiceId, const char*>> rows);

const std::vector<Pattern>& presetPatterns();

} // namespace beatqueen
