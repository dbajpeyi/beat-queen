#pragma once

#include "PanelComponents.h"
#include "PluginProcessor.h"

#include <juce_audio_utils/juce_audio_utils.h>

#include <array>
#include <memory>

// The whole instrument, drawn at a fixed base size; the editor scales it.
class FrontPanel final : public juce::Component, private juce::Timer
{
public:
    static constexpr int kWidth = 1120;
    static constexpr int kMainHeight = 540;
    static constexpr int kDrawerHeight = 200;

    explicit FrontPanel(BeatQueenProcessor&);

    int baseHeight() const { return kMainHeight + (serviceOpen() ? kDrawerHeight : 0); }
    bool serviceOpen() const { return service_.getToggleState(); }
    std::function<void()> onServiceToggled;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void paintDrawer(juce::Graphics&);

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;

    BeatQueenProcessor& processor_;
    juce::Image tolex_, metal_;

    std::vector<std::unique_ptr<beatqueen::ui::RhythmKey>> rhythmKeys_;
    std::vector<std::unique_ptr<ButtonAttachment>> rhythmAttachments_;

    beatqueen::ui::Lamp lamp_;
    beatqueen::ui::ToggleSwitch hostSync_;
    beatqueen::ui::RockerSwitch start_;
    juce::Slider volume_, speed_;
    std::unique_ptr<SliderAttachment> volumeAttachment_, speedAttachment_;
    std::unique_ptr<ButtonAttachment> hostSyncAttachment_, startAttachment_;

    std::array<beatqueen::ui::VoiceKey, beatqueen::mrk1::kNumButtons> voiceKeys_;

    juce::TextButton service_ { "SERVICE" };
    std::array<juce::Slider, beatqueen::mrk1::kNumTrims> trims_;
    std::array<std::unique_ptr<SliderAttachment>, beatqueen::mrk1::kNumTrims> trimAttachments_;
    int shownPulse_ = -1;

    juce::TooltipWindow tooltips_ { this, 600 };
};

class BeatQueenEditor final : public juce::AudioProcessorEditor
{
public:
    explicit BeatQueenEditor(BeatQueenProcessor&);
    ~BeatQueenEditor() override;

    void resized() override;

private:
    void updateAspect();

    beatqueen::ui::PanelLookAndFeel lookAndFeel_;
    FrontPanel panel_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BeatQueenEditor)
};
