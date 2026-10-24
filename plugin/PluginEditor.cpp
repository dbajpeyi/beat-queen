#include "PluginEditor.h"

namespace {

namespace colours {
const juce::Colour walnut { 0xff2b1d14 };
const juce::Colour panel { 0xff3a2a1e };
const juce::Colour cream { 0xfff2e6cf };
const juce::Colour amber { 0xffe8902a };
const juce::Colour ledOff { 0xff4d3626 };
} // namespace colours

constexpr int kWidth = 780;
constexpr int kHeight = 440;

void styleRotary(juce::Slider& s)
{
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 70, 18);
}

} // namespace

BeatQueenEditor::BeatQueenEditor(BeatQueenProcessor& p) : AudioProcessorEditor(p), processor_(p)
{
    auto& lf = lookAndFeel_;
    lf.setColour(juce::Slider::thumbColourId, colours::amber);
    lf.setColour(juce::Slider::rotarySliderFillColourId, colours::amber);
    lf.setColour(juce::Slider::trackColourId, colours::amber);
    lf.setColour(juce::Slider::textBoxTextColourId, colours::cream);
    lf.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lf.setColour(juce::TextButton::buttonColourId, colours::panel);
    lf.setColour(juce::TextButton::buttonOnColourId, colours::amber);
    lf.setColour(juce::TextButton::textColourOffId, colours::cream);
    lf.setColour(juce::TextButton::textColourOnId, colours::walnut);
    lf.setColour(juce::ToggleButton::textColourId, colours::cream);
    lf.setColour(juce::ToggleButton::tickColourId, colours::amber);
    lf.setColour(juce::ComboBox::backgroundColourId, colours::panel);
    lf.setColour(juce::ComboBox::textColourId, colours::cream);
    lf.setColour(juce::ComboBox::arrowColourId, colours::amber);

    auto& params = processor_.parameters;

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*>(params.getParameter("rhythm")))
        rhythm_.addItemList(choice->choices, 1);
    addAndMakeVisible(rhythm_);
    rhythmAttachment_ = std::make_unique<ComboAttachment>(params, "rhythm", rhythm_);

    styleRotary(tempo_);
    addAndMakeVisible(tempo_);
    tempoAttachment_ = std::make_unique<SliderAttachment>(params, "tempo", tempo_);

    styleRotary(master_);
    addAndMakeVisible(master_);
    masterAttachment_ = std::make_unique<SliderAttachment>(params, "master", master_);

    start_.setClickingTogglesState(true);
    addAndMakeVisible(start_);
    startAttachment_ = std::make_unique<ButtonAttachment>(params, "run", start_);

    addAndMakeVisible(hostSync_);
    hostSyncAttachment_ = std::make_unique<ButtonAttachment>(params, "hostSync", hostSync_);

    for (int i = 0; i < beatqueen::kNumVoices; ++i)
    {
        auto& strip = strips_[static_cast<size_t>(i)];
        const auto id = static_cast<beatqueen::VoiceId>(i);

        strip.level.setSliderStyle(juce::Slider::LinearVertical);
        strip.level.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        addAndMakeVisible(strip.level);
        strip.attachment = std::make_unique<SliderAttachment>(params, BeatQueenProcessor::levelParamId(i), strip.level);

        strip.tap.setButtonText(beatqueen::voiceName(id));
        strip.tap.onClick = [this, id] { processor_.tapVoice(id); };
        addAndMakeVisible(strip.tap);
    }

    setLookAndFeel(&lookAndFeel_);
    setSize(kWidth, kHeight);
    startTimerHz(30);
}

BeatQueenEditor::~BeatQueenEditor()
{
    setLookAndFeel(nullptr);
}

void BeatQueenEditor::paint(juce::Graphics& g)
{
    g.fillAll(colours::walnut);

    auto header = getLocalBounds().removeFromTop(56).reduced(20, 0);
    g.setColour(colours::cream);
    g.setFont(juce::FontOptions(28.0f, juce::Font::bold));
    g.drawText("BEAT QUEEN", header, juce::Justification::centredLeft);
    g.setColour(colours::cream.withAlpha(0.6f));
    g.setFont(juce::FontOptions(13.0f));
    g.drawText("after the Maestro Rhythm King MRK-1", header, juce::Justification::centredRight);

    g.setFont(juce::FontOptions(12.0f));
    g.drawText("RHYTHM", rhythm_.getBounds().translated(0, -18), juce::Justification::topLeft);
    g.drawText("TEMPO", tempo_.getBounds().withHeight(14).translated(0, -16), juce::Justification::centred);
    g.drawText("VOLUME", master_.getBounds().withHeight(14).translated(0, -16), juce::Justification::centred);

    // Step lights.
    const int length = juce::jmax(1, processor_.patternLength());
    const float w = static_cast<float>(stepArea_.getWidth()) / static_cast<float>(length);
    for (int i = 0; i < length; ++i)
    {
        const auto led = juce::Rectangle<float>(stepArea_.getX() + i * w, static_cast<float>(stepArea_.getY()), w, 12.0f)
                             .reduced(2.0f, 0.0f);
        g.setColour(i == shownStep_ ? colours::amber : colours::ledOff);
        g.fillRoundedRectangle(led, 3.0f);
    }
}

void BeatQueenEditor::resized()
{
    auto area = getLocalBounds().reduced(20);
    area.removeFromTop(40);

    auto controls = area.removeFromTop(110);
    controls.removeFromTop(18);
    rhythm_.setBounds(controls.removeFromLeft(220).removeFromTop(28));
    controls.removeFromLeft(20);
    tempo_.setBounds(controls.removeFromLeft(100));
    controls.removeFromLeft(20);
    auto transport = controls.removeFromLeft(140);
    start_.setBounds(transport.removeFromTop(40));
    transport.removeFromTop(8);
    hostSync_.setBounds(transport.removeFromTop(26));
    master_.setBounds(controls.removeFromRight(100));

    area.removeFromTop(12);
    stepArea_ = area.removeFromTop(12);
    area.removeFromTop(16);

    const int stripWidth = area.getWidth() / beatqueen::kNumVoices;
    for (auto& strip : strips_)
    {
        auto column = area.removeFromLeft(stripWidth).reduced(6, 0);
        strip.tap.setBounds(column.removeFromBottom(44));
        column.removeFromBottom(8);
        strip.level.setBounds(column);
    }
}

void BeatQueenEditor::timerCallback()
{
    if (const int step = processor_.currentStep(); step != shownStep_)
    {
        shownStep_ = step;
        repaint(stepArea_);
    }
}
