// Offline renderer: writes every instrument and every preset rhythm to WAV
// files, for auditioning the circuit models and comparing them with
// recordings of a real MRK-1.
//
//   beatqueen_render <output-dir> [sample-rate] [tempo-bpm]

#include "beatqueen/DrumMachine.h"
#include "beatqueen/mrk1/Instrumentation.h"
#include "beatqueen/mrk1/Rhythms.h"

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

const char* keyName(mrk1::KeyPoint k)
{
    using K = mrk1::KeyPoint;
    switch (k)
    {
        case K::BassDrum: return "bass_drum";
        case K::TomTom: return "tom_tom";
        case K::Bongo: return "bongo";
        case K::Blocks: return "blocks";
        case K::Clave: return "clave";
        case K::RimShot: return "rim_shot";
        case K::HiDrum: return "hi_drum";
        case K::LoDrum: return "lo_drum";
        case K::LoDrumSoft: return "lo_drum_soft";
        case K::Snare: return "snare_drum";
        case K::HiHat: return "hi_hat";
        case K::Cymbal: return "cymbal";
        case K::Count: break;
    }
    return "unknown";
}

} // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
    {
        std::fprintf(stderr, "usage: %s <output-dir> [sample-rate] [tempo-bpm]\n", argv[0]);
        return 2;
    }
    const std::string dir = argv[1];
    const int sampleRate = argc > 2 ? std::atoi(argv[2]) : 48000;
    const double tempo = argc > 3 ? std::atof(argv[3]) : 120.0;
    const float scale = 1.0f / DrumMachine::kFullScaleVolts;

    mrk1::Instrumentation inst;
    inst.prepare(sampleRate);
    for (int k = 0; k < mrk1::kNumKeyPoints; ++k)
    {
        for (int i = 0; i < sampleRate / 2; ++i)
            inst.process();
        std::vector<float> out(static_cast<size_t>(sampleRate * 2));
        inst.key(static_cast<mrk1::KeyPoint>(k));
        for (auto& s : out)
            s = scale * inst.process();

        const auto path = dir + "/instrument_" + keyName(static_cast<mrk1::KeyPoint>(k)) + ".wav";
        if (!writeWav(path, out, sampleRate))
            return 1;
        std::printf("%s\n", path.c_str());
    }

    const auto& bank = mrk1::rhythms();
    for (size_t r = 0; r < bank.size(); ++r)
    {
        DrumMachine dm;
        dm.prepare(sampleRate);
        dm.setVolume(1.0f);
        dm.setTempo(tempo);
        dm.setRhythms(1u << r);
        dm.setRunning(true);

        // Two full clock cycles (16 beats).
        std::vector<float> out(static_cast<size_t>(16.0 * 60.0 / tempo * sampleRate));
        dm.render(out.data(), static_cast<int>(out.size()), nullptr);

        const auto path = dir + "/rhythm_" + std::to_string(r + 1) + "_" + slug(bank[r].name) + ".wav";
        if (!writeWav(path, out, sampleRate))
            return 1;
        std::printf("%s\n", path.c_str());
    }
    return 0;
}
