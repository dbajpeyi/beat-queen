#include "beatqueen/Pattern.h"

#include <algorithm>
#include <cstring>

namespace beatqueen {

Pattern makePattern(std::string name,
                    std::string category,
                    int stepsPerBeat,
                    std::initializer_list<std::pair<VoiceId, const char*>> rows)
{
    Pattern p;
    p.name = std::move(name);
    p.category = std::move(category);
    p.stepsPerBeat = stepsPerBeat;
    p.length = 0;

    for (const auto& [voice, grid] : rows)
    {
        const int len = std::min(static_cast<int>(std::strlen(grid)), kMaxSteps);
        p.length = std::max(p.length, len);
        for (int i = 0; i < len; ++i)
            if (grid[i] == 'x' || grid[i] == 'X')
                p.steps[static_cast<size_t>(i)] |= voiceBit(voice);
    }

    p.length = std::max(p.length, 1);
    return p;
}

const std::vector<Pattern>& presetPatterns()
{
    // The MRK-1's 18 preset rhythms (Latin / American / Traditional) belong
    // here, transcribed from the rhythm-matrix diagram in the service manual.
    // Until then this bank holds a single test groove that is NOT an MRK-1
    // rhythm.
    static const std::vector<Pattern> bank = {
        makePattern("Test Beat", "Placeholder", 4,
                    {
                        { VoiceId::BassDrum, "x.......x.x....." },
                        { VoiceId::Snare,    "....x.......x..." },
                        { VoiceId::HiHat,    "x.x.x.x.x.x.x.x." },
                        { VoiceId::Clave,    "x..x..x...x.x..." },
                    }),
    };
    return bank;
}

} // namespace beatqueen
