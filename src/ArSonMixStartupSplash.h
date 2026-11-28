// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <BinaryData.h>

// Native opaque first-paint guard. The official product PNG is embedded in
// ARMixAssets and decoded once, on construction (never in paint or audio).
// Keep the existing startup stage/handoff contract; no new timer or disk I/O.
class ArSonMixStartupSplash final : public juce::Component
{
public:
    ArSonMixStartupSplash()
        : brand_ (juce::ImageFileFormat::loadFrom (
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
        // The guard can expand to the full mixer HWND. Keep the entire
        // backdrop opaque, but centre the compact composition as ONE unit.
        g.fillAll (juce::Colour (0xff0b1823));
        constexpr float cardWidth = 448.0f, cardHeight = 312.0f;
        const float width = static_cast<float> (getWidth());
        const float height = static_cast<float> (getHeight());
        if (width <= 0.0f || height <= 0.0f)
            return;

        const float scale = juce::jmin (1.0f, juce::jmin (
            width / (cardWidth + 24.0f), height / (cardHeight + 24.0f)));
        juce::Graphics::ScopedSaveState restore (g);
        g.addTransform (juce::AffineTransform::scale (scale).translated (
            (width - cardWidth * scale) * 0.5f,
            (height - cardHeight * scale) * 0.5f));

        const juce::Rectangle<float> card (0.0f, 0.0f, cardWidth, cardHeight);
        g.setColour (juce::Colour (0xff101f2b));
        g.fillRoundedRectangle (card, 10.0f);
        g.setColour (juce::Colour (0xff263b48));
        g.drawRoundedRectangle (card.reduced (0.5f), 10.0f, 1.0f);

        // Preserve the source aspect ratio and alpha; never recreate the
        // product artwork from decorative faders or split text.
        if (brand_.isValid())
        {
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImageWithin (brand_, 174, 24, 100, 100,
                               juce::RectanglePlacement::centred);
        }

        g.setColour (juce::Colour (0xffeaf3f7));
        g.setFont (juce::FontOptions (34.0f));
        g.drawText ("ArSonMix", 24, 136, 400, 40, juce::Justification::centred);
        g.setColour (juce::Colour (0xff92aab9));
        g.setFont (juce::FontOptions (13.0f));
        g.drawText ("LIVE DIGITAL MIXER  /  NATIVE VST3 HOST", 24, 180, 400, 20,
                    juce::Justification::centred);

        const juce::String status = stage_ == 0 ? "Loading mixer interface..."
                                 : stage_ == 1 ? "Preparing native audio engine..."
                                               : "Mixer interface ready";
        g.setColour (juce::Colour (0xffd9ebf2));
        g.setFont (juce::FontOptions (15.0f));
        g.drawText (status, 24, 225, 400, 24, juce::Justification::centred);

        // Three discrete startup milestones, not a fabricated percentage.
        // Completed / active / pending are distinct; no animation or timer.
        for (int i = 0; i < 3; ++i)
        {
            g.setColour (juce::Colour (i < stage_ ? 0xff128f86
                                    : i == stage_ ? 0xff1edac2 : 0xff283c49));
            g.fillRoundedRectangle (70.0f + static_cast<float> (i) * 104.0f,
                                    262.0f, 100.0f, 4.0f, 2.0f);
        }
    }

private:
    const juce::Image brand_;
    int stage_ = 0;
};
