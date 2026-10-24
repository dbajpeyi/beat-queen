#pragma once

#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <functional>
#include <memory>

// A push button that reports both press and release (the MRK-1 voice buttons).
class MomentaryButton final : public juce::TextButton
{
public:
    using juce::TextButton::TextButton;
    std::function<void(bool)> onPressChanged;

private:
    void buttonStateChanged() override
    {
        juce::TextButton::buttonStateChanged();
        if (const bool down = isDown(); down != wasDown_)
        {
            wasDown_ = down;
            if (onPressChanged)
                onPressChanged(down);
        }
    }
    bool wasDown_ = false;
};

class BeatQueenEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BeatQueenEditor(BeatQueenProcessor&);
    ~BeatQueenEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void layoutWindow();

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    BeatQueenProcessor& processor_;
    juce::LookAndFeel_V4 lookAndFeel_;

    std::array<juce::TextButton, beatqueen::mrk1::kNumRhythms> rhythmButtons_;
    std::array<std::unique_ptr<ButtonAttachment>, beatqueen::mrk1::kNumRhythms> rhythmAttachments_;

    juce::Slider speed_, volume_;
    std::unique_ptr<SliderAttachment> speedAttachment_, volumeAttachment_;
    juce::TextButton start_ { "START" };
    juce::ToggleButton hostSync_ { "Host sync" };
    std::unique_ptr<ButtonAttachment> startAttachment_, hostSyncAttachment_;

    std::array<MomentaryButton, beatqueen::mrk1::kNumButtons> voiceButtons_;

    juce::TextButton serviceToggle_ { "Service adjustments" };
    std::array<juce::Slider, beatqueen::mrk1::kNumTrims> trims_;
    std::array<std::unique_ptr<SliderAttachment>, beatqueen::mrk1::kNumTrims> trimAttachments_;

    std::array<juce::Rectangle<int>, 3> groupAreas_;
    juce::Rectangle<int> lampArea_, pulseArea_, serviceArea_;
    bool lampOn_ = false;
    int shownPulse_ = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BeatQueenEditor)
};
