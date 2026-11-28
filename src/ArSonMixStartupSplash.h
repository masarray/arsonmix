// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <BinaryData.h>

// Native opaque first-paint guard. Both images are embedded and decoded once.
// Background blur, colour grading and vignette are baked into the small JPEG.
// Preserve startup handoff: no runtime convolution, extra timer or disk I/O.
class ArSonMixStartupSplash final : public juce::Component
{
public:
    ArSonMixStartupSplash()
        : background_ (juce::ImageFileFormat::loadFrom (
              BinaryData::arsonmixsplashbackground_jpg,
              static_cast<size_t> (BinaryData::arsonmixsplashbackground_jpgSize))),
          brand_ (juce::ImageFileFormat::loadFrom (
              BinaryData::arsonmix256_png,
              static_cast<size_t> (BinaryData::arsonmix256_pngSize)))
    {
        setName ("ArSonMix loading");
        setOpaque (true);
        setSize (740, 420);
        centreWithSize (740, 420);
        addToDesktop (juce::ComponentPeer::windowHasDropShadow);
        setAlwaysOnTop (true);
        setVisible (true);
    }

    void setStage (int stage)
    {
        // 3 is the caller's failure/dismissal event, not success.
        // Ignore duplicates, stale events and invalid states.
        if (stage > stage_ && stage <= 2)
        {
            stage_ = stage;
            repaint();
        }
    }

    void paint (juce::Graphics& g) override
    {
        // Fill the entire HWND even if an embedded image fails to decode.
        g.fillAll (juce::Colour (0xff0b1823));
        if (getWidth() <= 0 || getHeight() <= 0)
            return;
        if (background_.isValid())
        {
            g.setImageResamplingQuality (juce::Graphics::mediumResamplingQuality);
            g.drawImageWithin (background_, 0, 0, getWidth(), getHeight(),
                               juce::RectanglePlacement::fillDestination);
        }
        constexpr float cardWidth = 620.0f, cardHeight = 306.0f;
        const float width = static_cast<float> (getWidth());
        const float height = static_cast<float> (getHeight());

        const float scale = juce::jmin (1.20f, juce::jmin (
            width / (cardWidth + 24.0f), height / (cardHeight + 24.0f)));
        juce::Graphics::ScopedSaveState restore (g);
        g.addTransform (juce::AffineTransform::scale (scale).translated (
            (width - cardWidth * scale) * 0.5f,
            (height - cardHeight * scale) * 0.5f));

        const juce::Rectangle<float> card (0.0f, 0.0f, cardWidth, cardHeight);
        g.setColour (juce::Colour (0xe90b171f));
        g.fillRoundedRectangle (card, 12.0f);
        g.setColour (juce::Colour (0xff36525b));
        g.drawRoundedRectangle (card.reduced (0.5f), 12.0f, 1.0f);

        // Preserve the source aspect ratio and alpha; never recreate the
        // product artwork from decorative faders or split text.
        if (brand_.isValid())
        {
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImageWithin (brand_, 265, 22, 90, 90,
                               juce::RectanglePlacement::centred);
        }

        g.setColour (juce::Colour (0xffeaf3f7));
        g.setFont (juce::FontOptions (42.0f));
        g.drawText ("ArSonMix", 24, 116, 572, 50, juce::Justification::centred);
        g.setColour (juce::Colour (0xff92aab9));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("LIVE DIGITAL MIXER  /  NATIVE VST3 HOST", 24, 169, 572, 22,
                    juce::Justification::centred);

        const juce::String status = stage_ == 0 ? "Loading mixer interface..."
                                 : stage_ == 1 ? "Initializing audio device..."
                                               : "Opening mixer...";
        g.setColour (juce::Colour (0xffd9ebf2));
        g.setFont (juce::FontOptions (15.0f));
        g.drawText (status, 24, 207, 572, 26, juce::Justification::centred);

        // Aurora Glass Linear: first paint, authenticated GUI handshake,
        // and native startup handoff are three equal *checkpoints*, not
        // measured time or device/session readiness. No timer or fake %.
        drawAuroraProgress (g, stage_);

        g.setFont (juce::FontOptions (11.0f));
        g.setColour (juce::Colour (0xff92aab9));
        g.drawText ("STARTUP CHECKPOINTS", 100, 269, 238, 18,
                    juce::Justification::centredLeft);
        g.drawText (juce::String (stage_ + 1) + " / 3", 350, 269, 170, 18,
                    juce::Justification::centredRight);
    }

private:
    static void drawAuroraProgress (juce::Graphics& g, int stage)
    {
        // Keep a fixed optical centre and a slim hardware-like recessed track.
        const juce::Rectangle<float> track (100.0f, 240.0f, 420.0f, 14.0f);
        const auto inner = track.reduced (3.0f);
        g.setGradientFill (juce::ColourGradient (
            juce::Colour (0xff03090d), track.getX(), track.getY(),
            juce::Colour (0xff26343e), track.getX(), track.getBottom(), false));
        g.fillRoundedRectangle (track, 7.0f);
        g.setColour (juce::Colour (0xff51626e));
        g.drawRoundedRectangle (track.reduced (0.5f), 6.5f, 0.7f);

        const float fraction = static_cast<float> (stage + 1) / 3.0f;
        const auto fill = inner.withWidth (inner.getWidth() * fraction);
        const float radius = juce::jmin (fill.getHeight() * 0.5f, fill.getWidth() * 0.5f);
        juce::ColourGradient aurora (
            juce::Colour (0xff167a91), fill.getX(), fill.getCentreY(),
            juce::Colour (0xffb4efe1), fill.getRight(), fill.getCentreY(), false);
        aurora.addColour (0.35, juce::Colour (0xff20b3bd));
        aurora.addColour (0.70, juce::Colour (0xff59ded0));
        g.setGradientFill (aurora);
        g.fillRoundedRectangle (fill, radius);

        // A single translucent specular overlay; no offscreen blur, shadow
        // bitmap, animation, new compositor or per-frame allocation of images.
        juce::ColourGradient glass (
            juce::Colour (0xb3ffffff), fill.getX(), fill.getY(),
            juce::Colour (0x3d00262a), fill.getX(), fill.getBottom(), false);
        glass.addColour (0.40, juce::Colour (0x1fffffff));
        glass.addColour (0.50, juce::Colour (0x00ffffff));
        g.setGradientFill (glass);
        g.fillRoundedRectangle (fill, radius);
    }

    const juce::Image background_;
    const juce::Image brand_;
    int stage_ = 0;
};
