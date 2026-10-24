#include "PluginProcessor.h"

#include "PluginEditor.h"

namespace {

using beatqueen::mrk1::KeyPoint;

// General MIDI drum notes onto the MRK-1 keying points.
std::optional<KeyPoint> keyForNote(int note)
{
    switch (note)
    {
        case 35: case 36: return KeyPoint::BassDrum;
        case 37: return KeyPoint::RimShot;
        case 38: case 40: return KeyPoint::Snare;
        case 42: case 44: case 46: return KeyPoint::HiHat;
        case 49: case 51: case 52: case 55: case 57: case 59: return KeyPoint::Cymbal;
        case 41: case 43: return KeyPoint::TomTom;
        case 45: case 47: return KeyPoint::LoDrum;
        case 48: case 50: return KeyPoint::HiDrum;
        case 60: case 61: return KeyPoint::Bongo;
        case 75: return KeyPoint::Clave;
        case 76: case 77: return KeyPoint::Blocks;
        default: return std::nullopt;
    }
}

} // namespace

BeatQueenProcessor::BeatQueenProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "BeatQueen", createLayout())
{
    for (int r = 0; r < beatqueen::mrk1::kNumRhythms; ++r)
        rhythms_[static_cast<size_t>(r)] = parameters.getRawParameterValue(rhythmParamId(r));
    for (int t = 0; t < beatqueen::mrk1::kNumTrims; ++t)
        trims_[static_cast<size_t>(t)] = parameters.getRawParameterValue(trimParamId(t));
    tempo_ = parameters.getRawParameterValue("tempo");
    run_ = parameters.getRawParameterValue("run");
    hostSync_ = parameters.getRawParameterValue("hostSync");
    volume_ = parameters.getRawParameterValue("volume");
}

juce::AudioProcessorValueTreeState::ParameterLayout BeatQueenProcessor::createLayout()
{
    using namespace beatqueen::mrk1;
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    for (int r = 0; r < kNumRhythms; ++r)
        layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { rhythmParamId(r), 1 },
                                                              rhythms()[static_cast<size_t>(r)].name, r == 0));

    // The hardware's range could not be fixed from the manual (it depends on the
    // tempo regulator's B+); this covers it with room to spare.
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "tempo", 1 }, "Speed",
                                                           juce::NormalisableRange<float>(40.0f, 240.0f, 0.1f), 120.0f,
                                                           juce::AudioParameterFloatAttributes().withLabel("BPM")));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "run", 1 }, "Start", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "hostSync", 1 }, "Host Sync", true));
    const auto percent = juce::AudioParameterFloatAttributes().withStringFromValueFunction(
        [](float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; });
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "volume", 1 }, "Volume",
                                                           juce::NormalisableRange<float>(0.0f, 1.0f), 0.8f, percent));

    for (int t = 0; t < kNumTrims; ++t)
        layout.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID { trimParamId(t), 1 }, trimName(static_cast<Trim>(t)),
            juce::NormalisableRange<float>(0.0f, 1.0f), static_cast<float>(defaultTrim(static_cast<Trim>(t))), percent));
    return layout;
}

void BeatQueenProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    for (int t = 0; t < beatqueen::mrk1::kNumTrims; ++t)
    {
        appliedTrims_[static_cast<size_t>(t)] = trims_[static_cast<size_t>(t)]->load();
        machine_.setTrim(static_cast<beatqueen::mrk1::Trim>(t), appliedTrims_[static_cast<size_t>(t)]);
    }
    machine_.prepare(sampleRate);
    mono_.assign(static_cast<size_t>(samplesPerBlock), 0.0f);
}

bool BeatQueenProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void BeatQueenProcessor::applyParameters()
{
    uint32_t selection = 0;
    for (int r = 0; r < beatqueen::mrk1::kNumRhythms; ++r)
        if (rhythms_[static_cast<size_t>(r)]->load() > 0.5f)
            selection |= 1u << r;
    machine_.setRhythms(selection);
    machine_.setTempo(tempo_->load());
    machine_.setRunning(run_->load() > 0.5f);
    machine_.setVolume(volume_->load());

    // A trimmer re-solves one circuit's operating point: apply at most one per block.
    for (int t = 0; t < beatqueen::mrk1::kNumTrims; ++t)
    {
        const float value = trims_[static_cast<size_t>(t)]->load();
        if (std::abs(value - appliedTrims_[static_cast<size_t>(t)]) > 1e-4f)
        {
            appliedTrims_[static_cast<size_t>(t)] = value;
            machine_.setTrim(static_cast<beatqueen::mrk1::Trim>(t), value);
            break;
        }
    }
}

void BeatQueenProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    if (static_cast<int>(mono_.size()) < numSamples)
        mono_.resize(static_cast<size_t>(numSamples)); // only if the host exceeds its announced block size

    applyParameters();

    // Follow the host transport when it is playing and sync is on; otherwise
    // the internal clock runs from the Start switch. The standalone app has no
    // play head, so it always uses the internal clock.
    std::optional<beatqueen::HostTransport> host;
    if (hostSync_->load() > 0.5f)
        if (auto* hostPlayHead = getPlayHead())
            if (const auto pos = hostPlayHead->getPosition(); pos && pos->getIsPlaying())
                if (const auto ppq = pos->getPpqPosition(); ppq)
                    host = beatqueen::HostTransport { true, *ppq, pos->getBpm().orFallback(120.0) };

    const double ppqPerSample = host ? host->bpm / (60.0 * getSampleRate()) : 0.0;

    // Render in segments split at MIDI note-ons so played notes land on the exact sample.
    int rendered = 0;
    auto renderUpTo = [&](int end) {
        if (end <= rendered)
            return;
        if (host)
        {
            auto segment = *host;
            segment.ppqPosition += rendered * ppqPerSample;
            machine_.render(mono_.data() + rendered, end - rendered, &segment);
        }
        else
        {
            machine_.render(mono_.data() + rendered, end - rendered, nullptr);
        }
        rendered = end;
    };

    for (const auto metadata : midi)
    {
        const auto msg = metadata.getMessage();
        if (!msg.isNoteOn())
            continue;
        if (const auto key = keyForNote(msg.getNoteNumber()))
        {
            renderUpTo(juce::jlimit(0, numSamples, metadata.samplePosition));
            machine_.key(*key);
        }
    }
    renderUpTo(numSamples);

    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        buffer.copyFrom(ch, 0, mono_.data(), numSamples);
}

juce::AudioProcessorEditor* BeatQueenProcessor::createEditor()
{
    return new BeatQueenEditor(*this);
}

void BeatQueenProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (const auto xml = parameters.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void BeatQueenProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (const auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(parameters.state.getType()))
            parameters.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BeatQueenProcessor();
}
