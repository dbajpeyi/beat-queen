#pragma once

#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <memory>

class BeatQueenEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BeatQueenEditor(BeatQueenProcessor&);
    ~BeatQueenEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    struct VoiceStrip
    {
        juce::Slider level;
        juce::TextButton tap;
        std::unique_ptr<SliderAttachment> attachment;
    };

    BeatQueenProcessor& processor_;
    juce::LookAndFeel_V4 lookAndFeel_;

    juce::ComboBox rhythm_;
    juce::Slider tempo_, master_;
    juce::TextButton start_ { "START" };
    juce::ToggleButton hostSync_ { "Host sync" };
    std::array<VoiceStrip, beatqueen::kNumVoices> strips_;

    std::unique_ptr<ComboAttachment> rhythmAttachment_;
    std::unique_ptr<SliderAttachment> tempoAttachment_, masterAttachment_;
    std::unique_ptr<ButtonAttachment> startAttachment_, hostSyncAttachment_;

    juce::Rectangle<int> stepArea_;
    int shownStep_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BeatQueenEditor)
};
