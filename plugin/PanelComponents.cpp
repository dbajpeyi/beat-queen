#include "PanelComponents.h"

namespace beatqueen::ui {

namespace {

juce::Font panelFont(float height, bool bold = true)
{
    return juce::Font(juce::FontOptions(height, bold ? juce::Font::bold : juce::Font::plain));
}

// A bevelled key body: lighter top edge, darker bottom edge.
void drawKeyBody(juce::Graphics& g, juce::Rectangle<float> r, juce::Colour face, bool down)
{
    const float travel = down ? 3.5f : 0.0f;
    auto well = r;
    g.setColour(juce::Colours::black.withAlpha(0.85f));
    g.fillRoundedRectangle(well, 2.5f);

    auto cap = r.reduced(1.5f).withTrimmedBottom(down ? 1.0f : 3.5f).translated(0.0f, travel);
    // Side of the key, visible below the cap when raised.
    g.setColour(face.darker(1.2f));
    g.fillRoundedRectangle(cap.withHeight(cap.getHeight() + (down ? 1.0f : 3.0f)), 2.0f);

    juce::ColourGradient grad(face.brighter(down ? 0.05f : 0.25f), cap.getX(), cap.getY(),
                              face.darker(down ? 0.35f : 0.15f), cap.getX(), cap.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(cap, 2.0f);
    g.setColour(juce::Colours::white.withAlpha(down ? 0.08f : 0.25f));
    g.drawLine(cap.getX() + 2.0f, cap.getY() + 0.8f, cap.getRight() - 2.0f, cap.getY() + 0.8f, 1.0f);
}

} // namespace

// ---------------------------------------------------------------------------

RhythmKey::RhythmKey(const juce::String& name, juce::Colour cap) : juce::Button(name), cap_(cap)
{
    setClickingTogglesState(true);
}

void RhythmKey::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    const bool latched = getToggleState() || down;
    auto r = getLocalBounds().toFloat();

    if (getToggleState())
    {
        // Selected switches glow, as if backlit through the cap.
        for (float spread : { 6.0f, 3.5f, 1.5f })
        {
            g.setColour(cap_.brighter(0.4f).withAlpha(0.22f));
            g.fillRoundedRectangle(r.expanded(spread), 3.0f + spread);
        }
    }

    const auto face = getToggleState() ? cap_.brighter(0.45f) : (highlighted ? cap_.darker(0.05f) : cap_.darker(0.25f));
    drawKeyBody(g, r, face, latched);

    auto legend = r.reduced(3.0f, 4.0f).translated(0.0f, latched ? 1.5f : -1.0f);
    g.setColour(juce::Colour(0xff151515));
    g.setFont(panelFont(9.5f));
    const auto caption = getName() == "Discotheque" ? juce::String("Disco-\ntheque") : getName().replace(" ", "\n");
    g.drawFittedText(caption, legend.toNearestInt(), juce::Justification::centred, 2, 0.8f);
}

// ---------------------------------------------------------------------------

VoiceKey::VoiceKey() : juce::Button({}) {}

void VoiceKey::buttonStateChanged()
{
    if (const bool down = isDown(); down != wasDown_)
    {
        wasDown_ = down;
        if (onPressChanged)
            onPressChanged(down);
    }
}

void VoiceKey::paintButton(juce::Graphics& g, bool highlighted, bool down)
{
    drawKeyBody(g, getLocalBounds().toFloat(), highlighted ? juce::Colour(0xff2a2a2a) : juce::Colour(0xff1c1c1c), down);
}

// ---------------------------------------------------------------------------

RockerSwitch::RockerSwitch() : juce::Button({}) { setClickingTogglesState(true); }

void RockerSwitch::paintButton(juce::Graphics& g, bool, bool)
{
    const bool on = getToggleState();
    auto r = getLocalBounds().toFloat();

    // Chrome bezel.
    juce::ColourGradient bezel(colours::chrome.brighter(0.4f), r.getX(), r.getY(), colours::chrome.darker(0.6f),
                               r.getRight(), r.getBottom(), false);
    g.setGradientFill(bezel);
    g.fillRoundedRectangle(r, 3.0f);
    auto well = r.reduced(3.0f);
    g.setColour(juce::Colours::black);
    g.fillRoundedRectangle(well, 2.0f);

    // Rocker: the pressed half sits low and dark, the other half catches the light.
    auto rocker = well.reduced(2.0f);
    auto top = rocker.removeFromTop(rocker.getHeight() * 0.5f);
    auto bottom = rocker;
    auto shade = [&](juce::Rectangle<float> half, bool pressed) {
        juce::ColourGradient grad(juce::Colour(pressed ? 0xff1a1a1a : 0xff5a5a5a), half.getX(), half.getY(),
                                  juce::Colour(pressed ? 0xff0d0d0d : 0xff262626), half.getX(), half.getBottom(), false);
        g.setGradientFill(grad);
        g.fillRect(half);
    };
    shade(top, on);
    shade(bottom, !on);

    // Pilot light in the upper half.
    const auto pilot = top.withSizeKeepingCentre(top.getWidth() * 0.5f, 4.0f);
    g.setColour(on ? colours::lampOn : juce::Colour(0xff3b2a1c));
    g.fillRoundedRectangle(pilot, 2.0f);
    if (on)
    {
        g.setColour(colours::lampOn.withAlpha(0.3f));
        g.fillRoundedRectangle(pilot.expanded(3.0f), 4.0f);
    }
}

// ---------------------------------------------------------------------------

ToggleSwitch::ToggleSwitch() : juce::Button({}) { setClickingTogglesState(true); }

void ToggleSwitch::paintButton(juce::Graphics& g, bool, bool)
{
    auto r = getLocalBounds().toFloat();
    const auto c = r.getCentre();
    const float nut = juce::jmin(r.getWidth(), r.getHeight()) * 0.28f;

    // Hex nut and the bat handle, pointing up when on.
    juce::ColourGradient nutGrad(colours::chrome.brighter(0.5f), c.x - nut, c.y - nut, colours::chrome.darker(0.7f),
                                 c.x + nut, c.y + nut, false);
    g.setGradientFill(nutGrad);
    g.fillEllipse(juce::Rectangle<float>(nut * 2.0f, nut * 2.0f).withCentre(c));
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawEllipse(juce::Rectangle<float>(nut * 2.0f, nut * 2.0f).withCentre(c), 0.8f);

    const float length = r.getHeight() * 0.42f;
    const float dir = getToggleState() ? -1.0f : 1.0f;
    const juce::Point<float> tip(c.x, c.y + dir * length);
    juce::Path bat;
    bat.startNewSubPath(c);
    bat.lineTo(tip);
    g.setColour(colours::chrome.darker(0.3f));
    g.strokePath(bat, juce::PathStrokeType(nut * 0.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(colours::chrome.brighter(0.6f));
    g.fillEllipse(juce::Rectangle<float>(nut * 1.1f, nut * 1.1f).withCentre(tip));
}

// ---------------------------------------------------------------------------

void Lamp::setLit(bool lit)
{
    if (lit != lit_)
    {
        lit_ = lit;
        repaint();
    }
}

void Lamp::paint(juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced(1.0f);
    const auto c = r.getCentre();
    const float outer = juce::jmin(r.getWidth(), r.getHeight()) * 0.5f;

    juce::ColourGradient bezel(colours::chrome.brighter(0.6f), c.x - outer, c.y - outer, colours::chrome.darker(0.8f),
                               c.x + outer, c.y + outer, false);
    g.setGradientFill(bezel);
    g.fillEllipse(r);

    const float jewel = outer * 0.62f;
    const auto lens = juce::Rectangle<float>(jewel * 2.0f, jewel * 2.0f).withCentre(c);
    juce::ColourGradient glass(lit_ ? colours::lampOn.brighter(0.6f) : juce::Colour(0xff8a8478), c.x - jewel * 0.4f,
                               c.y - jewel * 0.4f, lit_ ? colours::lampOn.darker(0.4f) : juce::Colour(0xff2d2a26),
                               c.x + jewel, c.y + jewel, true);
    g.setGradientFill(glass);
    g.fillEllipse(lens);
    g.setColour(juce::Colours::white.withAlpha(lit_ ? 0.6f : 0.35f));
    g.fillEllipse(lens.withSizeKeepingCentre(jewel * 0.5f, jewel * 0.35f).translated(-jewel * 0.3f, -jewel * 0.35f));
}

// ---------------------------------------------------------------------------

PanelLookAndFeel::PanelLookAndFeel()
{
    setColour(juce::BubbleComponent::backgroundColourId, juce::Colour(0xff111111));
    setColour(juce::BubbleComponent::outlineColourId, colours::chrome.withAlpha(0.6f));
    setColour(juce::TooltipWindow::backgroundColourId, juce::Colour(0xff111111));
    setColour(juce::TooltipWindow::textColourId, colours::panelText);
    setColour(juce::TooltipWindow::outlineColourId, colours::chrome.withAlpha(0.6f));
}

juce::Font PanelLookAndFeel::getSliderPopupFont(juce::Slider&) { return panelFont(13.0f); }

void PanelLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                        float startAngle, float endAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
    const auto c = bounds.getCentre();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);

    if (slider.getProperties().contains("trimmer"))
    {
        // Carbon trimmer pot on the circuit board: white housing, slotted rotor.
        const float r = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.42f;
        g.setColour(juce::Colour(0xffeeeae0));
        g.fillRoundedRectangle(juce::Rectangle<float>(r * 2.3f, r * 2.3f).withCentre(c), r * 0.25f);
        g.setColour(juce::Colour(0xff2c6fb0));
        g.fillEllipse(juce::Rectangle<float>(r * 2.0f, r * 2.0f).withCentre(c));
        g.setColour(juce::Colour(0xff17406a));
        g.drawEllipse(juce::Rectangle<float>(r * 2.0f, r * 2.0f).withCentre(c), 1.0f);
        const auto dir = juce::Point<float>(std::sin(angle), -std::cos(angle));
        const auto perp = juce::Point<float>(dir.y, -dir.x);
        g.setColour(juce::Colour(0xffdce8f5));
        g.drawLine(juce::Line<float>(c - dir * (r * 0.8f), c + dir * (r * 0.8f)), r * 0.28f);
        g.drawLine(juce::Line<float>(c - perp * (r * 0.45f), c + perp * (r * 0.45f)), r * 0.2f);
        return;
    }

    // Chrome knob with a knurled skirt and a black pointer line.
    const float r = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f - 2.0f;
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.fillEllipse(juce::Rectangle<float>(r * 2.0f, r * 2.0f).withCentre(c.translated(0.0f, 2.0f)));

    juce::ColourGradient skirt(juce::Colour(0xfff4f5f6), c.x - r, c.y - r, juce::Colour(0xff6f7378), c.x + r, c.y + r,
                               false);
    skirt.addColour(0.5, juce::Colour(0xffbfc3c8));
    g.setGradientFill(skirt);
    g.fillEllipse(juce::Rectangle<float>(r * 2.0f, r * 2.0f).withCentre(c));

    g.setColour(juce::Colours::black.withAlpha(0.28f));
    for (int i = 0; i < 48; ++i)
    {
        const float a = i * juce::MathConstants<float>::twoPi / 48.0f + angle;
        const auto d = juce::Point<float>(std::sin(a), -std::cos(a));
        g.drawLine(juce::Line<float>(c + d * (r * 0.8f), c + d * r), 0.8f);
    }

    const float cap = r * 0.72f;
    juce::ColourGradient top(juce::Colour(0xffffffff), c.x - cap, c.y - cap, juce::Colour(0xff8b8f95), c.x + cap,
                             c.y + cap, true);
    g.setGradientFill(top);
    g.fillEllipse(juce::Rectangle<float>(cap * 2.0f, cap * 2.0f).withCentre(c));
    g.setColour(juce::Colours::black.withAlpha(0.35f));
    g.drawEllipse(juce::Rectangle<float>(cap * 2.0f, cap * 2.0f).withCentre(c), 0.8f);

    const auto d = juce::Point<float>(std::sin(angle), -std::cos(angle));
    g.setColour(juce::Colour(0xff111111));
    g.drawLine(juce::Line<float>(c + d * (cap * 0.25f), c + d * (r * 0.95f)), 2.4f);
}

// ---------------------------------------------------------------------------

juce::Image makeTolexTexture(int width, int height)
{
    juce::Image img(juce::Image::RGB, width, height, false);
    juce::Random rng(0x7013);
    juce::Image::BitmapData data(img, juce::Image::BitmapData::writeOnly);

    // Coarse bumps (bilinear noise) plus fine grain.
    constexpr int cell = 3;
    const int gw = width / cell + 2, gh = height / cell + 2;
    std::vector<float> grid(static_cast<size_t>(gw * gh));
    for (auto& v : grid)
        v = rng.nextFloat();
    for (int py = 0; py < height; ++py)
        for (int px = 0; px < width; ++px)
        {
            const float fx = static_cast<float>(px) / cell, fy = static_cast<float>(py) / cell;
            const int ix = static_cast<int>(fx), iy = static_cast<int>(fy);
            const float tx = fx - ix, ty = fy - iy;
            auto at = [&](int gx, int gy) { return grid[static_cast<size_t>(gy * gw + gx)]; };
            const float bump = (at(ix, iy) * (1 - tx) + at(ix + 1, iy) * tx) * (1 - ty)
                               + (at(ix, iy + 1) * (1 - tx) + at(ix + 1, iy + 1) * tx) * ty;
            const int v = juce::jlimit(0, 255, static_cast<int>(16 + bump * 14 + rng.nextFloat() * 6));
            data.setPixelColour(px, py, juce::Colour::fromRGB(static_cast<uint8_t>(v), static_cast<uint8_t>(v),
                                                              static_cast<uint8_t>(v)));
        }
    return img;
}

juce::Image makeBrushedMetalTexture(int width, int height)
{
    juce::Image img(juce::Image::RGB, width, height, false);
    juce::Random rng(0xa1u);
    juce::Image::BitmapData data(img, juce::Image::BitmapData::writeOnly);
    std::vector<float> row(static_cast<size_t>(height));
    for (auto& v : row)
        v = rng.nextFloat() * 10.0f - 5.0f;
    for (int py = 0; py < height; ++py)
    {
        float streak = 0.0f;
        for (int px = 0; px < width; ++px)
        {
            streak = streak * 0.97f + (rng.nextFloat() - 0.5f) * 1.2f; // horizontal grain
            const float sheen = 18.0f * std::sin(static_cast<float>(px) / static_cast<float>(width) * 3.0f + 0.6f);
            const int v = juce::jlimit(0, 255, static_cast<int>(196 + row[static_cast<size_t>(py)] + streak + sheen));
            data.setPixelColour(px, py,
                                juce::Colour::fromRGB(static_cast<uint8_t>(v), static_cast<uint8_t>(v),
                                                      static_cast<uint8_t>(juce::jmin(255, v + 3))));
        }
    }
    return img;
}

void drawScrew(juce::Graphics& g, juce::Point<float> c, float radius)
{
    juce::ColourGradient head(colours::chrome.brighter(0.6f), c.x - radius, c.y - radius, colours::chrome.darker(0.9f),
                              c.x + radius, c.y + radius, false);
    g.setGradientFill(head);
    g.fillEllipse(juce::Rectangle<float>(radius * 2.0f, radius * 2.0f).withCentre(c));
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.drawLine(c.x - radius * 0.7f, c.y + radius * 0.2f, c.x + radius * 0.7f, c.y - radius * 0.2f, 1.0f);
}

void drawChromeFrame(juce::Graphics& g, juce::Rectangle<float> outer, float thickness)
{
    juce::ColourGradient grad(juce::Colour(0xffeef0f2), outer.getX(), outer.getY(), juce::Colour(0xff7d8187),
                              outer.getX(), outer.getBottom(), false);
    grad.addColour(0.5, juce::Colour(0xffb4b8bd));
    g.setGradientFill(grad);
    juce::Path frame;
    frame.addRectangle(outer);
    frame.addRectangle(outer.reduced(thickness));
    frame.setUsingNonZeroWinding(false);
    g.fillPath(frame);
    g.setColour(juce::Colours::black.withAlpha(0.5f));
    g.drawRect(outer.reduced(thickness), 1.0f);
}

void drawEmblem(juce::Graphics& g, juce::Rectangle<float> area)
{
    // An original crown emblem in the style of a 1960s instrument badge.
    auto badge = area.removeFromTop(area.getHeight() * 0.72f);
    g.setColour(colours::panelText);
    g.drawRect(badge, 1.6f);
    auto inner = badge.reduced(6.0f);
    const float w = inner.getWidth(), h = inner.getHeight();
    const juce::Colour wedges[] = { colours::latin, colours::american.darker(0.05f), colours::panelText };
    for (int i = 0; i < 3; ++i)
    {
        // Three flared points fanning up from the base, like a crown.
        const float bx = inner.getX() + w * (0.18f + 0.32f * i);
        juce::Path p;
        p.startNewSubPath(bx - w * 0.08f, inner.getBottom());
        p.lineTo(inner.getX() + w * (0.05f + 0.45f * i), inner.getY() + h * (i == 1 ? 0.0f : 0.18f));
        p.lineTo(bx + w * 0.16f, inner.getY() + h * (i == 1 ? 0.22f : 0.38f));
        p.lineTo(bx + w * 0.08f, inner.getBottom());
        p.closeSubPath();
        g.setColour(wedges[i]);
        g.fillPath(p);
    }
    g.setColour(colours::panelText);
    g.fillRect(inner.withTop(inner.getBottom() - 3.0f));

    auto plate = area.withTrimmedTop(3.0f);
    g.setColour(colours::panelText);
    g.fillRect(plate);
    g.setColour(colours::plexi);
    g.setFont(juce::Font(juce::FontOptions(plate.getHeight() * 0.72f, juce::Font::bold | juce::Font::italic)));
    g.drawFittedText("beat queen", plate.toNearestInt(), juce::Justification::centred, 1, 0.7f);
}

void drawSpacedText(juce::Graphics& g, const juce::String& text, juce::Rectangle<float> area, float height)
{
    // Wide letter-spacing, like the RHYTHM KING lettering: letters evenly spread over the area.
    g.setFont(panelFont(height, false));
    const int n = text.length();
    if (n == 0)
        return;
    const float step = area.getWidth() / static_cast<float>(n);
    for (int i = 0; i < n; ++i)
        g.drawText(juce::String::charToString(text[i]),
                   juce::Rectangle<float>(area.getX() + step * i, area.getY(), step, area.getHeight()),
                   juce::Justification::centred);
}

} // namespace beatqueen::ui
