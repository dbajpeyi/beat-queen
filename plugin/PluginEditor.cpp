#include "PluginEditor.h"

namespace {

namespace colours {
const juce::Colour cabinet { 0xff2b1d14 };
const juce::Colour panel { 0xff3a2a1e };
const juce::Colour faceplate { 0xffd9d4c7 };
const juce::Colour ink { 0xff1d1a17 };
const juce::Colour cream { 0xfff2e6cf };
const juce::Colour amber { 0xffe8902a };
const juce::Colour lampOff { 0xff4d3626 };
const juce::Colour latin { 0xffc0392b };
const juce::Colour american { 0xffd4a017 };
const juce::Colour traditional { 0xff2e6db4 };
const juce::Colour voiceButton { 0xff151515 };
} // namespace colours

constexpr int kWidth = 960;
constexpr int kHeight = 470;
constexpr int kServiceHeight = 150;

juce::Colour categoryColour(beatqueen::mrk1::Category c)
{
    switch (c)
    {
        case beatqueen::mrk1::Category::Latin: return colours::latin;
        case beatqueen::mrk1::Category::American: return colours::american;
        case beatqueen::mrk1::Category::Traditional: return colours::traditional;
    }
    return colours::panel;
}

void styleRotary(juce::Slider& s, int textWidth = 70)
{
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, textWidth, 18);
    s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
}

} // namespace

BeatQueenEditor::BeatQueenEditor(BeatQueenProcessor& p) : AudioProcessorEditor(p), processor_(p)
{
    using namespace beatqueen::mrk1;

    auto& lf = lookAndFeel_;
    lf.setColour(juce::Slider::thumbColourId, colours::amber);
    lf.setColour(juce::Slider::rotarySliderFillColourId, colours::amber);
    lf.setColour(juce::Slider::rotarySliderOutlineColourId, colours::lampOff);
    lf.setColour(juce::Slider::textBoxTextColourId, colours::cream);
    lf.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    lf.setColour(juce::TextButton::buttonColourId, colours::panel);
    lf.setColour(juce::TextButton::buttonOnColourId, colours::amber);
    lf.setColour(juce::TextButton::textColourOffId, colours::cream);
    lf.setColour(juce::TextButton::textColourOnId, colours::ink);
    lf.setColour(juce::ToggleButton::textColourId, colours::cream);
    lf.setColour(juce::ToggleButton::tickColourId, colours::amber);
    setLookAndFeel(&lookAndFeel_);

    auto& params = processor_.parameters;

    // Rhythm selector switches: latching, and combinable as on the real panel.
    for (int r = 0; r < kNumRhythms; ++r)
    {
        auto& b = rhythmButtons_[static_cast<size_t>(r)];
        const auto& rhythm = rhythms()[static_cast<size_t>(r)];
        const auto colour = categoryColour(rhythm.category);
        b.setButtonText(rhythm.name);
        b.setClickingTogglesState(true);
        b.setColour(juce::TextButton::buttonColourId, colour.darker(0.9f));
        b.setColour(juce::TextButton::buttonOnColourId, colour.brighter(0.3f));
        b.setColour(juce::TextButton::textColourOffId, colour.brighter(0.6f));
        b.setColour(juce::TextButton::textColourOnId, colours::ink);
        b.setTooltip(rhythm.triplet ? "Triplet timing" : "");
        addAndMakeVisible(b);
        rhythmAttachments_[static_cast<size_t>(r)] =
            std::make_unique<ButtonAttachment>(params, BeatQueenProcessor::rhythmParamId(r), b);
    }

    styleRotary(speed_);
    addAndMakeVisible(speed_);
    speedAttachment_ = std::make_unique<SliderAttachment>(params, "tempo", speed_);

    styleRotary(volume_);
    addAndMakeVisible(volume_);
    volumeAttachment_ = std::make_unique<SliderAttachment>(params, "volume", volume_);

    start_.setClickingTogglesState(true);
    start_.setTooltip("The reset foot switch: starts the rhythm from the down beat");
    addAndMakeVisible(start_);
    startAttachment_ = std::make_unique<ButtonAttachment>(params, "run", start_);

    hostSync_.setTooltip("Follow the host's tempo and bar position while it plays");
    addAndMakeVisible(hostSync_);
    hostSyncAttachment_ = std::make_unique<ButtonAttachment>(params, "hostSync", hostSync_);

    // The eight black voice buttons: momentary, play with or without a rhythm.
    for (int b = 0; b < kNumButtons; ++b)
    {
        auto& vb = voiceButtons_[static_cast<size_t>(b)];
        const auto button = static_cast<Button>(b);
        vb.setButtonText(buttonName(button));
        vb.setColour(juce::TextButton::buttonColourId, colours::voiceButton);
        vb.setColour(juce::TextButton::buttonOnColourId, colours::amber);
        vb.onPressChanged = [this, button](bool down) { processor_.setVoiceButton(button, down); };
        addAndMakeVisible(vb);
    }

    // Service trimmers VR1-VR12.
    serviceToggle_.setClickingTogglesState(true);
    serviceToggle_.onClick = [this] { layoutWindow(); };
    addAndMakeVisible(serviceToggle_);
    for (int t = 0; t < kNumTrims; ++t)
    {
        auto& s = trims_[static_cast<size_t>(t)];
        styleRotary(s, 60);
        s.setTooltip(trimName(static_cast<Trim>(t)));
        addChildComponent(s);
        trimAttachments_[static_cast<size_t>(t)] =
            std::make_unique<SliderAttachment>(params, BeatQueenProcessor::trimParamId(t), s);
    }

    layoutWindow();
    startTimerHz(30);
}

BeatQueenEditor::~BeatQueenEditor()
{
    setLookAndFeel(nullptr);
}

void BeatQueenEditor::layoutWindow()
{
    const bool service = serviceToggle_.getToggleState();
    for (auto& t : trims_)
        t.setVisible(service);
    setSize(kWidth, kHeight + (service ? kServiceHeight : 0));
}

void BeatQueenEditor::paint(juce::Graphics& g)
{
    using namespace beatqueen::mrk1;
    g.fillAll(colours::cabinet);

    // Header.
    auto header = getLocalBounds().removeFromTop(56).reduced(20, 0);
    g.setColour(colours::cream);
    g.setFont(juce::FontOptions(28.0f, juce::Font::bold));
    g.drawText("BEAT QUEEN", header, juce::Justification::centredLeft);
    g.setColour(colours::cream.withAlpha(0.6f));
    g.setFont(juce::FontOptions(13.0f));
    g.drawText("Maestro Rhythm King MRK-1, simulated from its schematics", header, juce::Justification::centredRight);

    // Rhythm groups.
    const Category categories[] = { Category::Latin, Category::American, Category::Traditional };
    for (size_t i = 0; i < groupAreas_.size(); ++i)
    {
        const auto area = groupAreas_[i];
        g.setColour(colours::panel);
        g.fillRoundedRectangle(area.toFloat(), 6.0f);
        g.setColour(categoryColour(categories[i]).brighter(0.4f));
        g.setFont(juce::FontOptions(12.0f, juce::Font::bold));
        g.drawText(juce::String(categoryName(categories[i])).toUpperCase(), area.withHeight(22).reduced(10, 0),
                   juce::Justification::centredLeft);
    }

    // Control labels.
    g.setColour(colours::cream.withAlpha(0.7f));
    g.setFont(juce::FontOptions(12.0f));
    g.drawText("SPEED", speed_.getBounds().withHeight(14).translated(0, -16), juce::Justification::centred);
    g.drawText("VOLUME", volume_.getBounds().withHeight(14).translated(0, -16), juce::Justification::centred);
    g.drawText("DOWN BEAT", lampArea_.withHeight(14).translated(0, -18).expanded(20, 0), juce::Justification::centred);
    g.drawText("CLOCK", pulseArea_.withHeight(14).translated(0, -18), juce::Justification::centredLeft);

    // Down beat lamp.
    const auto lamp = lampArea_.toFloat();
    if (lampOn_)
    {
        g.setColour(colours::amber.withAlpha(0.35f));
        g.fillEllipse(lamp.expanded(6.0f));
    }
    g.setColour(lampOn_ ? colours::amber : colours::lampOff);
    g.fillEllipse(lamp);

    // Clock pulses: 4 ring cycles of the 5-stage multivibrator.
    const float w = static_cast<float>(pulseArea_.getWidth()) / kPulsesPerCycle;
    for (int i = 0; i < kPulsesPerCycle; ++i)
    {
        const auto led = juce::Rectangle<float>(pulseArea_.getX() + i * w, static_cast<float>(pulseArea_.getY()), w,
                                                static_cast<float>(pulseArea_.getHeight()))
                             .reduced(i % kStages == 0 ? 1.0f : 2.5f, 0.0f);
        g.setColour(i == shownPulse_ ? colours::amber : colours::lampOff);
        g.fillRoundedRectangle(led, 2.0f);
    }

    // Voice button strip and service panel backgrounds.
    if (!serviceArea_.isEmpty())
    {
        g.setColour(colours::panel);
        g.fillRoundedRectangle(serviceArea_.toFloat(), 6.0f);
        g.setColour(colours::cream.withAlpha(0.7f));
        g.setFont(juce::FontOptions(11.0f));
        for (int t = 0; t < kNumTrims; ++t)
            g.drawFittedText(trimName(static_cast<Trim>(t)), trims_[static_cast<size_t>(t)].getBounds().withHeight(26).translated(0, -26),
                             juce::Justification::centred, 2);
    }
}

void BeatQueenEditor::resized()
{
    using namespace beatqueen::mrk1;
    auto area = getLocalBounds().reduced(16);
    area.removeFromTop(44);

    // Rhythm selector groups: 8 Latin, 7 American, 3 Traditional.
    auto rhythmRow = area.removeFromTop(140);
    const int groupWidths[] = { 4, 4, 2 }; // button columns per group
    const int columns = 10;
    const int gap = 10;
    const int columnWidth = (rhythmRow.getWidth() - 2 * gap) / columns;
    int first = 0;
    const int counts[] = { 8, 7, 3 };
    for (int grp = 0; grp < 3; ++grp)
    {
        auto groupArea = rhythmRow.removeFromLeft(columnWidth * groupWidths[grp]);
        rhythmRow.removeFromLeft(gap);
        groupAreas_[static_cast<size_t>(grp)] = groupArea;
        auto inner = groupArea.reduced(8);
        inner.removeFromTop(16);
        const int rows = 2;
        const int bw = inner.getWidth() / groupWidths[grp];
        const int bh = inner.getHeight() / rows;
        for (int i = 0; i < counts[grp]; ++i)
        {
            const int col = i % groupWidths[grp], row = i / groupWidths[grp];
            rhythmButtons_[static_cast<size_t>(first + i)].setBounds(
                juce::Rectangle<int>(inner.getX() + col * bw, inner.getY() + row * bh, bw, bh).reduced(3));
        }
        first += counts[grp];
    }

    // Front controls.
    area.removeFromTop(24);
    auto controls = area.removeFromTop(120);
    controls.removeFromTop(16);
    volume_.setBounds(controls.removeFromLeft(100));
    controls.removeFromLeft(16);
    speed_.setBounds(controls.removeFromLeft(100));
    controls.removeFromLeft(24);
    auto transport = controls.removeFromLeft(150);
    start_.setBounds(transport.removeFromTop(44));
    transport.removeFromTop(8);
    hostSync_.setBounds(transport.removeFromTop(24));
    controls.removeFromLeft(40);
    lampArea_ = controls.removeFromLeft(40).withSizeKeepingCentre(26, 26).translated(0, -18);
    controls.removeFromLeft(40);
    pulseArea_ = controls.withTrimmedRight(4).withHeight(14).translated(0, 22);

    // Voice push buttons.
    area.removeFromTop(16);
    auto voices = area.removeFromTop(56);
    const int vw = voices.getWidth() / kNumButtons;
    for (auto& vb : voiceButtons_)
        vb.setBounds(voices.removeFromLeft(vw).reduced(5, 2));

    area.removeFromTop(16);
    serviceToggle_.setBounds(area.removeFromTop(28).removeFromLeft(200));

    serviceArea_ = {};
    if (serviceToggle_.getToggleState())
    {
        area.removeFromTop(12);
        serviceArea_ = area.removeFromTop(kServiceHeight - 30);
        auto knobs = serviceArea_.reduced(8);
        knobs.removeFromTop(26);
        const int kw = knobs.getWidth() / kNumTrims;
        for (auto& t : trims_)
            t.setBounds(knobs.removeFromLeft(kw).reduced(4, 0));
    }
}

void BeatQueenEditor::timerCallback()
{
    const bool lamp = processor_.downBeatLamp();
    const int pulse = processor_.currentPulse();
    if (lamp != lampOn_ || pulse != shownPulse_)
    {
        lampOn_ = lamp;
        shownPulse_ = pulse;
        repaint(lampArea_.expanded(8));
        repaint(pulseArea_);
    }
}
