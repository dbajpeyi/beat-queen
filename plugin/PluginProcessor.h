#pragma once

#include "beatqueen/DrumMachine.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>

class BeatQueenProcessor final : public juce::AudioProcessor
{
public:
    BeatQueenProcessor();

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Safe to call from the message thread.
    void tapVoice(beatqueen::VoiceId id) { machine_.requestManualTrigger(id); }
    int currentStep() const { return machine_.currentStep(); }
    int patternLength() const { return machine_.patternLength(); }

    juce::AudioProcessorValueTreeState parameters;

    static juce::String levelParamId(int voice) { return "level_" + juce::String(voice); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void applyParameters();

    beatqueen::DrumMachine machine_;
    std::vector<float> mono_;
    int selectedRhythm_ = -1;

    juce::AudioParameterChoice* rhythm_ = nullptr;
    std::atomic<float>* tempo_ = nullptr;
    std::atomic<float>* run_ = nullptr;
    std::atomic<float>* hostSync_ = nullptr;
    std::atomic<float>* master_ = nullptr;
    std::array<std::atomic<float>*, beatqueen::kNumVoices> levels_ {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BeatQueenProcessor)
};
