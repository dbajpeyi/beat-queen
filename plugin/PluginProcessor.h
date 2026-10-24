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
    void setVoiceButton(beatqueen::mrk1::Button b, bool down) { machine_.requestButton(b, down); }
    int currentPulse() const { return machine_.currentPulse(); }
    bool downBeatLamp() const { return machine_.downBeatLamp(); }

    juce::AudioProcessorValueTreeState parameters;

    static juce::String rhythmParamId(int rhythm) { return "rhythm_" + juce::String(rhythm); }
    static juce::String trimParamId(int trim) { return "vr" + juce::String(trim + 1); }

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void applyParameters();

    beatqueen::DrumMachine machine_;
    std::vector<float> mono_;

    std::array<std::atomic<float>*, beatqueen::mrk1::kNumRhythms> rhythms_ {};
    std::array<std::atomic<float>*, beatqueen::mrk1::kNumTrims> trims_ {};
    std::array<float, beatqueen::mrk1::kNumTrims> appliedTrims_ {};
    std::atomic<float>* tempo_ = nullptr;
    std::atomic<float>* run_ = nullptr;
    std::atomic<float>* hostSync_ = nullptr;
    std::atomic<float>* volume_ = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BeatQueenProcessor)
};
