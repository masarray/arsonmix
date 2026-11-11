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
        // Native window caption: semantic, low-glare hover followed by a
        // clearly lit pressed face. No animations, timers, CSS or WebView2.
        // Keep the existing 34 px click target and original SVG geometry.
        if (over || down)
        {
            const auto palette = paletteFor (kind_);
            const auto face = getLocalBounds().toFloat().reduced (2.0f, 3.0f);
            g.setColour (down ? palette.pressedFace : palette.hoverFace);
            g.fillRoundedRectangle (face, 3.0f);
            g.setColour (down ? palette.pressedEdge : palette.hoverEdge);
            g.drawRoundedRectangle (face.reduced (0.5f), 3.0f, 1.0f);
        }
        if (hasKeyboardFocus (true))
        {
            g.setColour (juce::Colour (0xff68d9f2));
            g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.5f, 2.5f), 3.0f, 1.0f);
        }
        auto* parent = findParentComponentOfClass<juce::DocumentWindow>();
        auto* image = kind_ == juce::DocumentWindow::maximiseButton &&
                      parent != nullptr && parent->isFullScreen()
                    ? restoreIcon_.get() : icon_.get();
        if (image != nullptr)
        {
            const auto b = getLocalBounds().toFloat();
            image->drawWithin (g, { b.getCentreX() - 6.0f, b.getCentreY() - 6.0f, 12.0f, 12.0f },
                               juce::RectanglePlacement::centred, isEnabled() ? 1.0f : 0.45f);
        }
    }

private:
    struct CaptionPalette
    {
        juce::Colour hoverFace, hoverEdge, pressedFace, pressedEdge;
    };

    // Each action has one authoritative native palette. A dark hover
    // identifies the action without flashing; a bright press confirms it.
    // Minimise = green, maximise/restore = amber, close = red.
    // Pure value construction: no heap allocation or frame-driven repaint.
    static CaptionPalette paletteFor (int kind) noexcept
    {
        if (kind == juce::DocumentWindow::closeButton)
            return { juce::Colour (0xff5c3039), juce::Colour (0xffab5864),
                     juce::Colour (0xffef3e50), juce::Colour (0xffff8993) };
        if (kind == juce::DocumentWindow::minimiseButton)
            return { juce::Colour (0xff244e3a), juce::Colour (0xff4ead7a),
                     juce::Colour (0xff1fb76b), juce::Colour (0xff8be2b4) };
        // JUCE uses the same button for maximised and restored states;
        // choosing the icon depends on the real DocumentWindow state.
        return { juce::Colour (0xff584929), juce::Colour (0xffbc9544),
                 juce::Colour (0xffe5ae2b), juce::Colour (0xffffdf81) };
    }

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
        constexpr int buttonWidth = 34;
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
        const auto brandFont = juce::Font (juce::FontOptions { 13.0f });
        g.setFont (brandFont);
        const auto alpha = window.isActiveWindow() ? 1.0f : 0.72f;
        auto paintPart = [&] (const juce::String& word, juce::Colour colour)
        {
            // Real glyph advances: "ArSonMiX" is one continuous wordmark.
            const auto advance = juce::GlyphArrangement::getStringWidthInt (brandFont, word);
            g.setColour (colour.withMultipliedAlpha (alpha));
            g.drawText (word, x, 0, advance + 2, h, juce::Justification::centredLeft, false);
            x += advance;
        };
        paintPart ("Ar", juce::Colour (0xff48d5f3));
        paintPart ("Son", juce::Colour (0xffeff8fa));
        paintPart ("MiX", juce::Colour (0xffffad59));
        x += 15;
        const auto remaining = juce::jmax (0, titleX + titleW - x - 9);
        if (remaining > 80)
        {
            // The installed executable owns its version. It is stamped from
            // the verified release tag at build time, never a UI placeholder.
            // Show it first so narrowing the window clips the description,
            // not the operator's release identity.
            const auto subtitleFont = juce::Font (juce::FontOptions { 11.0f });
            g.setFont (subtitleFont);
            auto paintSubtitle = [&] (const juce::String& label, juce::Colour colour)
            {
                const auto available = juce::jmax (0, titleX + titleW - x - 9);
                if (available == 0)
                    return;
                const auto width = juce::jmin (available,
                    juce::GlyphArrangement::getStringWidthInt (subtitleFont, label) + 4);
                g.setColour (colour.withMultipliedAlpha (alpha));
                g.drawText (label, x, 0, width, h, juce::Justification::centredLeft, true);
                x += width;
            };
            paintSubtitle (juce::String ("v") + JUCE_APPLICATION_VERSION_STRING,
                           juce::Colour (0xffe8f6fc));
            // Explicit Unicode code points prevent mojibake in MSVC builds.
            const auto separator = juce::String ("  ") +
                juce::String::charToString ((juce::juce_wchar) 0x00b7) + "  ";
            paintSubtitle (separator + "Open Source Premium Digital Mixer" + separator
                + juce::String::charToString ((juce::juce_wchar) 0x00a9) + " 2026 SonKuPik",
                juce::Colour (0xffc9e1e9));
        }
    }
};
} // namespace armix::ui
