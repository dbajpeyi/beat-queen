#pragma once

// Widgets and textures for the Rhythm King-style front panel.

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace beatqueen::ui {

namespace colours {
const juce::Colour tolex { 0xff161616 };
const juce::Colour plexi { 0xff0b0b0c };
const juce::Colour chrome { 0xffc9ccd0 };
const juce::Colour engraving { 0xff1b1b1b };
const juce::Colour panelText { 0xfff1f1ee };
const juce::Colour latin { 0xffe5512d };
const juce::Colour american { 0xffe8c21e };
const juce::Colour traditional { 0xff1f8a8f };
const juce::Colour lampOn { 0xffffb347 };
const juce::Colour pcb { 0xff1f5c33 };
const juce::Colour silkscreen { 0xffe8efe4 };
const juce::Colour led { 0xffff3b1f };
} // namespace colours

// Latching rhythm selector key with a coloured cap.
class RhythmKey final : public juce::Button
{
public:
    RhythmKey(const juce::String& name, juce::Colour cap);
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;

private:
    juce::Colour cap_;
};

// Black momentary voice key; reports both press and release.
class VoiceKey final : public juce::Button
{
public:
    VoiceKey();
    std::function<void(bool)> onPressChanged;
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;

private:
    void buttonStateChanged() override;
    bool wasDown_ = false;
};

// Rocker switch with a pilot light, toggling on click.
class RockerSwitch final : public juce::Button
{
public:
    RockerSwitch();
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
};

// Small chrome bat-handle toggle switch.
class ToggleSwitch final : public juce::Button
{
public:
    ToggleSwitch();
    void paintButton(juce::Graphics&, bool highlighted, bool down) override;
};

// Jewel lamp in a chrome bezel.
class Lamp final : public juce::Component, public juce::SettableTooltipClient
{
public:
    void setLit(bool lit);
    void paint(juce::Graphics&) override;

private:
    bool lit_ = false;
};

// Chrome knurled knobs, and PCB trimmers for sliders with the "trimmer" property.
class PanelLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    PanelLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height, float sliderPos, float startAngle,
                          float endAngle, juce::Slider&) override;
    juce::Font getSliderPopupFont(juce::Slider&) override;
};

juce::Image makeTolexTexture(int width, int height);
juce::Image makeBrushedMetalTexture(int width, int height);
void drawScrew(juce::Graphics&, juce::Point<float> centre, float radius);
void drawChromeFrame(juce::Graphics&, juce::Rectangle<float> outer, float thickness);
void drawEmblem(juce::Graphics&, juce::Rectangle<float> area);
void drawSpacedText(juce::Graphics&, const juce::String& text, juce::Rectangle<float> area, float height);

} // namespace beatqueen::ui
