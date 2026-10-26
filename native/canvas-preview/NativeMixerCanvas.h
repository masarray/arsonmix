#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <functional>

// Native, hand-drawn JUCE Component. No HTML/WebView and no audio DSP.
// All public setters and callbacks must be called on the JUCE message thread.
namespace armix::native_canvas
{
struct ChannelState
{
    juce::String name;
    float gainDb = 0.0f;
    float pan = 0.0f;        // -1 left, 0 centre, +1 right
    float meterLDb = -100.0f;
    float meterRDb = -100.0f;
    bool muted = false;
    bool solo = false;
    bool clip = false;
};

class MixerCanvas final : public juce::Component
{
public:
    static constexpr std::size_t inputCount = 12;
    static constexpr std::size_t channelCount = inputCount + 1;
    std::function<void(std::size_t, float)> onGainChanged;
    std::function<void(std::size_t, float)> onPanChanged;
    std::function<void(std::size_t, bool)> onMuteChanged;
    std::function<void(std::size_t, bool)> onSoloChanged;

    MixerCanvas()
    {
        const char* labels[] = { "MIC 1", "MIC 2", "MIC 3", "MIC 4",
                                 "IN 5", "IN 6", "IN 7", "IN 8",
                                 "IN 9", "IN 10", "IN 11", "IN 12", "MASTER" };
        for (std::size_t i = 0; i < channelCount; ++i)
            channels[i].name = labels[i];
        setOpaque (true);
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    // Source-labelled meter samples are supplied by the host. This prototype
    // does not generate decorative/fake audio readings.
    void setMeter (std::size_t i, float leftDb, float rightDb, bool clipped)
    {
        if (i >= channelCount)
            return;
        channels[i].meterLDb = finiteMeter (leftDb);
        channels[i].meterRDb = finiteMeter (rightDb);
        channels[i].clip = clipped;
        repaint (boundsFor (i).toNearestInt());
    }

    void setGainDb (std::size_t i, float db)
    {
        if (i < channelCount && std::isfinite (db))
        {
            channels[i].gainDb = std::clamp (db, minGainDb, maxGainDb);
            repaint (boundsFor (i).toNearestInt());
        }
    }

    void setPan (std::size_t i, float p)
    {
        if (i < channelCount && std::isfinite (p))
        {
            channels[i].pan = std::clamp (p, -1.0f, 1.0f);
            repaint (boundsFor (i).toNearestInt());
        }
    }

    void setMute (std::size_t i, bool muted)
    {
        if (i < channelCount)
        {
            channels[i].muted = muted;
            repaint (boundsFor (i).toNearestInt());
        }
    }

    void setSolo (std::size_t i, bool solo)
    {
        if (i < channelCount)
        {
            channels[i].solo = solo;
            repaint (boundsFor (i).toNearestInt());
        }
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour::fromRGB (14, 18, 23));
        g.setColour (juce::Colour::fromRGB (218, 225, 229));
        g.setFont (15.0f);
        g.drawText ("ArSonMix  /  NATIVE MIXER CANVAS", 16, 8, 390, 28,
                    juce::Justification::centredLeft);
        g.setColour (juce::Colour::fromRGB (119, 139, 150));
        g.setFont (11.0f);
        g.drawText ("JUCE preview  |  UI only  |  meters require a native source",
                    408, 9, getWidth() - 420, 26, juce::Justification::centredRight);
        for (std::size_t i = 0; i < channelCount; ++i)
            paintStrip (g, i);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        const int hit = channelAt (e.position);
        if (hit < 0)
            return;
        activeChannel = hit;
        selected = static_cast<std::size_t> (hit);
        dragMode = DragMode::none;
        const auto i = selected;
        const auto r = boundsFor (i);
        const auto p = panBounds (r);
        const auto f = faderBounds (r);
        const auto mute = buttonBounds (r, false);
        const auto solo = buttonBounds (r, true);

        if (mute.contains (e.position))
        {
            channels[i].muted = !channels[i].muted;
            if (onMuteChanged) onMuteChanged (i, channels[i].muted);
        }
        else if (solo.contains (e.position))
        {
            channels[i].solo = !channels[i].solo;
            if (onSoloChanged) onSoloChanged (i, channels[i].solo);
        }
        else if (p.expanded (0.0f, 6.0f).contains (e.position))
        {
            dragMode = DragMode::pan;
            dragStart = e.position;
            dragStartValue = channels[i].pan;
        }
        else if (f.expanded (19.0f, 13.0f).contains (e.position))
        {
            // Relative dragging: clicking the track NEVER teleports the thumb.
            dragMode = DragMode::gain;
            dragStart = e.position;
            dragStartValue = channels[i].gainDb;
        }
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (activeChannel < 0)
            return;
        const auto i = static_cast<std::size_t> (activeChannel);
        if (dragMode == DragMode::gain)
        {
            const float pixels = std::max (1.0f, faderBounds (boundsFor (i)).getHeight());
            const float candidate = std::clamp (dragStartValue -
                    ((e.position.y - dragStart.y) / pixels) * (maxGainDb - minGainDb),
                    minGainDb, maxGainDb);
            if (std::abs (candidate - channels[i].gainDb) > 0.001f)
            {
                channels[i].gainDb = candidate;
                if (onGainChanged) onGainChanged (i, candidate);
                repaint (boundsFor (i).toNearestInt());
            }
        }
        else if (dragMode == DragMode::pan)
        {
            const auto width = std::max (1.0f, panBounds (boundsFor (i)).getWidth());
            float candidate = std::clamp (dragStartValue +
                ((e.position.x - dragStart.x) / width) * 2.0f, -1.0f, 1.0f);
            if (std::abs (candidate) < 0.035f)
                candidate = 0.0f;
            if (std::abs (candidate - channels[i].pan) > 0.001f)
            {
                channels[i].pan = candidate;
                if (onPanChanged) onPanChanged (i, candidate);
                repaint (boundsFor (i).toNearestInt());
            }
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        activeChannel = -1;
        dragMode = DragMode::none;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        const auto hit = channelAt (e.position);
        if (hit < 0)
            return;
        const auto i = static_cast<std::size_t> (hit);
        const auto r = boundsFor (i);
        if (faderBounds (r).expanded (19.0f, 13.0f).contains (e.position))
        {
            setGainDb (i, 0.0f);
            if (onGainChanged) onGainChanged (i, 0.0f);
        }
        else if (panBounds (r).contains (e.position))
        {
            setPan (i, 0.0f);
            if (onPanChanged) onPanChanged (i, 0.0f);
        }
    }

    void mouseWheelMove (const juce::MouseEvent& e,
                         const juce::MouseWheelDetails& wheel) override
    {
        const int hit = channelAt (e.position);
        if (hit < 0 || wheel.deltaY == 0.0f)
            return;
        const auto i = static_cast<std::size_t> (hit);
        const auto r = boundsFor (i);
        if (! faderBounds (r).expanded (19.0f, 15.0f).contains (e.position))
            return;
        const float newDb = std::clamp (channels[i].gainDb + wheel.deltaY * 2.0f,
                                        minGainDb, maxGainDb);
        setGainDb (i, newDb);
        if (onGainChanged) onGainChanged (i, newDb);
    }

private:
    enum class DragMode { none, gain, pan };
    static constexpr float minGainDb = -60.0f;
    static constexpr float maxGainDb =  10.0f;
    std::array<ChannelState, channelCount> channels {};
    std::size_t selected = 0;
    int activeChannel = -1;
    DragMode dragMode = DragMode::none;
    juce::Point<float> dragStart;
    float dragStartValue = 0.0f;

    static float finiteMeter (float dB) noexcept
    {
        return std::isfinite (dB) ? std::clamp (dB, -100.0f, 6.0f) : -100.0f;
    }

    juce::Rectangle<float> boundsFor (std::size_t i) const noexcept
    {
        const float left = 10.0f, gap = 2.0f, masterWidth = 146.0f;
        const float width = static_cast<float> (getWidth());
        const float usable = std::max (980.0f, width) - 20.0f
                                - masterWidth - inputCount * gap;
        const float stripWidth = usable / static_cast<float> (inputCount);
        const float x = i == inputCount
                     ? left + inputCount * (stripWidth + gap)
                     : left + static_cast<float> (i) * (stripWidth + gap);
        return { x, 45.0f, i == inputCount ? masterWidth : stripWidth,
                 std::max (470.0f, static_cast<float> (getHeight()) - 54.0f) };
    }

    static juce::Rectangle<float> panBounds (juce::Rectangle<float> r) noexcept
    {
        return { r.getX() + 10.0f, r.getY() + 45.0f,
                 r.getWidth() - 20.0f, 17.0f };
    }

    static juce::Rectangle<float> buttonBounds (juce::Rectangle<float> r, bool solo) noexcept
    {
        const float width = (r.getWidth() - 22.0f) * 0.5f;
        return { r.getX() + 8.0f + (solo ? width + 6.0f : 0.0f),
                 r.getY() + 71.0f, width, 25.0f };
    }

    static juce::Rectangle<float> faderBounds (juce::Rectangle<float> r) noexcept
    {
        const float top = r.getY() + 268.0f;
        return { r.getX() + r.getWidth() * 0.49f - 3.0f, top, 6.0f,
                 std::max (80.0f, r.getBottom() - 66.0f - top) };
    }

    static float gainY (juce::Rectangle<float> fader, float gainDb) noexcept
    {
        return fader.getBottom() - (gainDb - minGainDb) /
               (maxGainDb - minGainDb) * fader.getHeight();
    }

    int channelAt (juce::Point<float> p) const noexcept
    {
        for (std::size_t i = 0; i < channelCount; ++i)
            if (boundsFor (i).contains (p)) return static_cast<int> (i);
        return -1;
    }

    static void drawButton (juce::Graphics& g, juce::Rectangle<float> b,
                            const juce::String& text, bool lit, juce::Colour on)
    {
        const auto base = lit ? on : juce::Colour::fromRGB (43, 48, 56);
        g.setGradientFill (juce::ColourGradient (base.brighter (0.12f), b.getTopLeft(),
                             base.darker (0.28f), b.getBottomLeft(), false));
        g.fillRoundedRectangle (b, 2.5f);
        g.setColour (lit ? on.brighter (0.30f) : juce::Colour::fromRGB (91, 97, 105));
        g.drawRoundedRectangle (b.reduced (0.5f), 2.5f, 1.0f);
        g.setColour (juce::Colours::white.withAlpha (lit ? 1.0f : 0.86f));
        g.setFont (10.0f);
        g.drawText (text, b.toNearestInt(), juce::Justification::centred);
    }

    static void drawMeter (juce::Graphics& g, juce::Rectangle<float> b,
                           float level, bool clipped)
    {
        g.setColour (juce::Colour::fromRGB (5, 9, 13));
        g.fillRoundedRectangle (b, 1.2f);
        const float t = std::clamp ((level + 60.0f) / 60.0f, 0.0f, 1.0f);
        const float full = b.getHeight(), bottom = b.getBottom();
        // Independent 0..-60 dB meter axis, NOT the +10..-60 fader axis.
        for (int band = 0; band < 3; ++band)
        {
            const float from = band == 0 ? 0.0f : band == 1 ? 0.72f : 0.94f;
            const float to = band == 0 ? 0.72f : band == 1 ? 0.94f : 1.0f;
            const float visible = std::clamp (t - from, 0.0f, to - from);
            if (visible <= 0.0f) continue;
            const auto colour = band == 0 ? juce::Colour::fromRGB (29, 185, 95)
                              : band == 1 ? juce::Colour::fromRGB (237, 185, 41)
                                          : juce::Colour::fromRGB (229, 67, 67);
            g.setColour (colour);
            g.fillRect (b.getX() + 1.0f, bottom - (from + visible) * full,
                        b.getWidth() - 2.0f, visible * full);
        }
        g.setColour (clipped ? juce::Colour::fromRGB (238, 56, 51)
                             : juce::Colour::fromRGB (60, 71, 79));
        g.fillRect (b.getX(), b.getY() - 7.0f, b.getWidth(), 4.0f);
    }

    void paintStrip (juce::Graphics& g, std::size_t i)
    {
        const auto r = boundsFor (i);
        const auto& ch = channels[i];
        const bool master = i == inputCount;
        const auto accent = master ? juce::Colour::fromRGB (218, 69, 65)
                             : juce::Colour::fromHSV (
                                  std::fmod (0.55f + static_cast<float> (i) * 0.075f, 1.0f),
                                  0.58f, 0.86f, 1.0f);
        g.setGradientFill (juce::ColourGradient (
            juce::Colour::fromRGB (38, 43, 51), r.getTopLeft(),
            juce::Colour::fromRGB (24, 28, 35), r.getBottomLeft(), false));
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (i == selected ? juce::Colour::fromRGB (238, 169, 60)
                                   : juce::Colour::fromRGB (72, 81, 91));
        g.drawRoundedRectangle (r.reduced (0.75f), 3.0f, i == selected ? 1.5f : 0.8f);

        g.setColour (accent);
        g.fillRect (r.getX() + 2, r.getY() + 2, r.getWidth() - 4, 3.0f);

        // Top dB readout and pan are separately controlled.
        g.setColour (juce::Colour::fromRGB (9, 14, 18));
        g.fillRoundedRectangle (r.getX() + 8, r.getY() + 10, r.getWidth() - 16, 28, 2.5f);
        g.setColour (juce::Colour::fromRGB (89, 223, 224));
        g.setFont (14.0f);
        g.drawText (juce::String (ch.gainDb, 1) + " dB",
                    juce::Rectangle<float> {r.getX() + 8, r.getY() + 10,
                                            r.getWidth() - 16, 28}.toNearestInt(),
                    juce::Justification::centred);

        const auto pan = panBounds (r);
        g.setColour (juce::Colour::fromRGB (9, 12, 16));
        g.fillRoundedRectangle (pan, 3.0f);
        g.setColour (juce::Colour::fromRGB (76, 96, 109));
        g.drawLine (pan.getX() + 7, pan.getCentreY(),
                    pan.getRight() - 7, pan.getCentreY(), 2.0f);
        const float centre = pan.getCentreX();
        g.setColour (juce::Colour::fromRGB (97, 132, 142));
        g.drawLine (centre, pan.getY() + 2, centre, pan.getBottom() - 2, 1.0f);
        const float px = centre + ch.pan * (pan.getWidth() * 0.5f - 11.0f);
        g.setColour (juce::Colour::fromRGB (198, 209, 214));
        g.fillRoundedRectangle (px - 4.5f, pan.getY() + 1.0f, 9, pan.getHeight() - 2, 2);

        drawButton (g, buttonBounds (r, false), "MUTE", ch.muted,
                    juce::Colour::fromRGB (177, 45, 48));
        drawButton (g, buttonBounds (r, true), "SOLO", ch.solo,
                    juce::Colour::fromRGB (182, 144, 26));

        // Six explicitly unassigned native FX placeholders. Do not pretend
        // they route audio; real inserts require the host's DSP/graph adapter.
        g.setFont (9.0f);
        for (int slot = 0; slot < 6; ++slot)
        {
            const juce::Rectangle<float> fx (r.getX() + 8, r.getY() + 109.0f + slot * 24.0f,
                                             r.getWidth() - 16, 21);
            g.setColour (juce::Colour::fromRGB (17, 27, 37));
            g.fillRoundedRectangle (fx, 2.0f);
            g.setColour (juce::Colour::fromRGB (47, 67, 82));
            g.drawRoundedRectangle (fx, 2.0f, 0.7f);
            g.setColour (juce::Colour::fromRGB (127, 146, 160));
            g.drawText ("FX " + juce::String (slot + 1) + "  EMPTY",
                        fx.toNearestInt(), juce::Justification::centred);
        }

        const auto f = faderBounds (r);
        g.setColour (juce::Colour::fromRGB (8, 10, 13));
        g.fillRoundedRectangle (f.expanded (3, 0), 3.0f);
        // Fader labels: physical dB position aligns with cap's black index line.
        for (float db : { 10.0f, 0.0f, -10.0f, -20.0f, -30.0f,
                          -40.0f, -50.0f, -60.0f })
        {
            const float y = gainY (f, db);
            g.setColour (juce::Colour::fromRGB (113, 125, 132));
            g.drawLine (f.getRight() + 8, y, f.getRight() + 13, y, 1.0f);
            g.setFont (9.5f);
            g.drawText (juce::String (static_cast<int> (db)),
                        juce::Rectangle<int> (juce::roundToInt (f.getRight() + 16),
                                              juce::roundToInt (y - 7), 24, 14),
                        juce::Justification::centredLeft);
        }

        const float capY = gainY (f, ch.gainDb);
        const juce::Rectangle<float> cap (f.getCentreX() - 15.0f, capY - 11.0f, 30, 22);
        const auto capBase = master ? juce::Colour::fromRGB (201, 76, 70)
                                    : juce::Colour::fromRGB (176, 189, 199);
        g.setGradientFill (juce::ColourGradient (capBase.brighter (0.26f), cap.getTopLeft(),
                                                 capBase.darker (0.43f),
                                                 cap.getBottomLeft(), false));
        g.fillRoundedRectangle (cap, 2.7f);
        g.setColour (juce::Colour::fromRGB (34, 39, 44));
        g.drawRoundedRectangle (cap, 2.7f, 1.0f);
        g.setColour (juce::Colour::fromRGB (15, 19, 21));
        g.fillRect (cap.getX() + 2, capY - 1, cap.getWidth() - 4, 2.0f);

        // Independent vertical meter lane (0..-60 dB; no shared scale).
        const juce::Rectangle<float> meter (r.getX() + 8, f.getY(), master ? 10.0f : 11.0f,
                                            f.getHeight());
        drawMeter (g, meter, ch.meterLDb, ch.clip);
        if (master)
            drawMeter (g, meter.translated (13.0f, 0), ch.meterRDb, ch.clip);
        g.setColour (juce::Colour::fromRGB (110, 124, 131));
        g.setFont (9.0f);
        g.drawText (master ? "L  R" : "VU", r.getX() + 5, f.getY() - 17, 40, 13,
                    juce::Justification::centredLeft);
        g.setColour (juce::Colour::fromRGB (110, 124, 131));
        g.drawText ("0", juce::roundToInt (meter.getX()), juce::roundToInt (meter.getY() - 27),
                    20, 11, juce::Justification::centredLeft);
        g.drawText ("-60", juce::roundToInt (meter.getX()),
                    juce::roundToInt (meter.getBottom() + 6), 30, 11,
                    juce::Justification::centredLeft);

        const juce::Rectangle<float> footer (r.getX() + 3, r.getBottom() - 42,
                                              r.getWidth() - 6, 35);
        g.setColour (accent.darker (0.45f));
        g.fillRoundedRectangle (footer, 2.0f);
        g.setColour (juce::Colours::white);
        g.setFont (12.0f);
        g.drawFittedText (ch.name, footer.toNearestInt(),
                          juce::Justification::centred, 1);
    }
};
} // namespace armix::native_canvas
