// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

// Lightweight native first-paint: inspired by the approved ArSonMix loading
// reference, deliberately vector-only. No WebView2, image decode, animation,
// blocking timer, disk access or per-frame GPU filters. Owning app paints this
// before constructing the browser/device engine on its next message-loop tick.
class ArSonMixStartupSplash final : public juce::Component
{
public:
    ArSonMixStartupSplash()
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
        stage_ = juce::jlimit (0, 2, stage);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        // Full HWND dark guard + centered, fixed-cost 740×420 vector artwork.
        // Never reveal the browser's intermediate unpainted region.
        g.fillAll (juce::Colour (0xff0b1823));
        juce::Graphics::ScopedSaveState restore (g);
        g.addTransform (juce::AffineTransform::translation (
            float (getWidth() - 740) * 0.5f, float (getHeight() - 420) * 0.5f));
        const int width = 740, height = 420;
        g.setGradientFill (juce::ColourGradient (
            juce::Colour (0xff0c2332), 0.0f, 0.0f,
            juce::Colour (0xff030c15), (float) width, (float) height, false));
        g.fillAll();
        // Mixer strips at the sides are deliberately low-contrast vectors,
        // not blurry images or animated native/WebView layers.
        for (int side = 0; side != 2; ++side)
            for (int i = 0; i != 3; ++i)
            {
                const int x = side == 0 ? 13 + i * 51 : width - 59 - i * 51;
                const auto strip = juce::Rectangle<float> ((float) x, 54.0f, 42.0f, 288.0f);
                g.setColour (juce::Colour (0xff122c3b).withAlpha (0.55f));
                g.fillRoundedRectangle (strip, 3.0f);
                g.setColour (juce::Colour (0xff244657).withAlpha (0.34f));
                g.drawRoundedRectangle (strip, 3.0f, 1.0f);
                g.setColour (juce::Colour (0xff030b10));
                g.fillRoundedRectangle ((float)x + 8.0f, 104.0f, 7.0f, 174.0f, 2.0f);
                for (int j = 0; j < 16; ++j)
                {
                    g.setColour (juce::Colour (j < 12 ? 0xff18c886 : 0xffdfb55c)
                                     .withAlpha (0.12f));
                    g.fillRect (x + 9, 272 - j * 9, 5, 5);
                }
                g.setColour (juce::Colour (0xff839ba8).withAlpha (0.22f));
                g.fillRoundedRectangle ((float) x + 21.0f, 183.0f, 16.0f, 25.0f, 3.0f);
            }

        g.setColour (juce::Colour (0xff12bed4));
        g.drawHorizontalLine (366, 26.0f, (float)width - 26.0f);
        g.setColour (juce::Colour (0xff09d5d5));
        g.setFont (juce::FontOptions (44.0f));
        g.drawText ("Ar", 194, 73, 77, 58, juce::Justification::centredRight);
        g.setColour (juce::Colour (0xffeaf3f7));
        g.drawText ("SonMix", 269, 73, 274, 58, juce::Justification::centredLeft);
        g.setColour (juce::Colour (0xff87a8bd));
        g.setFont (juce::FontOptions (15.0f));
        g.drawText ("N A T I V E   V S T 3   F X   H O S T", 176, 132, 388, 30,
                    juce::Justification::centred);

        // Static equalizer glyph: a 5-bar detail from the reference image.
        for (int i = 0; i < 5; ++i)
        {
            g.setColour (juce::Colour (0xff1edac2));
            const int height = 8 + (2 - std::abs (2 - i)) * 8;
            g.fillRoundedRectangle ((float)(355 + i * 8), (float)(183 - height / 2),
                                    5.0f, (float)height, 1.5f);
        }

        g.setColour (juce::Colour (0xffd9ebf2));
        g.setFont (juce::FontOptions (17.0f));
        const juce::String status = stage_ == 0 ? "Loading mixer interface..."
                                 : stage_ == 1 ? "Preparing native audio engine..."
                                               : "Mixer interface ready";
        g.drawText (status, 145, 213, 450, 28, juce::Justification::centred);
        g.setColour (juce::Colour (0xff283c49));
        g.fillRoundedRectangle (179.0f, 255.0f, 382.0f, 9.0f, 4.0f);
        g.setColour (juce::Colour (0xff17d7b1));
        g.fillRoundedRectangle (179.0f, 255.0f, stage_ == 0 ? 105.0f :
                                stage_ == 1 ? 273.0f : 382.0f, 9.0f, 4.0f);
        const juce::String labels[] = {
            "Loading interface", "Preparing audio engine", "Ready mixer layout"
        };
        for (int i = 0; i < 3; ++i)
        {
            const int y = 282 + i * 24;
            g.setColour (juce::Colour (i <= stage_ ? 0xff16dbb0 : 0xff506878));
            g.drawEllipse (229.0f, (float)y + 5.0f, 11.0f, 11.0f, 1.3f);
            if (i < stage_)
                g.fillEllipse (232.0f, (float)y + 8.0f, 5.0f, 5.0f);
            g.setColour (juce::Colour (i <= stage_ ? 0xffe1f1f6 : 0xff819aac));
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (labels[i], 251, y, 286, 23, juce::Justification::centredLeft);
        }
        g.setColour (juce::Colour (0xff8aaabb));
        g.setFont (juce::FontOptions (11.0f));
        g.drawText ("ArSonMix  |  Native VST3 FX Host", 25, 380, 350, 24,
                    juce::Justification::centredLeft);
        g.drawText ("Initializing locally", 470, 380, 245, 24,
                    juce::Justification::centredRight);
    }

private:
    int stage_ = 0;
};
