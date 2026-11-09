// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace armix::ui
{
// Only the main JUCE mixer window uses this caption. No WebView2 command
// round trips for minimise, maximise, close, drag or OS window resizing.
class MixerCaptionButton final : public juce::Button
{
public:
    explicit MixerCaptionButton (int kind) : Button ("ArSonMiX window control"), kind_ (kind)
    {
        // Exact operator-provided SVG path geometry, in its native 24x24 coordinates.
        icon_ = load (kind == juce::DocumentWindow::closeButton
            ? "M6.4,19L5,17.6L10.6,12L5,6.4L6.4,5L12,10.6L17.6,5L19,6.4L13.4,12L19,17.6L17.6,19L12,13.4L6.4,19Z"
            : kind == juce::DocumentWindow::minimiseButton
            ? "M6,21V19H18v2H6Z"
            : "M3,21V3H21V21H3Zm2-2H19V5H5V19Zm0,0V5V19Z");
        if (kind == juce::DocumentWindow::maximiseButton)
            restoreIcon_ = load ("M8,3H21V16H18V6H8V3ZM3,8H16V21H3V8ZM6,11V18H13V11H6Z");
        setTooltip (kind == juce::DocumentWindow::closeButton ? "Close" :
                    kind == juce::DocumentWindow::minimiseButton ? "Minimise" : "Maximise / restore");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void paintButton (juce::Graphics& g, bool over, bool down) override
    {
        const bool close = kind_ == juce::DocumentWindow::closeButton;
        g.setColour (down ? (close ? juce::Colour (0xffa63742) : juce::Colour (0xff204457))
                          : over ? (close ? juce::Colour (0xffd34b54) : juce::Colour (0xff294a5c))
                                 : juce::Colour (0xff112632));
        g.fillRect (getLocalBounds());
        if (hasKeyboardFocus (true))
        {
            g.setColour (juce::Colour (0xff68d9f2));
            g.drawRect (getLocalBounds().reduced (1), 1);
        }
        auto* parent = findParentComponentOfClass<juce::DocumentWindow>();
        auto* image = kind_ == juce::DocumentWindow::maximiseButton &&
                      parent != nullptr && parent->isFullScreen()
                    ? restoreIcon_.get() : icon_.get();
        if (image != nullptr)
        {
            const auto b = getLocalBounds().toFloat();
            image->drawWithin (g, { b.getCentreX() - 7.5f, b.getCentreY() - 7.5f, 15.0f, 15.0f },
                               juce::RectanglePlacement::centred, isEnabled() ? 1.0f : 0.45f);
        }
    }

private:
    static std::unique_ptr<juce::Drawable> load (const char* path)
    {
        juce::String source ("<svg xmlns=\"http://www.w3.org/2000/svg\" viewBox=\"0 0 24 24\"><path fill=\"#dcecf2\" d=\"");
        source << path << "\"/></svg>";
        if (auto xml = juce::XmlDocument::parse (source))
            return juce::Drawable::createFromSVG (*xml);
        return {};
    }
    int kind_;
    std::unique_ptr<juce::Drawable> icon_, restoreIcon_;
};

class MixerCaptionLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    juce::Button* createDocumentWindowButton (int kind) override
    {
        return new MixerCaptionButton (kind);
    }

    void positionDocumentWindowButtons (juce::DocumentWindow&, int x, int y, int w, int h,
                                        juce::Button* minimise, juce::Button* maximise,
                                        juce::Button* close, bool onLeft) override
    {
        constexpr int buttonWidth = 44;
        if (onLeft)
        {
            for (auto* button : { close, minimise, maximise })
                if (button != nullptr) { button->setBounds (x, y, buttonWidth, h); x += buttonWidth; }
        }
        else
        {
            x += w;
            for (auto* button : { close, maximise, minimise })
                if (button != nullptr) { x -= buttonWidth; button->setBounds (x, y, buttonWidth, h); }
        }
    }

    void drawDocumentWindowTitleBar (juce::DocumentWindow& window, juce::Graphics& g,
                                     int w, int h, int titleX, int titleW,
                                     const juce::Image* icon, bool) override
    {
        g.fillAll (juce::Colour (0xff112632));
        g.setColour (juce::Colour (0xff365b6b));
        g.drawHorizontalLine (h - 1, 0.0f, static_cast<float> (w));
        int x = juce::jmax (10, titleX + 9);
        if (icon != nullptr && icon->isValid())
        {
            g.drawImageWithin (*icon, x, (h - 16) / 2, 16, 16,
                               juce::RectanglePlacement::centred, false);
            x += 23;
        }
        g.setFont (juce::Font (juce::FontOptions { 13.0f }));
        const auto alpha = window.isActiveWindow() ? 1.0f : 0.72f;
        auto paintPart = [&] (const char* word, int width, juce::Colour colour)
        {
            g.setColour (colour.withMultipliedAlpha (alpha));
            g.drawText (word, x, 0, width, h, juce::Justification::centredLeft, false);
            x += width;
        };
        paintPart ("Ar", 16, juce::Colour (0xff48d5f3));
        paintPart ("Son", 27, juce::Colour (0xffeff8fa));
        paintPart ("MiX", 27, juce::Colour (0xffffad59));
        const auto remaining = titleX + titleW - x;
        if (remaining > 185)
        {
            g.setColour (juce::Colour (0xff799dac));
            g.setFont (juce::Font (juce::FontOptions { 10.5f }));
            g.drawText ("MIXER  ·  NATIVE SESSION", x + 13, 0,
                        juce::jmin (230, remaining - 16), h,
                        juce::Justification::centredLeft, false);
        }
    }
};
} // namespace armix::ui
