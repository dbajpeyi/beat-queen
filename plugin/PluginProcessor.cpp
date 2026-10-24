#include "PluginProcessor.h"

#include "PluginEditor.h"

#include "beatqueen/Pattern.h"

namespace {

using beatqueen::VoiceId;

// General MIDI drum map onto the eight voices, so a DAW or pad controller can play them.
std::optional<VoiceId> voiceForNote(int note)
{
    switch (note)
    {
        case 35: case 36: return VoiceId::BassDrum;
        case 37: case 38: case 40: return VoiceId::Snare;
        case 42: case 44: case 46: return VoiceId::HiHat;
        case 49: case 51: case 52: case 55: case 57: case 59: return VoiceId::Cymbal;
        case 41: case 43: case 45: case 47: case 48: case 50: return VoiceId::Tom;
        case 60: case 61: return VoiceId::Bongo;
        case 76: case 77: return VoiceId::Blocks;
        case 75: return VoiceId::Clave;
        default: return std::nullopt;
    }
}

} // namespace

BeatQueenProcessor::BeatQueenProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      parameters(*this, nullptr, "BeatQueen", createLayout())
{
    rhythm_ = dynamic_cast<juce::AudioParameterChoice*>(parameters.getParameter("rhythm"));
    tempo_ = parameters.getRawParameterValue("tempo");
    run_ = parameters.getRawParameterValue("run");
    hostSync_ = parameters.getRawParameterValue("hostSync");
    master_ = parameters.getRawParameterValue("master");
    for (int i = 0; i < beatqueen::kNumVoices; ++i)
        levels_[static_cast<size_t>(i)] = parameters.getRawParameterValue(levelParamId(i));
}

juce::AudioProcessorValueTreeState::ParameterLayout BeatQueenProcessor::createLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    juce::StringArray rhythmNames;
    for (const auto& p : beatqueen::presetPatterns())
        rhythmNames.add(p.name);

    layout.add(std::make_unique<juce::AudioParameterChoice>(juce::ParameterID { "rhythm", 1 }, "Rhythm", rhythmNames, 0));
    // TODO(manual): match the tempo range of the MRK-1 clock oscillator.
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "tempo", 1 }, "Tempo",
                                                           juce::NormalisableRange<float>(40.0f, 240.0f, 0.1f), 120.0f,
                                                           juce::AudioParameterFloatAttributes().withLabel("BPM")));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "run", 1 }, "Start", false));
    layout.add(std::make_unique<juce::AudioParameterBool>(juce::ParameterID { "hostSync", 1 }, "Host Sync", true));
    layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { "master", 1 }, "Volume",
                                                           juce::NormalisableRange<float>(0.0f, 1.0f), 0.8f,
                                                           juce::AudioParameterFloatAttributes().withStringFromValueFunction(
                                                               [](float v, int) { return juce::String(juce::roundToInt(v * 100.0f)) + "%"; })));

    for (int i = 0; i < beatqueen::kNumVoices; ++i)
        layout.add(std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { levelParamId(i), 1 },
                                                               beatqueen::voiceName(static_cast<VoiceId>(i)),
                                                               juce::NormalisableRange<float>(0.0f, 1.0f), 0.8f));
    return layout;
}

void BeatQueenProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    machine_.prepare(sampleRate);
    mono_.assign(static_cast<size_t>(samplesPerBlock), 0.0f);
    selectedRhythm_ = -1;
}

bool BeatQueenProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void BeatQueenProcessor::applyParameters()
{
    if (const int index = rhythm_->getIndex(); index != selectedRhythm_)
    {
        machine_.selectPattern(index);
        selectedRhythm_ = index;
    }
    machine_.setTempo(tempo_->load());
    machine_.setRunning(run_->load() > 0.5f);
    machine_.setMasterLevel(master_->load());
    for (int i = 0; i < beatqueen::kNumVoices; ++i)
        machine_.setVoiceLevel(static_cast<VoiceId>(i), levels_[static_cast<size_t>(i)]->load());
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
        if (const auto voice = voiceForNote(msg.getNoteNumber()))
        {
            renderUpTo(juce::jlimit(0, numSamples, metadata.samplePosition));
            machine_.triggerVoice(*voice, msg.getFloatVelocity());
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
