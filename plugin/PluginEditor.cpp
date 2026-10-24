#include "PluginEditor.h"

namespace {

using namespace beatqueen;
namespace col = beatqueen::ui::colours;

// Base-size geometry, modelled on the Rhythm King cabinet.
const juce::Rectangle<float> kCabinet { 10.0f, 26.0f, 1100.0f, 508.0f };
const juce::Rectangle<float> kUpperFrame { 40.0f, 56.0f, 1040.0f, 248.0f };
const juce::Rectangle<float> kFaceplate { 40.0f, 314.0f, 1040.0f, 194.0f };
const juce::Rectangle<float> kControlBlock { 214.0f, 344.0f, 352.0f, 116.0f };

constexpr float kFrame = 10.0f;
constexpr int kKeyWidth = 42, kKeyHeight = 34, kKeyGap = 6, kKeysX = 186, kKeysY = 204;
constexpr int kVoiceX = 636, kVoiceY = 372, kVoiceSize = 34, kVoicePitch = 52;

juce::Colour capColour(mrk1::Category c)
{
    switch (c)
    {
        case mrk1::Category::Latin: return col::latin;
        case mrk1::Category::American: return col::american;
        case mrk1::Category::Traditional: return col::traditional;
    }
    return col::chrome;
}

juce::Font font(float height, bool bold = true)
{
    return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
}

// Engraved panel label with a little letter-spacing.
void label(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> r, juce::Colour c, float height,
           juce::Justification just = juce::Justification::centred)
{
    g.setColour(c);
    g.setFont(font(height).withExtraKerningFactor(0.08f));
    g.drawFittedText(text, r.toNearestInt(), just, 2, 0.9f);
}

// Scale ticks around a knob, with optional end/number legends.
void knobScale(juce::Graphics& g, juce::Point<float> c, float radius, int ticks, const juce::StringArray& legends)
{
    const float start = -2.4f, end = 2.4f; // radians from 12 o'clock, matching the slider's rotary range
    g.setColour(col::panelText.withAlpha(0.85f));
    for (int i = 0; i < ticks; ++i)
    {
        const float a = start + (end - start) * static_cast<float>(i) / static_cast<float>(ticks - 1);
        const juce::Point<float> d(std::sin(a), -std::cos(a));
        g.fillEllipse(juce::Rectangle<float>(2.4f, 2.4f).withCentre(c + d * radius));
    }
    g.setFont(font(8.5f));
    for (int i = 0; i < legends.size(); ++i)
    {
        const float a = start + (end - start) * static_cast<float>(i) / static_cast<float>(juce::jmax(1, legends.size() - 1));
        const juce::Point<float> d(std::sin(a), -std::cos(a));
        g.drawText(legends[i], juce::Rectangle<float>(26.0f, 10.0f).withCentre(c + d * (radius + 9.0f)),
                   juce::Justification::centred);
    }
}

} // namespace

// ---------------------------------------------------------------------------

FrontPanel::FrontPanel(BeatQueenProcessor& p) : processor_(p)
{
    auto& params = processor_.parameters;
    tolex_ = ui::makeTolexTexture(kWidth, kMainHeight + kDrawerHeight);
    metal_ = ui::makeBrushedMetalTexture(static_cast<int>(kFaceplate.getWidth()), static_cast<int>(kFaceplate.getHeight()));

    for (int r = 0; r < mrk1::kNumRhythms; ++r)
    {
        const auto& rhythm = mrk1::rhythms()[static_cast<size_t>(r)];
        auto key = std::make_unique<ui::RhythmKey>(rhythm.name, capColour(rhythm.category));
        key->setTooltip(juce::String(rhythm.name) + (rhythm.triplet ? " (triplet timing)" : "")
                        + ". Latching; press several to combine rhythms.");
        addAndMakeVisible(*key);
        rhythmAttachments_.push_back(std::make_unique<ButtonAttachment>(params, BeatQueenProcessor::rhythmParamId(r), *key));
        rhythmKeys_.push_back(std::move(key));
    }

    addAndMakeVisible(lamp_);
    lamp_.setTooltip("Down beat: lights every four beats");

    hostSync_.setTooltip("Host sync: follow the DAW's tempo and bar position while it plays");
    addAndMakeVisible(hostSync_);
    hostSyncAttachment_ = std::make_unique<ButtonAttachment>(params, "hostSync", hostSync_);

    start_.setTooltip("Start / stop (the reset foot switch): starts the rhythm from the down beat");
    addAndMakeVisible(start_);
    startAttachment_ = std::make_unique<ButtonAttachment>(params, "run", start_);

    for (auto* s : { &volume_, &speed_ })
    {
        s->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s->setRotaryParameters(juce::MathConstants<float>::twoPi - 2.4f, juce::MathConstants<float>::twoPi + 2.4f, true);
        s->setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s->setPopupDisplayEnabled(true, true, this);
        addAndMakeVisible(*s);
    }
    volumeAttachment_ = std::make_unique<SliderAttachment>(params, "volume", volume_);
    speedAttachment_ = std::make_unique<SliderAttachment>(params, "tempo", speed_);
    speed_.setTextValueSuffix(" BPM");
    speed_.onValueChange = [this] { repaint(kControlBlock.toNearestInt()); };

    for (int b = 0; b < mrk1::kNumButtons; ++b)
    {
        auto& key = voiceKeys_[static_cast<size_t>(b)];
        const auto button = static_cast<mrk1::Button>(b);
        key.onPressChanged = [this, button](bool down) { processor_.setVoiceButton(button, down); };
        addAndMakeVisible(key);
    }

    service_.setClickingTogglesState(true);
    service_.setTooltip("Open the case: the instrumentation board's trimmers VR1-VR12 and the clock");
    service_.setColour(juce::TextButton::buttonColourId, juce::Colour(0xff1a1a1a));
    service_.setColour(juce::TextButton::buttonOnColourId, juce::Colour(0xff2a2a2a));
    service_.setColour(juce::TextButton::textColourOffId, col::chrome);
    service_.setColour(juce::TextButton::textColourOnId, col::lampOn);
    service_.setColour(juce::ComboBox::outlineColourId, col::chrome.withAlpha(0.4f));
    service_.onClick = [this] {
        for (auto& t : trims_)
            t.setVisible(serviceOpen());
        if (onServiceToggled)
            onServiceToggled();
    };
    addAndMakeVisible(service_);

    for (int t = 0; t < mrk1::kNumTrims; ++t)
    {
        auto& s = trims_[static_cast<size_t>(t)];
        s.getProperties().set("trimmer", true);
        s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        s.setPopupDisplayEnabled(true, true, this);
        s.setTooltip(mrk1::trimName(static_cast<mrk1::Trim>(t)));
        addChildComponent(s);
        trimAttachments_[static_cast<size_t>(t)] =
            std::make_unique<SliderAttachment>(params, BeatQueenProcessor::trimParamId(t), s);
    }

    startTimerHz(30);
}

void FrontPanel::resized()
{
    for (int r = 0; r < mrk1::kNumRhythms; ++r)
        rhythmKeys_[static_cast<size_t>(r)]->setBounds(kKeysX + r * (kKeyWidth + kKeyGap), kKeysY, kKeyWidth, kKeyHeight);

    lamp_.setBounds(juce::Rectangle<int>(34, 34).withCentre({ 100, 384 }));
    hostSync_.setBounds(juce::Rectangle<int>(30, 34).withCentre({ 100, 448 }));

    start_.setBounds(240, 374, 30, 52);
    volume_.setBounds(juce::Rectangle<int>(58, 58).withCentre({ 354, 402 }));
    speed_.setBounds(juce::Rectangle<int>(58, 58).withCentre({ 486, 402 }));

    for (int b = 0; b < mrk1::kNumButtons; ++b)
        voiceKeys_[static_cast<size_t>(b)].setBounds(kVoiceX + b * kVoicePitch, kVoiceY, kVoiceSize, 30);

    service_.setBounds(984, 478, 84, 20);

    const int trimWidth = 76;
    const int trimX = (kWidth - trimWidth * mrk1::kNumTrims) / 2;
    for (int t = 0; t < mrk1::kNumTrims; ++t)
        trims_[static_cast<size_t>(t)].setBounds(trimX + t * trimWidth + 20, kMainHeight + 62, 36, 36);
}

void FrontPanel::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.fillAll(juce::Colour(0xff4a4c50)); // studio backdrop
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff5d5f63), bounds.getCentreX(), 0.0f, juce::Colour(0xff2f3033),
                                           bounds.getCentreX(), bounds.getBottom(), false));
    g.fillRect(bounds);

    // Carry handle.
    const auto handle = juce::Rectangle<float>(420.0f, 8.0f, 280.0f, 22.0f);
    g.setColour(juce::Colours::black.withAlpha(0.4f));
    g.fillRoundedRectangle(handle.translated(0.0f, 3.0f), 4.0f);
    g.setGradientFill(juce::ColourGradient(juce::Colour(0xff3a3a3a), 0.0f, handle.getY(), juce::Colour(0xff0e0e0e), 0.0f,
                                           handle.getBottom(), false));
    g.fillRoundedRectangle(handle, 4.0f);

    // Tolex cabinet.
    auto cabinet = kCabinet.withHeight(static_cast<float>(baseHeight()) - kCabinet.getY() - 6.0f);
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.fillRoundedRectangle(cabinet.translated(0.0f, 4.0f), 14.0f);
    {
        juce::Graphics::ScopedSaveState state(g);
        juce::Path shape;
        shape.addRoundedRectangle(cabinet, 14.0f);
        g.reduceClipRegion(shape);
        g.drawImageAt(tolex_, 0, 0);
        g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.07f), 0.0f, cabinet.getY(),
                                               juce::Colours::black.withAlpha(0.25f), 0.0f, cabinet.getBottom(), false));
        g.fillRect(cabinet);
    }

    // Upper panel: black plexi in a chrome frame, three screws along the top strip.
    ui::drawChromeFrame(g, kUpperFrame, kFrame);
    const auto plexi = kUpperFrame.reduced(kFrame);
    g.setColour(col::plexi);
    g.fillRect(plexi);
    g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.06f), plexi.getX(), plexi.getY(),
                                           juce::Colours::transparentWhite, plexi.getX() + 300.0f, plexi.getBottom(), false));
    g.fillRect(plexi);
    for (float x : { 330.0f, 560.0f, 790.0f })
        ui::drawScrew(g, { x, kUpperFrame.getY() + kFrame * 0.5f }, 3.2f);

    ui::drawEmblem(g, { 82.0f, 124.0f, 66.0f, 86.0f });

    g.setColour(col::panelText);
    ui::drawSpacedText(g, "BEAT  QUEEN", { 420.0f, 158.0f, 440.0f, 20.0f }, 15.0f);

    // Rhythm group legends under the key row.
    const float legendY = static_cast<float>(kKeysY + kKeyHeight + 6);
    auto keyX = [](int i) { return static_cast<float>(kKeysX + i * (kKeyWidth + kKeyGap)); };
    label(g, "LATIN", { keyX(0), legendY, 80.0f, 12.0f }, col::panelText, 8.5f, juce::Justification::centredLeft);
    label(g, "AMERICAN", { keyX(8), legendY, keyX(15) - keyX(8), 12.0f }, col::panelText, 8.5f);
    label(g, "TRADITIONAL", { keyX(15), legendY, keyX(18) - keyX(15) - kKeyGap, 12.0f }, col::panelText, 8.5f);

    // Lower faceplate: brushed aluminium between chrome strips.
    g.drawImageAt(metal_, static_cast<int>(kFaceplate.getX()), static_cast<int>(kFaceplate.getY()));
    ui::drawChromeFrame(g, kFaceplate.expanded(0.0f, 2.0f), 4.0f);
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.drawLine(kFaceplate.getX(), kFaceplate.getY() - 6.0f, kFaceplate.getRight(), kFaceplate.getY() - 6.0f, 1.5f);

    label(g, "DOWN BEAT", { 60.0f, 350.0f, 80.0f, 12.0f }, col::engraving, 8.5f);
    label(g, "HOST SYNC", { 60.0f, 470.0f, 80.0f, 12.0f }, col::engraving, 8.0f);

    // Black control block: start rocker, volume and speed.
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.fillRoundedRectangle(kControlBlock.translated(1.0f, 2.0f), 3.0f);
    g.setColour(juce::Colour(0xff0d0d0d));
    g.fillRoundedRectangle(kControlBlock, 3.0f);
    label(g, "START  STOP", { 220.0f, 352.0f, 70.0f, 12.0f }, col::panelText, 8.0f);
    label(g, "VOLUME", { 314.0f, 352.0f, 80.0f, 12.0f }, col::panelText, 8.5f);
    label(g, "SPEED", { 446.0f, 352.0f, 80.0f, 12.0f }, col::panelText, 8.5f);
    knobScale(g, { 354.0f, 402.0f }, 35.0f, 11, { "0", "", "", "", "", "", "", "", "", "", "10" });
    knobScale(g, { 486.0f, 402.0f }, 35.0f, 11, { "SLOW", "", "", "", "", "", "", "", "", "", "FAST" });
    label(g, juce::String(juce::roundToInt(speed_.getValue())) + " BPM", { 446.0f, 442.0f, 80.0f, 12.0f },
          col::panelText.withAlpha(0.7f), 8.0f);

    // Voice key legends.
    for (int b = 0; b < mrk1::kNumButtons; ++b)
    {
        auto name = juce::String(mrk1::buttonName(static_cast<mrk1::Button>(b))).toUpperCase();
        if (name == "HI-HAT CYMBAL")
            name = "HI-HAT\nCYMBAL";
        else
            name = name.replace(" ", "\n");
        label(g, name, { static_cast<float>(kVoiceX + b * kVoicePitch) - 10.0f, static_cast<float>(kVoiceY + 34), 54.0f, 24.0f },
              col::engraving, 8.0f);
    }

    if (serviceOpen())
        paintDrawer(g);
}

void FrontPanel::paintDrawer(juce::Graphics& g)
{
    // The instrumentation board, seen with the case open.
    const auto board = juce::Rectangle<float>(40.0f, static_cast<float>(kMainHeight) + 4.0f, 1040.0f,
                                              static_cast<float>(kDrawerHeight) - 22.0f);
    g.setColour(col::pcb);
    g.fillRoundedRectangle(board, 4.0f);
    g.setColour(juce::Colour(0xffb8913a));
    g.drawRoundedRectangle(board.reduced(3.0f), 3.0f, 1.2f);
    for (auto corner : { board.getTopLeft() + juce::Point<float>(12, 12), board.getTopRight() + juce::Point<float>(-12, 12),
                         board.getBottomLeft() + juce::Point<float>(12, -12), board.getBottomRight() + juce::Point<float>(-12, -12) })
        ui::drawScrew(g, corner, 4.0f);

    g.setColour(col::silkscreen);
    g.setFont(font(10.0f));
    g.drawText("INSTRUMENTATION BOARD    DWG NO. 4", board.reduced(28.0f, 10.0f).withHeight(14.0f),
               juce::Justification::centredLeft);
    g.drawText("SERVICE ADJUSTMENTS", board.reduced(28.0f, 10.0f).withHeight(14.0f), juce::Justification::centredRight);

    for (int t = 0; t < mrk1::kNumTrims; ++t)
    {
        const auto r = trims_[static_cast<size_t>(t)].getBounds().toFloat();
        auto name = juce::String(mrk1::trimName(static_cast<mrk1::Trim>(t))).toUpperCase();
        const auto vr = name.upToFirstOccurrenceOf(" ", false, false);
        g.setFont(font(9.0f));
        g.drawText(vr, r.withY(r.getY() - 16.0f).withHeight(12.0f).expanded(20.0f, 0.0f), juce::Justification::centred);
        g.setFont(font(7.5f, false));
        g.drawFittedText(name.fromFirstOccurrenceOf(" ", false, false),
                         r.withY(r.getBottom() + 4.0f).withHeight(22.0f).expanded(20.0f, 0.0f).toNearestInt(),
                         juce::Justification::centredTop, 2, 0.8f);
    }

    // Clock: the four ring cycles of the 5-stage multivibrator Q1-Q5.
    const float ledY = board.getBottom() - 30.0f;
    g.setFont(font(9.0f));
    g.drawText("CLOCK  Q1-Q5", juce::Rectangle<float>(80.0f, ledY - 2.0f, 110.0f, 14.0f), juce::Justification::centredLeft);
    for (int i = 0; i < mrk1::kPulsesPerCycle; ++i)
    {
        const float x = 200.0f + static_cast<float>(i) * 22.0f + static_cast<float>(i / mrk1::kStages) * 26.0f;
        const auto ledRect = juce::Rectangle<float>(8.0f, 8.0f).withCentre({ x, ledY + 5.0f });
        const bool on = i == shownPulse_;
        if (on)
        {
            g.setColour(col::led.withAlpha(0.35f));
            g.fillEllipse(ledRect.expanded(4.0f));
        }
        g.setColour(on ? col::led : col::led.darker(2.2f));
        g.fillEllipse(ledRect);
    }
}

void FrontPanel::timerCallback()
{
    lamp_.setLit(processor_.downBeatLamp());
    if (const int pulse = processor_.currentPulse(); pulse != shownPulse_)
    {
        shownPulse_ = pulse;
        if (serviceOpen())
            repaint(0, kMainHeight + 100, kWidth, kDrawerHeight - 100);
    }
}

// ---------------------------------------------------------------------------

BeatQueenEditor::BeatQueenEditor(BeatQueenProcessor& p) : AudioProcessorEditor(p), panel_(p)
{
    setLookAndFeel(&lookAndFeel_);
    addAndMakeVisible(panel_);
    panel_.onServiceToggled = [this] { updateAspect(); };
    setResizable(true, true);
    updateAspect();
}

BeatQueenEditor::~BeatQueenEditor()
{
    setLookAndFeel(nullptr);
}

void BeatQueenEditor::updateAspect()
{
    const double aspect = static_cast<double>(FrontPanel::kWidth) / panel_.baseHeight();
    const int width = getWidth() > 0 ? getWidth() : FrontPanel::kWidth;
    if (auto* c = getConstrainer())
    {
        c->setFixedAspectRatio(aspect);
        c->setSizeLimits(FrontPanel::kWidth / 2, static_cast<int>(FrontPanel::kWidth / 2 / aspect), FrontPanel::kWidth * 2,
                         static_cast<int>(FrontPanel::kWidth * 2 / aspect));
    }
    setSize(width, static_cast<int>(std::lround(width / aspect)));
}

void BeatQueenEditor::resized()
{
    panel_.setBounds(0, 0, FrontPanel::kWidth, panel_.baseHeight());
    const float scale = static_cast<float>(getWidth()) / FrontPanel::kWidth;
    panel_.setTransform(juce::AffineTransform::scale(scale));
}
