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
        const auto next = juce::jlimit (0, 2, stage);
        if (next != stage_)
        {
            stage_ = next;
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
        constexpr float cardWidth = 620.0f, cardHeight = 372.0f;
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
                                 : stage_ == 1 ? "Preparing native audio engine..."
                                               : "Mixer interface ready";
        g.setColour (juce::Colour (0xffd9ebf2));
        g.setFont (juce::FontOptions (15.0f));
        g.drawText (status, 24, 207, 572, 26, juce::Justification::centred);

        // Three discrete startup milestones, not a fabricated percentage.
        // Completed / active / pending are distinct; no animation or timer.
        for (int i = 0; i < 3; ++i)
        {
            g.setColour (juce::Colour (i < stage_ ? 0xff128f86
                                    : i == stage_ ? 0xff1edac2 : 0xff283c49));
            g.fillRoundedRectangle (100.0f + static_cast<float> (i) * 142.0f,
                                    242.0f, 136.0f, 4.0f, 2.0f);
        }

        const juce::String labels[] = {
            "Loading interface", "Preparing native audio engine", "Mixer interface ready"
        };
        g.setFont (juce::FontOptions (12.5f));
        for (int i = 0; i < 3; ++i)
        {
            const float y = 268.0f + static_cast<float> (i) * 24.0f;
            g.setColour (juce::Colour (i <= stage_ ? 0xff1edac2 : 0xff526671));
            g.drawEllipse (210.0f, y + 3.0f, 10.0f, 10.0f, 1.2f);
            if (i < stage_)
                g.fillEllipse (212.5f, y + 5.5f, 5.0f, 5.0f);
            g.setColour (juce::Colour (i == stage_ ? 0xffe5f1f4
                                    : i < stage_ ? 0xffa8c5c7 : 0xff8297a2));
            g.drawText (labels[i], 230, static_cast<int> (y) - 3, 180, 22,
                        juce::Justification::centredLeft);
        }
    }

private:
    const juce::Image background_;
    const juce::Image brand_;
    int stage_ = 0;
};
