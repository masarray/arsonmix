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
#include <functional>
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
        // Graphic EQ 31 has only the 31 band controls. No invisible
        // input/output gain, and no extra multiplier in the realtime path.
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
        // GEQ1 v3: 31 band gains, no hidden trim (132 bytes).
        // Restore supports legacy v1/v2 and discards old input/output trims.
        out.writeInt(stateMagic);out.writeInt(3);
        for(auto* p:bandValues_)out.writeFloat(p->load(std::memory_order_acquire));
    }
    bool restoreUnpublishedState(const void* bytes,int length) {
        constexpr int bandBytes=static_cast<int>(GraphicEq31Core::bandCount)*4;
        constexpr int v1Bytes=8+bandBytes+8; // band + input + output
        constexpr int v2Bytes=8+bandBytes+4; // band + input
        constexpr int v3Bytes=8+bandBytes;   // band only
        if(published_||!bytes||
           (length!=v1Bytes&&length!=v2Bytes&&length!=v3Bytes))return false;
        juce::MemoryInputStream input(bytes,static_cast<std::size_t>(length),false);
        if(input.readInt()!=stateMagic)return false;
        const int version=input.readInt();
        if((version==1&&length!=v1Bytes)||
           (version==2&&length!=v2Bytes)||
           (version==3&&length!=v3Bytes)||
           (version!=1&&version!=2&&version!=3))return false;
        std::array<float,GraphicEq31Core::bandCount> bands{};
        for(auto& value:bands) {
            value=input.readFloat();
            if(!std::isfinite(value)||value < -12.f||value>12.f)return false;
        }
        // Strictly validate legacy trims before any state mutation, then
        // discard them. Hidden gain must NEVER survive a UI migration.
        for(int i=0;i<(version==1?2:version==2?1:0);++i) {
            const auto obsoleteTrim=input.readFloat();
            if(!std::isfinite(obsoleteTrim)||
               obsoleteTrim < -12.f||obsoleteTrim>12.f)return false;
        }
        if(!input.isExhausted())return false;
        for(std::size_t i=0;i<GraphicEq31Core::bandCount;++i)
            setParameter(paramId(i),bands[i]);
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
        return result;
    }
    void setParameter(const juce::String& id,float db) {
        if(auto* p=dynamic_cast<juce::RangedAudioParameter*>(params_.getParameter(id))) {
            p->beginChangeGesture();
            p->setValueNotifyingHost(p->convertTo0to1(db));
            p->endChangeGesture();
        }
    }
    juce::AudioProcessorValueTreeState params_;
    GraphicEq31Core core_;
    std::array<std::atomic<float>*,GraphicEq31Core::bandCount> bandValues_{};
    std::atomic<float> peak_{0.f};
    bool published_=false,ready_=false;
};

class GraphicEq31Editor final : public juce::AudioProcessorEditor, private juce::Timer {
public:
    // Readback from the existing mixer-authoritative global/rack/insert gates.
    struct HostBypassState {
        bool valid=false;
        bool slotBypassed=false;
        bool rackBypassed=false;
        bool globalBypassed=false;
    };
    void bindHostPower(std::function<bool(bool)> request,
                       std::function<HostBypassState()> readback) {
        requestBypass_=std::move(request);
        readHostPower_=std::move(readback);
        refreshHostPower();
    }
    void synchronizeHostPower() {refreshHostPower();}

public:
    explicit GraphicEq31Editor(GraphicEq31Processor& processor)
        : juce::AudioProcessorEditor(processor), processor_(processor) {
        setLookAndFeel(&skin_);
#if defined(ARMIX_GEQ31_VECTOR_ASSET) && ARMIX_GEQ31_VECTOR_ASSET
        // Decode supplied genuine brushed-aluminium texture ONCE on UI init.
        brushedMetal_=juce::ImageCache::getFromMemory(
            BinaryData::geq31_brushed_metal_jpg,
            BinaryData::geq31_brushed_metal_jpgSize);
#endif
        // The existing ArSonMix rack/ChannelId owns this JUCE editor window.
        // The owner's original grouped SVG provides EVERY track and thumb.
        for(std::size_t i=0;i<GraphicEq31Core::bandCount;++i) {
            auto& slider=sliders_[i];
            slider.setComponentID("geq31-band-"+juce::String(static_cast<int>(i)));
            slider.setSliderStyle(juce::Slider::LinearVertical);
            slider.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);
            slider.setDoubleClickReturnValue(true,0.);
            slider.setTooltip(juce::String(graphicEq31Labels[i])+
                " Hz  |  -12 ... +12 dB  |  Double-click: 0 dB");
            slider.onValueChange=[this]{
                if(!applyingPreset_)
                    preset_.setSelectedId(1,juce::dontSendNotification);
            };
            bandSurface_.addAndMakeVisible(slider);
            attachments_.emplace_back(std::make_unique<Attachment>(
                processor_.parameters(),"band"+juce::String(static_cast<int>(i)),slider));
        }
        viewport_.setViewedComponent(&bandSurface_,false);
        viewport_.setScrollBarsShown(false,true);
        viewport_.setScrollBarThickness(7);
        addAndMakeVisible(viewport_);

        // Same compact built-in FX header language as Parametric EQ /
        // Compressor: a selectable preset, host-powered bypass and Close.
        // Factory presets really apply all 31 JUCE parameters; user library
        // .armixfx remains host-owned and is not falsely offered here.
        preset_.setComponentID("geq31.preset");
        preset_.addItem("Custom",1);
        preset_.addItem("Flat",2);
        preset_.addItem("Vocal Presence",3);
        preset_.addItem("Warm",4);
        preset_.addItem("Speech Clarity",5);
        preset_.setSelectedId(1,juce::dontSendNotification);
        preset_.setTooltip("Graphic EQ 31 factory presets");
        preset_.onChange=[this]{applyFactoryPreset(preset_.getSelectedId());};
        addAndMakeVisible(preset_);

        power_.setComponentID("geq31.host-bypass");
        power_.setButtonText(juce::String::fromUTF8("⏻"));
        power_.setClickingTogglesState(false);
        power_.setEnabled(false); // no mixer lease: no fake power authority
        power_.setTooltip("FX state unavailable");
        power_.onClick=[this]{
            if(!readHostPower_||!requestBypass_)return;
            const auto before=readHostPower_();
            if(!before.valid||before.globalBypassed||before.rackBypassed) {
                refreshHostPower();
                return;
            }
            (void)requestBypass_(!before.slotBypassed);
            refreshHostPower(); // reflect actual accepted host state only
        };
        addAndMakeVisible(power_);

        close_.setComponentID("geq31.close");
        close_.setButtonText(juce::String::fromUTF8("×"));
        close_.setTooltip("Close Graphic EQ 31");
        close_.onClick=[this]{
            if(auto* window=dynamic_cast<juce::DocumentWindow*>(
                   getTopLevelComponent()))
                window->setVisible(false); // retain mixer FX & preset state
        };
        addAndMakeVisible(close_);

        flat_.setComponentID("geq31.flat");
        flat_.setButtonText("Flat");
        flat_.setTooltip("Reset all 31 EQ bands to 0 dB");
        flat_.onClick=[this]{
            applyFactoryPreset(2);
            preset_.setSelectedId(2,juce::dontSendNotification);
        };
        addAndMakeVisible(flat_);
        setResizable(true,true);
        setResizeLimits(850,391,1900,391);
        setSize(1610,391);
        startTimerHz(6); // GUI-only external host readback; no audio work
    }

    ~GraphicEq31Editor() override {stopTimer();setLookAndFeel(nullptr);}

    void paint(juce::Graphics& g) override {
        const int width=getWidth(),height=getHeight();
        // Actual operator-supplied horizontal brushed-aluminium texture.
        // One decode in the editor constructor; one cached Image draw here.
        if(brushedMetal_.isValid())
            g.drawImageWithin(brushedMetal_,0,0,width,height,
                juce::RectanglePlacement::stretchToFit);
        else
            g.fillAll(juce::Colour::fromRGB(160,161,161));
        g.setGradientFill(juce::ColourGradient(
            juce::Colours::white.withAlpha(.045f),0.f,0.f,
            juce::Colours::black.withAlpha(.06f),0.f,static_cast<float>(height),false));
        g.fillRect(getLocalBounds());
        g.setColour(juce::Colour::fromRGB(94,102,106).withAlpha(.72f));
        g.drawRect(getLocalBounds().reduced(1),1);
        g.setColour(juce::Colour::fromRGB(33,46,52));
        g.setFont(inter(17.f));
        g.drawText("Graphic EQ 31",19,13,250,31,juce::Justification::centredLeft);
        g.setFont(inter(10.f));
        g.setColour(juce::Colour::fromRGB(63,80,88));
        g.drawText("ARSONMIX  /  1/3 OCTAVE  /  +/-12 dB",
                   216,22,310,20,juce::Justification::centredLeft);
        g.setColour(juce::Colour::fromRGB(76,90,97).withAlpha(.45f));
        g.drawHorizontalLine(53,0.f,static_cast<float>(width));
        // Quiet right-hand utility rail; leave room for future real controls.
        const int railX=getWidth()-176;
        g.setColour(juce::Colour::fromRGB(60,71,77).withAlpha(.24f));
        g.drawVerticalLine(railX-8,61.f,static_cast<float>(height-14));
        // The THREE long tick paths are authored SVG coordinates.
        // This mapping uses the same height/position as the actual JUCE
        // Slider, avoiding labels that float above/below their ticks.
        const auto majorTickY=[this](float sourceY) {
            return juce::roundToInt(
                static_cast<float>(viewport_.getY())+
                sourceY*static_cast<float>(sliders_[0].getHeight())/2042.f);
        };
        g.setColour(juce::Colour::fromRGB(56,72,80));
        g.setFont(inter(10.f));
        g.drawText("+12",5,majorTickY(208.75f)-8,36,16,
            juce::Justification::centredRight);
        g.drawText("0",5,majorTickY(918.33f)-8,36,16,
            juce::Justification::centredRight);
        g.drawText("-12",5,majorTickY(1627.92f)-8,36,16,
            juce::Justification::centredRight);
    }
    void resized() override {
        constexpr int x0=46,rightRail=176;
        viewport_.setBounds(x0,66,
            juce::jmax(1,getWidth()-x0-rightRail-10),297);
        bandSurface_.setSize(static_cast<int>(
            GraphicEq31Core::bandCount)*bandCellWidth,287);
        for(std::size_t i=0;i<GraphicEq31Core::bandCount;++i)
            sliders_[i].setBounds(static_cast<int>(i)*bandCellWidth,0,
                                  bandCellWidth,286);
        const int r=getWidth()-rightRail;
        flat_.setBounds(r+25,104,126,29);
        close_.setBounds(getWidth()-46,13,30,30);
        power_.setBounds(getWidth()-87,13,33,30);
        preset_.setBounds(getWidth()-298,13,200,30);
    }

private:
    void refreshHostPower() {
        const auto state=readHostPower_?readHostPower_():HostBypassState{};
        const bool active=state.valid&&!state.slotBypassed&&
                          !state.rackBypassed&&!state.globalBypassed;
        power_.setEnabled(state.valid&&requestBypass_&&
                          !state.globalBypassed&&!state.rackBypassed);
        power_.setToggleState(active,juce::dontSendNotification);
        power_.setTooltip(!state.valid?"FX instance unavailable":
            state.globalBypassed?"Global FX bypassed in mixer":
            state.rackBypassed?"FX Rack bypassed in mixer":
            state.slotBypassed?"Enable Graphic EQ 31":"Bypass Graphic EQ 31");
        power_.setColour(juce::TextButton::textColourOffId,
            active?juce::Colour::fromRGB(24,105,89):
                   juce::Colour::fromRGB(87,91,92));
    }
    void timerCallback() override {refreshHostPower();}
    void applyFactoryPreset(int choice) {
        if(choice<2||choice>5)return;
        applyingPreset_=true;
        for(std::size_t i=0;i<GraphicEq31Core::bandCount;++i) {
            const float hz=graphicEq31Hz[i];
            float db=0.f;
            if(choice==3) // Voice presence, subtract mud without hype.
                db=hz<100.f?-2.5f:hz<270.f?-1.5f:
                   hz>=1800.f&&hz<5600.f?2.f:0.f;
            else if(choice==4)
                db=hz<=160.f?2.f:hz>=5000.f?-1.f:0.f;
            else if(choice==5)
                db=hz<=125.f?-3.f:hz>=200.f&&hz<=500.f?-1.f:
                   hz>=1600.f&&hz<=4000.f?2.5f:0.f;
            processor_.setBandFromUi(i,db);
        }
        applyingPreset_=false;
    }
    static juce::Font inter(float size) {return juce::Font("Inter",size,juce::Font::plain);}
    class EqSkin final : public juce::LookAndFeel_V4 {
    public:
        EqSkin() {
#if defined(ARMIX_GEQ31_VECTOR_ASSET) && ARMIX_GEQ31_VECTOR_ASSET
            originalSvg_=juce::String::fromUTF8(BinaryData::slider_eq_grp_svg,
                static_cast<int>(BinaryData::slider_eq_grp_svgSize));
#endif
            lastGain_.fill(std::numeric_limits<float>::quiet_NaN());
            thumbGroups_.fill(nullptr);
            setColour(juce::TextButton::buttonColourId,juce::Colour::fromRGB(231,237,237));
            setColour(juce::TextButton::textColourOffId,juce::Colour::fromRGB(65,77,79));
        }
        juce::Font getTextButtonFont(juce::TextButton&,int) override {
            return GraphicEq31Editor::inter(11.f);
        }
        juce::Font getLabelFont(juce::Label&) override {
            return GraphicEq31Editor::inter(11.f);
        }
        juce::Font getComboBoxFont(juce::ComboBox&) override {
            return GraphicEq31Editor::inter(11.f);
        }
        void drawLinearSlider(juce::Graphics& g,int x,int y,int width,int height,
                              float,float,float,juce::Slider::SliderStyle,
                              juce::Slider& slider) override {
            const auto id=slider.getComponentID();
            if(!id.startsWith("geq31-band-"))return;
            const int index=id.fromFirstOccurrenceOf("geq31-band-",false,false).getIntValue();
            if(index<0||index>=static_cast<int>(GraphicEq31Core::bandCount))return;
            const auto band=static_cast<std::size_t>(index);
            if(!drawables_[band])createBandArtwork(band);
            if(auto* art=drawables_[band].get()) {
                const auto gain=static_cast<float>(slider.getValue());
                if(lastGain_[band]!=gain&&thumbGroups_[band]!=nullptr) {
                    const float shift=thumbShift(gain);
                    thumbGroups_[band]->setTransform(juce::AffineTransform::translation(0.f, shift));
                    lastGain_[band]=gain;
                }
                // LOCAL thumb-only shadow: no per-band rectangular raster.
                // Authored vector THUMB bounds approximately (158..319,
                // 776..1055) in the 60..412 cropped SVG viewBox.
                const float shift=thumbShift(gain);
                const auto shadowBounds=juce::Rectangle<int>(
                    juce::roundToInt(x+width*98.f/352.f),
                    juce::roundToInt(y+(776.f+shift)*height/2042.f),
                    juce::roundToInt(width*161.f/352.f),
                    juce::roundToInt(height*280.f/2042.f));
                juce::DropShadow(juce::Colours::black.withAlpha(.30f),
                    4,{1,3}).drawForRectangle(g,shadowBounds);
                art->drawWithin(g,{static_cast<float>(x),static_cast<float>(y),
                    static_cast<float>(width),static_cast<float>(height)},
                    juce::RectanglePlacement::stretchToFit,1.f);
            }else {
                g.setColour(juce::Colours::red);
                g.drawFittedText("SVG ERROR",x,y,width,height,
                    juce::Justification::centred,2);
            }
        }
    private:
        // Exact ends measured from the owner's original vector SVG paths:
        // slot outer Y=147.08..1696.67, THUMB Y=776.33..1055.50.
        static float thumbShift(float gain) noexcept {
            const float t=juce::jlimit(-12.f,12.f,gain)/12.f;
            return t>=0.f ? -629.25f*t : -641.17f*t;
        }
        void createBandArtwork(std::size_t band) {
            if(originalSvg_.isEmpty())return;
            auto document=juce::XmlDocument::parse(originalSvg_);
            if(!document)return;
            auto* fader=document->getChildByAttribute("id","Slider_EQ");
            if(!fader)return;
            // Crop only empty source margins for readable track geometry.
            document->setAttribute("viewBox","60 0 352 2042");
            auto* labelGroup=fader->getChildByAttribute("id","Text");
            auto* thumbGroup=fader->getChildByAttribute("id","THUMB");
            if(!labelGroup||!thumbGroup)return;
            // The source stays byte-for-byte intact; adjust just this
            // UI-thread SVG clone. Uniform Inter text, same two-line layout
            // and visible Hz/kHz units as equalizer-interface.svg.
            const auto nominal=juce::String(graphicEq31Labels[band]);
            const bool isKhz=nominal.endsWithChar('k');
            const auto number=isKhz?nominal.dropLastCharacters(1):nominal;
            auto* frequency=labelGroup->getChildByAttribute("id","32");
            auto* tspan=frequency?frequency->getFirstChildElement():nullptr;
            auto* unitGroup=labelGroup->getChildByAttribute("id","Hz");
            auto* unitText=unitGroup?unitGroup->getFirstChildElement():nullptr;
            auto* unitSpan=unitText?unitText->getFirstChildElement():nullptr;
            if(!tspan||!unitSpan)return;
            tspan->deleteAllTextElements();
            tspan->addTextElement(number);
            tspan->setAttribute("style",
                "font-family: Inter; font-size: 27px; font-weight: 700; fill: #344047");
            frequency->setAttribute("text-anchor","middle");
            frequency->setAttribute("style",
                "transform: matrix(4.167,0,0,4.167,237.76,1850.466); font-family: Inter");
            unitSpan->deleteAllTextElements();
            unitSpan->addTextElement(isKhz?"kHz":"Hz");
            unitSpan->setAttribute("style",
                "font-family: Inter; font-size: 18px; font-weight: 600; fill: #5b6a72");
            unitText->setAttribute("text-anchor","middle");
            unitText->setAttribute("style",
                "transform: matrix(4.167,0,0,4.167,237.76,1933.799); font-family: Inter");

            auto* trackGroup=fader->getChildByAttribute("id","Track");
            if(!trackGroup)return;
            // Original small coloured endpoint dots add no signal meaning.
            for(int j=trackGroup->getNumChildElements()-1;j>=0;--j) {
                auto* node=trackGroup->getChildElement(j);
                if(node->getChildByAttribute("class","s5")||
                   node->getChildByAttribute("class","s6"))
                    trackGroup->removeChildElement(node,true);
            }
            // SVG Track path geometry is preserved. Use prominent black ticks
            // on BOTH sides; change only this UI XML clone, not owner source.
            for(int row=0;row<2;++row) {
                if(auto* tickGroup=trackGroup->getChildElement(row)) {
                    for(int p=0;p<tickGroup->getNumChildElements();++p) {
                        auto* tick=tickGroup->getChildElement(p);
                        if(tick->hasTagName("path")&&
                           tick->getStringAttribute("class")=="s0")
                            tick->setAttribute("style",
                              "fill:none;stroke:#131a1d;stroke-width:7.9;"
                              "stroke-linecap:butt;stroke-miterlimit:10");
                    }
                }
            }
            // Recessed outer lip: the original SVG g1 gradient is visually
            // lighter than the aluminium surface, flattening the channel.
            // Tint the UI clone only, to a darker chamfer around the hole.
            if(auto* defs=document->getChildByName("defs")) {
                if(auto* lip=defs->getChildByAttribute("id","g1")) {
                    const char* shades[]={"#929b9f","#a6afb3",
                                          "#969ea3","#858d92"};
                    for(int j=0;j<lip->getNumChildElements();++j)
                        lip->getChildElement(j)->setAttribute(
                            "stop-color",shades[juce::jmin(j,3)]);
                }
            }
            // Deep recessed black hardware slot (existing SVG g2 gradient).
            if(auto* defs=document->getChildByName("defs")) {
                if(auto* gradient=defs->getChildByAttribute("id","g2")) {
                    const char* shades[]={"#080c0f","#12181c",
                                          "#1b2126","#20272b","#242a2d"};
                    for(int j=0;j<gradient->getNumChildElements();++j)
                        gradient->getChildElement(j)->setAttribute(
                            "stop-color",shades[juce::jmin(j,4)]);
                }
            }
            // Restore the OWNERS' AUTHORED raster _Image3 shadow,
            // inside THUMB. No synthetic shadow graphic or flattened image.
            // Only its opacity is slightly moderated to fit the tight pitch.
            if(auto* rasterShadow=thumbGroup->getFirstChildElement()) {
                if(rasterShadow->getChildByAttribute("id","_Image3"))
                    rasterShadow->setAttribute("opacity",0.87);
            }
            auto drawable=juce::Drawable::createFromSVG(*document);
            if(!drawable)return;
            auto* authoredFader=drawable->findChildWithID("Slider_EQ");
            auto* authoredThumb=authoredFader?authoredFader->findChildWithID("THUMB"):nullptr;
            if(!authoredThumb)return;
            thumbGroups_[band]=authoredThumb;
            drawables_[band]=std::move(drawable);
        }
        juce::String originalSvg_;
        std::array<std::unique_ptr<juce::Drawable>,GraphicEq31Core::bandCount> drawables_{};
        std::array<juce::Component*,GraphicEq31Core::bandCount> thumbGroups_{};
        std::array<float,GraphicEq31Core::bandCount> lastGain_{};
    };

    using Attachment=juce::AudioProcessorValueTreeState::SliderAttachment;
    static constexpr int bandCellWidth=50;
    GraphicEq31Processor& processor_;
    EqSkin skin_;
    juce::Image brushedMetal_;
    juce::Component bandSurface_;
    juce::Viewport viewport_;
    std::array<juce::Slider,GraphicEq31Core::bandCount> sliders_;
    std::vector<std::unique_ptr<Attachment>> attachments_;
    juce::ComboBox preset_;
    juce::TextButton flat_,power_,close_;
    std::function<bool(bool)> requestBypass_;
    std::function<HostBypassState()> readHostPower_;
    bool applyingPreset_=false;
};
inline juce::AudioProcessorEditor* GraphicEq31Processor::createEditor() {
    return new GraphicEq31Editor(*this);
}
} // namespace armix::builtin_fx
