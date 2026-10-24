// Offline renderer: writes each voice and each preset to WAV files so the
// models can be auditioned and compared against recordings of a real MRK-1.
//
//   beatqueen_render <output-dir> [sample-rate]

#include "beatqueen/DrumMachine.h"
#include "beatqueen/Pattern.h"
#include "beatqueen/Voices.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

using namespace beatqueen;

bool writeWav(const std::string& path, const std::vector<float>& samples, int sampleRate)
{
    FILE* f = std::fopen(path.c_str(), "wb");
    if (f == nullptr)
        return false;

    auto u32 = [f](uint32_t v) { std::fwrite(&v, 4, 1, f); };
    auto u16 = [f](uint16_t v) { std::fwrite(&v, 2, 1, f); };

    const auto dataBytes = static_cast<uint32_t>(samples.size() * 2);
    std::fwrite("RIFF", 1, 4, f);
    u32(36 + dataBytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16);
    u16(1); // PCM
    u16(1); // mono
    u32(static_cast<uint32_t>(sampleRate));
    u32(static_cast<uint32_t>(sampleRate * 2));
    u16(2);
    u16(16);
    std::fwrite("data", 1, 4, f);
    u32(dataBytes);
    for (float s : samples)
    {
        const auto v = static_cast<int16_t>(std::lround(std::clamp(s, -1.0f, 1.0f) * 32767.0f));
        std::fwrite(&v, 2, 1, f);
    }
    return std::fclose(f) == 0;
}

std::string slug(std::string s)
{
    for (auto& c : s)
        c = std::isalnum(static_cast<unsigned char>(c)) ? static_cast<char>(std::tolower(c)) : '_';
    return s;
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: %s <output-dir> [sample-rate]\n", argv[0]);
        return 2;
    }
    const std::string dir = argv[1];
    const int sampleRate = argc > 2 ? std::atoi(argv[2]) : 48000;

    for (int i = 0; i < kNumVoices; ++i)
    {
        const auto id = static_cast<VoiceId>(i);
        auto voice = makeVoice(id);
        voice->prepare(sampleRate);
        voice->trigger(1.0f);

        std::vector<float> out(static_cast<size_t>(sampleRate * 2));
        for (auto& s : out)
            s = 0.8f * voice->process();

        const auto path = dir + "/voice_" + slug(voiceName(id)) + ".wav";
        if (!writeWav(path, out, sampleRate))
            return 1;
        std::printf("%s\n", path.c_str());
    }

    const auto& bank = presetPatterns();
    for (size_t p = 0; p < bank.size(); ++p)
    {
        DrumMachine dm;
        dm.prepare(sampleRate);
        dm.selectPattern(static_cast<int>(p));
        dm.setRunning(true);

        std::vector<float> out(static_cast<size_t>(sampleRate * 8));
        dm.render(out.data(), static_cast<int>(out.size()), nullptr);

        const auto path = dir + "/rhythm_" + slug(bank[p].name) + ".wav";
        if (!writeWav(path, out, sampleRate))
            return 1;
        std::printf("%s\n", path.c_str());
    }
    return 0;
}
