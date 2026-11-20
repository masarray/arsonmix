// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "GraphicEq31Core.h"
#include "../core/RealtimeNativeFxControl.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#if defined(ARMIX_GEQ31_VECTOR_ASSET) && ARMIX_GEQ31_VECTOR_ASSET
#include <BinaryData.h>
#endif
#include <array>
#include <atomic>
#include <cmath>
#include <memory>
#include <limits>
#include <vector>

namespace armix::builtin_fx {

class GraphicEq31Processor final : public juce::AudioProcessor,
                                   public armix::mixer::RealtimeNativeFxControl {
public:
    static constexpr const char* catalogIdentity="armix.builtin.graphic-eq31.v1";
    static constexpr int stateMagic=0x47455131; // GEQ1, independent of PEQ
    GraphicEq31Processor()
        : juce::AudioProcessor(BusesProperties()
              .withInput("Input",juce::AudioChannelSet::stereo(),true)
              .withOutput("Output",juce::AudioChannelSet::stereo(),true)),
          params_(*this,nullptr,"GEQ31",makeParameters()) {
        for(std::size_t i=0;i<GraphicEq31Core::bandCount;++i)
            bandValues_[i]=params_.getRawParameterValue(paramId(i));
        inputValue_=params_.getRawParameterValue("inputDb");
    }

    const juce::String getName() const override {return "Graphic EQ 31";}
    bool acceptsMidi() const override {return false;}
    bool producesMidi() const override {return false;}
    bool isMidiEffect() const override {return false;}
    double getTailLengthSeconds() const override {return 0.;}
    int getNumPrograms() override {return 1;}
    int getCurrentProgram() override {return 0;}
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override {return "Manual";}
    void changeProgramName(int,const juce::String&) override {}
    bool hasEditor() const override {return true;}
    juce::AudioProcessorEditor* createEditor() override;
    bool isBusesLayoutSupported(const BusesLayout& layout) const override {
        return layout.getMainInputChannelSet()==juce::AudioChannelSet::stereo() &&
               layout.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();
    }

    void prepareToPlay(double sampleRate,int /*maxFrames*/) override {
        ready_=core_.prepare(sampleRate);
        if(!ready_)return;
        for(std::size_t i=0;i<bandValues_.size();++i)
            core_.setGain(i,static_cast<double>(bandValues_[i]->load(std::memory_order_relaxed)));
        inputSmooth_.reset(sampleRate,.006);
        inputSmooth_.setCurrentAndTargetValue(dbToGain(inputValue_->load()));
        peak_.store(0.f,std::memory_order_relaxed);
    }
    void releaseResources() override {ready_=false;}
    bool isReady() const noexcept {return ready_;}
    void consumeRealtimeEdits() noexcept override {
        // Parameters are JUCE atomic floats. Coefficients are computed at
        // most once per changed band per block; no allocation or mutex.
        if(!ready_)return;
        for(std::size_t i=0;i<bandValues_.size();++i)
            (void)core_.setGain(i,bandValues_[i]->load(std::memory_order_relaxed));
    }
    void sealForRealtime() noexcept override {published_=true;}
    // No pending control mailbox: JUCE publishes each target atomically.
    bool stateSnapshotQuiescent() const noexcept override {return true;}
    void processBlock(juce::AudioBuffer<float>& pcm,juce::MidiBuffer&) override {
        juce::ScopedNoDenormals noDenormals;
        if(!ready_||pcm.getNumChannels()<2||pcm.getNumSamples()<=0)return;
        consumeRealtimeEdits();
        const auto count=pcm.getNumSamples();
        auto* l=pcm.getWritePointer(0);
        auto* r=pcm.getWritePointer(1);
        inputSmooth_.setTargetValue(dbToGain(inputValue_->load(std::memory_order_relaxed)));
        for(int n=0;n<count;++n) {
            const float g=static_cast<float>(inputSmooth_.getNextValue());
            l[n]*=g;r[n]*=g;
        }
        core_.processStereo(l,r,static_cast<std::size_t>(count));
        float peak=0.f;
        // Output gain was removed from Graphic EQ 31: meter post-EQ PCM,
        // without a hidden output trim altering levels or headroom.
        for(int n=0;n<count;++n)
            peak=std::max(peak,std::max(std::abs(l[n]),std::abs(r[n])));
        peak_.store(peak,std::memory_order_relaxed);
    }
    void processBlock(juce::AudioBuffer<double>&,juce::MidiBuffer&) override {}
    void getStateInformation(juce::MemoryBlock& destination) override {
        // CONTROL thread. Only one fixed-size, versioned state per insert.
        juce::MemoryOutputStream out(destination,false);
        // GEQ1 v2: 31 bands + one input gain, 136 bytes.
        // Legacy GEQ1 v1 (140 bytes) still restores without its output trim.
        out.writeInt(stateMagic);out.writeInt(2);
        for(auto* p:bandValues_)out.writeFloat(p->load(std::memory_order_acquire));
        out.writeFloat(inputValue_->load(std::memory_order_acquire));
    }
    bool restoreUnpublishedState(const void* bytes,int length) {
        constexpr int v1Bytes=8+static_cast<int>(GraphicEq31Core::bandCount+2)*4;
        constexpr int v2Bytes=8+static_cast<int>(GraphicEq31Core::bandCount+1)*4;
        if(published_||!bytes||(length!=v1Bytes&&length!=v2Bytes))return false;
        juce::MemoryInputStream input(bytes,static_cast<std::size_t>(length),false);
        if(input.readInt()!=stateMagic)return false;
        const int version=input.readInt();
        if((version==1&&length!=v1Bytes)||(version==2&&length!=v2Bytes)||
           (version!=1&&version!=2))return false;
        // Validate the entire state, including the deprecated v1 output trim,
        // before touching live parameters. v1 output gain is NOT reapplied.
        std::array<float,GraphicEq31Core::bandCount+1> values{};
        for(auto& value:values) {
            value=input.readFloat();
            if(!std::isfinite(value)||value < -12.f||value>12.f)return false;
        }
        if(version==1) {
            const float discardedOutputDb=input.readFloat();
            if(!std::isfinite(discardedOutputDb)||
               discardedOutputDb < -12.f||discardedOutputDb>12.f)return false;
        }
        if(!input.isExhausted())return false;
        for(std::size_t i=0;i<GraphicEq31Core::bandCount;++i)
            setParameter(paramId(i),values[i]);
        setParameter("inputDb",values[GraphicEq31Core::bandCount]);
        if(ready_)prepareToPlay(core_.sampleRate(),0);
        return true;
    }

    void setStateInformation(const void* bytes,int count) override {
        (void)restoreUnpublishedState(bytes,count);
    }
    juce::AudioProcessorValueTreeState& parameters() noexcept {return params_;}
    float observedPeak() const noexcept {return peak_.load(std::memory_order_relaxed);}
    void setBandFromUi(std::size_t index,float db) {
        if(index<GraphicEq31Core::bandCount&&std::isfinite(db)&&db>=-12.f&&db<=12.f)
            setParameter(paramId(index),db);
    }
    void setFlatFromUi() {
        for(std::size_t i=0;i<GraphicEq31Core::bandCount;++i)
            setBandFromUi(i,0.f);
    }
private:
    static juce::String paramId(std::size_t i) {
        return "band"+juce::String(static_cast<int>(i));
    }
    static juce::AudioProcessorValueTreeState::ParameterLayout makeParameters() {
        juce::AudioProcessorValueTreeState::ParameterLayout result;
        for(std::size_t i=0;i<GraphicEq31Core::bandCount;++i)
            result.add(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID{paramId(i),1},
                juce::String(graphicEq31Labels[i])+" Hz",
                juce::NormalisableRange<float>(-12.f,12.f,.1f),0.f));
        result.add(std::make_unique<juce::AudioParameterFloat>(
            juce::ParameterID{"inputDb",1},"Input Gain",
            juce::NormalisableRange<float>(-12.f,12.f,.1f),0.f));
        return result;
    }
    void setParameter(const juce::String& id,float db) {
        if(auto* p=dynamic_cast<juce::RangedAudioParameter*>(params_.getParameter(id))) {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(db));
            p->endChangeGesture();
        }
    }
    static double dbToGain(float db) noexcept {return std::pow(10.,static_cast<double>(db)/20.);}
    juce::AudioProcessorValueTreeState params_;
    GraphicEq31Core core_;
    std::array<std::atomic<float>*,GraphicEq31Core::bandCount> bandValues_{};
    std::atomic<float>* inputValue_=nullptr;
    juce::SmoothedValue<double,juce::ValueSmoothingTypes::Linear> inputSmooth_;
    std::atomic<float> peak_{0.f};
    bool published_=false,ready_=false;
};

class GraphicEq31Editor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    explicit GraphicEq31Editor(GraphicEq31Processor& processor)
        : juce::AudioProcessorEditor(processor), processor_(processor) {
        setLookAndFeel(&skin_);
        // The approved FX Rack/ChannelId owner still owns this floating editor.
        // Never create a separate WebView, signal-processing path or VST host.
        for (std::size_t i = 0; i < GraphicEq31Core::bandCount; ++i) {
            auto& slider = sliders_[i];
            slider.setComponentID("geq31-band-" + juce::String(static_cast<int>(i)));
            slider.setSliderStyle(juce::Slider::LinearVertical);
            slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
            slider.setDoubleClickReturnValue(true, 0.0);
            slider.setTooltip(juce::String(graphicEq31Labels[i]) +
                              " Hz, -12 to +12 dB; double-click to centre");
            bandSurface_.addAndMakeVisible(slider);
            attachments_.emplace_back(std::make_unique<Attachment>(
                processor_.parameters(), "band" + juce::String(static_cast<int>(i)), slider));
        }
        // The original THUMB is raster/vector artwork embedded in the exact
        // owner's SVG; the JUCE Slider is input/automation, never a new thumb.
        viewport_.setViewedComponent(&bandSurface_, false);
        viewport_.setScrollBarsShown(false, true);
        viewport_.setScrollBarThickness(9);
        addAndMakeVisible(viewport_);

        for (auto* knob : {&input_, &output_}) {
            knob->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            knob->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 65, 18);
            knob->setDoubleClickReturnValue(true, 0.0);
            addAndMakeVisible(*knob);
        }
        input_.setComponentID("geq31-input");
        output_.setComponentID("geq31-output");
        inputAttachment_ = std::make_unique<Attachment>(processor_.parameters(), "inputDb", input_);
        outputAttachment_ = std::make_unique<Attachment>(processor_.parameters(), "outputDb", output_);

        flat_.setButtonText("Flat");
        flat_.onClick = [this] { processor_.setFlatFromUi(); };
        addAndMakeVisible(flat_);
        smile_.setButtonText("Smile");
        smile_.onClick = [this] {
            for (std::size_t i = 0; i < GraphicEq31Core::bandCount; ++i) {
                const auto hz = graphicEq31Hz[i];
                processor_.setBandFromUi(i, hz < 110. ? 3.f : (hz > 4500. ? 2.5f : -1.f));
            }
        };
        addAndMakeVisible(smile_);
        for (int n = 0; n < 3; ++n) {
            const auto id = n;
            jump_[n].setButtonText(n == 0 ? "LOW" : n == 1 ? "MID" : "HIGH");
            jump_[n].onClick = [this, id] {
                viewport_.setViewPosition(id == 0 ? 0 : (id == 1 ? 11 : 22) * bandCellWidth, 0);
            };
            addAndMakeVisible(jump_[n]);
        }
        peakLabel_.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(peakLabel_);
        setResizable(true, true);
        setResizeLimits(900, 470, 1920, 470);
        setSize(1280, 470);
        startTimerHz(20);
    }

    ~GraphicEq31Editor() override {
        stopTimer();
        setLookAndFeel(nullptr);
    }

    void paint(juce::Graphics& g) override {
        const auto width = getWidth();
        g.fillAll(juce::Colour::fromRGB(244, 245, 246));
        g.setColour(juce::Colour::fromRGB(225, 228, 229));
        g.fillRoundedRectangle(8.f, 9.f, static_cast<float>(width - 16), 452.f, 12.f);
        g.setColour(juce::Colour::fromRGB(249, 250, 250));
        g.fillRoundedRectangle(11.f, 11.f, static_cast<float>(width - 22), 449.f, 10.f);
        g.setColour(juce::Colour::fromRGB(78, 85, 88));
        g.setFont(juce::Font(18.f, juce::Font::plain));
        g.drawText("GRAPHIC EQ 31", 30, 19, 260, 28, juce::Justification::centredLeft);
        g.setFont(juce::Font(11.f, juce::Font::plain));
        g.setColour(juce::Colour::fromRGB(143, 151, 154));
        g.drawText("ARSONMIX  /  1/3 OCTAVE  /  20 Hz – 20 kHz", 31, 45, 390, 20,
                   juce::Justification::centredLeft);
        const auto right = width - rightPanelWidth;
        g.setColour(juce::Colour::fromRGB(220, 225, 225));
        g.drawVerticalLine(right, 72.f, 433.f);
        g.setColour(juce::Colour::fromRGB(107, 114, 117));
        g.setFont(juce::Font(12.f, juce::Font::plain));
        g.drawText("OUTPUT GAIN", right + 18, 75, rightPanelWidth - 34, 22,
                   juce::Justification::centred);
        g.drawText("INPUT", right + 20, 279, rightPanelWidth - 40, 20,
                   juce::Justification::centred);
        g.setColour(juce::Colour::fromRGB(155, 161, 164));
        g.setFont(juce::Font(10.f, juce::Font::plain));
        g.drawText("FX power & bypass: ARSONMIX FX RACK", right + 8, 420,
                   rightPanelWidth - 16, 20, juce::Justification::centred);
        g.drawText("31 ORIGINAL SVG FADERS  ·  ±12 dB", 28, 427,
                   juce::jmax(0, right - 42), 20, juce::Justification::centredLeft);
    }

    void resized() override {
        const int right = getWidth() - rightPanelWidth;
        viewport_.setBounds(24, 90, right - 37, 327);
        bandSurface_.setSize(static_cast<int>(GraphicEq31Core::bandCount) * bandCellWidth, 317);
        for (std::size_t i = 0; i < GraphicEq31Core::bandCount; ++i)
            sliders_[i].setBounds(static_cast<int>(i) * bandCellWidth + 4, 0,
                                  bandCellWidth - 8, 305);
        output_.setBounds(right + 31, 102, rightPanelWidth - 62, 160);
        input_.setBounds(right + 68, 298, rightPanelWidth - 136, 92);
        flat_.setBounds(right + 16, 380, 82, 29);
        smile_.setBounds(right + 112, 380, 82, 29);
        const int jumpX = juce::jmax(485, right - 265);
        for (int i = 0; i < 3; ++i)
            jump_[i].setBounds(jumpX + i * 78, 25, 71, 25);
        peakLabel_.setBounds(right + 35, 57, rightPanelWidth - 70, 20);
    }

private:
    // Preserve the owner's authored SVG as an editable DRAWABLE TREE.
    // Parse once per band on the message thread, then move only the existing
    // THUMB Drawable via a cheap affine transform during drag/automation.
    // No raster decode, XML parsing or heap allocation in the drag path.
    class EqSkin final : public juce::LookAndFeel_V4 {
    public:
        EqSkin() {
#if defined(ARMIX_GEQ31_VECTOR_ASSET) && ARMIX_GEQ31_VECTOR_ASSET
            originalSvg_ = juce::String::fromUTF8(BinaryData::slider_eq_grp_svg,
                static_cast<int>(BinaryData::slider_eq_grp_svgSize));
#endif
            lastGain_.fill(std::numeric_limits<float>::quiet_NaN());
            thumbGroups_.fill(nullptr);
            setColour(juce::TextButton::buttonColourId, juce::Colour::fromRGB(232, 236, 237));
            setColour(juce::TextButton::textColourOffId, juce::Colour::fromRGB(78, 85, 88));
        }

        void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                              float, float, float, juce::Slider::SliderStyle,
                              juce::Slider& slider) override {
            const auto id = slider.getComponentID();
            if (!id.startsWith("geq31-band-")) return;
            const int index = id.fromFirstOccurrenceOf("geq31-band-", false, false).getIntValue();
            if (index < 0 || index >= static_cast<int>(GraphicEq31Core::bandCount)) return;
            const auto band = static_cast<std::size_t>(index);
            if (!drawables_[band]) createBandArtwork(band);
            if (auto* art = drawables_[band].get()) {
                const auto gain = static_cast<float>(slider.getValue());
                if (lastGain_[band] != gain && thumbGroups_[band] != nullptr) {
                    // Authored SVG THUMB centre: y~=916, native viewBox 2042.
                    // +/-560px keeps the original 418px thumb inside Track.
                    const float shift = -560.f * juce::jlimit(-12.f, 12.f, gain) / 12.f;
                    thumbGroups_[band]->setTransform(juce::AffineTransform::translation(0.f, shift));
                    lastGain_[band] = gain;
                }
                art->drawWithin(g, {static_cast<float>(x), static_cast<float>(y),
                                   static_cast<float>(width), static_cast<float>(height)},
                                juce::RectanglePlacement::stretchToFit, 1.f);
            } else {
                // No silent surrogate design: broken asset is a visible error.
                g.setColour(juce::Colours::red);
                g.drawFittedText("SVG ERROR", x, y, width, height,
                                 juce::Justification::centred, 2);
            }
        }

        void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                              float position, float startAngle, float endAngle,
                              juce::Slider&) override {
            const auto size = static_cast<float>(juce::jmin(width, height)) - 21.f;
            const float cx = x + width * 0.5f;
            const float cy = y + height * 0.45f;
            const float radius = size * 0.5f;
            const auto ring = juce::Colour::fromRGB(55, 185, 156);
            for (int i = 0; i < 45; ++i) {
                const float a = startAngle + (endAngle - startAngle) * static_cast<float>(i) / 44.f;
                const float cs = std::sin(a), sn = -std::cos(a);
                g.setColour(i <= static_cast<int>(position * 44.f) ? ring :
                            juce::Colour::fromRGB(177, 183, 186));
                g.drawLine(cx + cs * (radius + 3.f), cy + sn * (radius + 3.f),
                           cx + cs * (radius + 9.f), cy + sn * (radius + 9.f), 2.f);
            }
            g.setColour(juce::Colour::fromRGB(218, 220, 220));
            g.fillEllipse(cx - radius + 5.f, cy - radius + 8.f, 2.f * radius - 10.f, 2.f * radius - 10.f);
            g.setGradientFill(juce::ColourGradient(
                juce::Colours::white, cx - radius * .6f, cy - radius * .8f,
                juce::Colour::fromRGB(239, 241, 242), cx + radius, cy + radius, false));
            g.fillEllipse(cx - radius + 7.f, cy - radius + 4.f,
                          2.f * radius - 14.f, 2.f * radius - 14.f);
            g.setColour(ring);
            g.fillEllipse(cx - 3.f, cy - radius * .65f, 6.f, 6.f);
        }

    private:
        void createBandArtwork(std::size_t band) {
            if (originalSvg_.isEmpty()) return;
            auto document = juce::XmlDocument::parse(originalSvg_);
            if (!document) return;
            auto* fader = document->getChildByAttribute("id", "Slider_EQ");
            if (!fader) return;
            auto* labelGroup = fader->getChildByAttribute("id", "Text");
            auto* thumbGroup = fader->getChildByAttribute("id", "THUMB");
            if (!labelGroup || !thumbGroup) return;
            auto* frequency = labelGroup->getChildByAttribute("id", "32");
            if (!frequency) return;
            auto* tspan = frequency->getFirstChildElement();
            if (!tspan) return;
            tspan->deleteAllTextElements();
            tspan->addTextElement(juce::String(graphicEq31Labels[band]));
            frequency->setAttribute("text-anchor", "middle");
            frequency->setAttribute("style", "transform: matrix(4.167,0,0,4.167,237.76,1850.466)");
            auto drawable = juce::Drawable::createFromSVG(*document);
            if (!drawable) return;
            // JUCE 8.0.15 maps SVG group IDs onto Drawable component IDs.
            // findChildWithID searches only DIRECT children; first descend
            // root -> Slider_EQ -> THUMB. This is the untouched author layer.
            auto* authoredFader = drawable->findChildWithID("Slider_EQ");
            auto* authoredThumb = authoredFader ? authoredFader->findChildWithID("THUMB") : nullptr;
            if (!authoredThumb) return;
            thumbGroups_[band] = authoredThumb;
            drawables_[band] = std::move(drawable);
        }

        juce::String originalSvg_;
        std::array<std::unique_ptr<juce::Drawable>, GraphicEq31Core::bandCount> drawables_{};
        std::array<juce::Component*, GraphicEq31Core::bandCount> thumbGroups_{};
        std::array<float, GraphicEq31Core::bandCount> lastGain_{};
    };

    void timerCallback() override {
        const bool clip = processor_.observedPeak() >= .999f;
        peakLabel_.setText(clip ? "CLIP" : "OUTPUT LEVEL", juce::dontSendNotification);
        peakLabel_.setColour(juce::Label::textColourId,
                             clip ? juce::Colour::fromRGB(235, 81, 57) :
                                    juce::Colour::fromRGB(73, 159, 139));
    }
    using Attachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    static constexpr int bandCellWidth = 80;
    static constexpr int rightPanelWidth = 225;
    GraphicEq31Processor& processor_;
    EqSkin skin_;
    juce::Component bandSurface_;
    juce::Viewport viewport_;
    std::array<juce::Slider, GraphicEq31Core::bandCount> sliders_;
    std::vector<std::unique_ptr<Attachment>> attachments_;
    juce::Slider input_, output_;
    std::unique_ptr<Attachment> inputAttachment_, outputAttachment_;
    juce::TextButton flat_, smile_;
    std::array<juce::TextButton, 3> jump_;
    juce::Label peakLabel_;
};
inline juce::AudioProcessorEditor* GraphicEq31Processor::createEditor() {
    return new GraphicEq31Editor(*this);
}
} // namespace armix::builtin_fx
