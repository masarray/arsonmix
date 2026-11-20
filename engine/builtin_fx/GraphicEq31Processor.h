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
    explicit GraphicEq31Editor(GraphicEq31Processor& processor)
        : juce::AudioProcessorEditor(processor), processor_(processor) {
        setLookAndFeel(&skin_);
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
            bandSurface_.addAndMakeVisible(slider);
            attachments_.emplace_back(std::make_unique<Attachment>(
                processor_.parameters(),"band"+juce::String(static_cast<int>(i)),slider));
        }
        viewport_.setViewedComponent(&bandSurface_,false);
        viewport_.setScrollBarsShown(false,true);
        viewport_.setScrollBarThickness(7);
        addAndMakeVisible(viewport_);

        // ONE gain control, using the owner's KNOB-EQ.svg (KNOB + POINTER).
        // No output knob, no input/output fader, and no duplicate FX host.
        input_.setComponentID("geq31-input");
        input_.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        input_.setRotaryParameters(juce::degreesToRadians(225.f),
                                   juce::degreesToRadians(495.f),true);
        input_.setTextBoxStyle(juce::Slider::TextBoxBelow,false,82,21);
        input_.setTextValueSuffix(" dB");
        input_.setDoubleClickReturnValue(true,0.);
        input_.setTooltip("Input Gain -12 to +12 dB. Double-click for 0 dB");
        inputAttachment_=std::make_unique<Attachment>(
            processor_.parameters(),"inputDb",input_);
        addAndMakeVisible(input_);

        flat_.setButtonText("Flat");
        flat_.onClick=[this]{processor_.setFlatFromUi();};
        addAndMakeVisible(flat_);
        smile_.setButtonText("Smile");
        smile_.onClick=[this]{
            for(std::size_t i=0;i<GraphicEq31Core::bandCount;++i) {
                const auto hz=graphicEq31Hz[i];
                processor_.setBandFromUi(i,hz<110.?3.f:(hz>4500.?2.5f:-1.f));
            }
        };
        addAndMakeVisible(smile_);
        for(int i=0;i<3;++i) {
            jump_[i].setButtonText(i==0?"LOW":i==1?"MID":"HIGH");
            jump_[i].onClick=[this,i]{
                viewport_.setViewPosition(i==0?0:(i==1?11:22)*bandCellWidth,0);
            };
            addAndMakeVisible(jump_[i]);
        }
        peakLabel_.setJustificationType(juce::Justification::centred);
        peakLabel_.setFont(inter(10.f));
        addAndMakeVisible(peakLabel_);
        setResizable(true,true);
        setResizeLimits(850,391,1800,391);
        setSize(1370,391);
        startTimerHz(20);
    }

    ~GraphicEq31Editor() override {stopTimer();setLookAndFeel(nullptr);}

    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour::fromRGB(243,245,246));
        auto b=getLocalBounds().toFloat().reduced(7.f);
        g.setColour(juce::Colour::fromRGB(251,252,252));
        g.fillRoundedRectangle(b,9.f);
        g.setColour(juce::Colour::fromRGB(220,225,226));
        g.drawRoundedRectangle(b,9.f,1.f);

        g.setColour(juce::Colour::fromRGB(53,67,72));
        g.setFont(inter(17.f));
        g.drawText("Graphic EQ 31",187,19,300,28,juce::Justification::centredLeft);
        g.setFont(inter(10.5f));
        g.setColour(juce::Colour::fromRGB(125,140,145));
        g.drawText("ARSONMIX  /  1/3 OCTAVE  /  20 Hz - 20 kHz",
                   188,44,410,18,juce::Justification::centredLeft);

        g.setColour(juce::Colour::fromRGB(224,230,230));
        g.drawVerticalLine(177,69.f,367.f);
        g.setColour(juce::Colour::fromRGB(65,76,79));
        g.setFont(inter(12.f));
        g.drawText("INPUT GAIN",18,79,149,20,juce::Justification::centred);
        g.setFont(inter(10.f));
        g.setColour(juce::Colour::fromRGB(133,146,149));
        g.drawText("-12",18,231,45,20,juce::Justification::centred);
        g.drawText("+12",116,231,48,20,juce::Justification::centred);
        g.drawText("31 BANDS  /  +/-12 dB",188,358,
                   juce::jmax(0,getWidth()-209),20,juce::Justification::centredLeft);
    }

    void resized() override {
        constexpr int x0=186;
        // 37px pitch instead of the former 80px: 31 bands total 1147px.
        // At 1370px default width all 31 appear without horizontal scrolling.
        viewport_.setBounds(x0,69,juce::jmax(1,getWidth()-x0-17),283);
        bandSurface_.setSize(static_cast<int>(GraphicEq31Core::bandCount)*bandCellWidth,276);
        for(std::size_t i=0;i<GraphicEq31Core::bandCount;++i)
            sliders_[i].setBounds(static_cast<int>(i)*bandCellWidth,0,
                                  bandCellWidth,274);
        input_.setBounds(21,110,138,175);
        flat_.setBounds(19,302,70,27);
        smile_.setBounds(96,302,70,27);
        for(int i=0;i<3;++i)
            jump_[i].setBounds(juce::jmax(595,getWidth()-250)+i*77,26,71,25);
        peakLabel_.setBounds(18,340,150,20);
    }

private:
    static juce::Font inter(float size) {return juce::Font("Inter",size,juce::Font::plain);}
    class EqSkin final : public juce::LookAndFeel_V4 {
    public:
        EqSkin() {
#if defined(ARMIX_GEQ31_VECTOR_ASSET) && ARMIX_GEQ31_VECTOR_ASSET
            originalSvg_=juce::String::fromUTF8(BinaryData::slider_eq_grp_svg,
                static_cast<int>(BinaryData::slider_eq_grp_svgSize));
            knobSvg_=juce::String::fromUTF8(BinaryData::KNOB_EQ_svg,
                static_cast<int>(BinaryData::KNOB_EQ_svgSize));
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
                    const float shift=-560.f*juce::jlimit(-12.f,12.f,gain)/12.f;
                    thumbGroups_[band]->setTransform(juce::AffineTransform::translation(0.f, shift));
                    lastGain_[band]=gain;
                }
                // Constrained to each 37px column. The original grouped SVG
                // still contains every native Track/Text/THUMB path and raster.
                art->drawWithin(g,{static_cast<float>(x),static_cast<float>(y),
                    static_cast<float>(width),static_cast<float>(height)},
                    juce::RectanglePlacement::stretchToFit,1.f);
            }else {
                g.setColour(juce::Colours::red);
                g.drawFittedText("SVG ERROR",x,y,width,height,
                    juce::Justification::centred,2);
            }
        }
        void drawRotarySlider(juce::Graphics& g,int x,int y,int width,int height,
                              float position,float,float,juce::Slider& slider) override {
            if(slider.getComponentID()!="geq31-input")return;
            if(!knobArt_)createKnobArtwork();
            if(!knobArt_) {
                g.setColour(juce::Colours::red);
                g.drawFittedText("KNOB SVG ERROR",x,y,width,height,
                    juce::Justification::centred,2);
                return;
            }
            if(knobPointer_&&lastKnobPosition_!=position) {
                constexpr float pi=3.14159265358979323846f;
                const float angle=(-135.f+270.f*juce::jlimit(0.f,1.f,position))*pi/180.f;
                // Source coordinate and POINTER group retained from KNOB-EQ.svg.
                knobPointer_->setTransform(juce::AffineTransform::rotation(
                    angle,35.62f,113.63f));
                lastKnobPosition_=position;
            }
            knobArt_->drawWithin(g,{static_cast<float>(x),static_cast<float>(y),
                static_cast<float>(width),static_cast<float>(height)},
                juce::RectanglePlacement::stretchToFit,1.f);
        }

    private:
        static juce::Component* findGroup(juce::Component* root,const juce::String& id) {
            if(root==nullptr)return nullptr;
            if(root->getComponentID()==id)return root;
            for(int i=0;i<root->getNumChildComponents();++i)
                if(auto* child=findGroup(root->getChildComponent(i),id))return child;
            return nullptr;
        }
        void createBandArtwork(std::size_t band) {
            if(originalSvg_.isEmpty())return;
            auto document=juce::XmlDocument::parse(originalSvg_);
            if(!document)return;
            auto* fader=document->getChildByAttribute("id","Slider_EQ");
            if(!fader)return;
            auto* labelGroup=fader->getChildByAttribute("id","Text");
            auto* thumbGroup=fader->getChildByAttribute("id","THUMB");
            if(!labelGroup||!thumbGroup)return;
            auto* frequency=labelGroup->getChildByAttribute("id","32");
            auto* tspan=frequency?frequency->getFirstChildElement():nullptr;
            if(!tspan)return;
            tspan->deleteAllTextElements();
            tspan->addTextElement(juce::String(graphicEq31Labels[band]));
            // Override the authored Arial class at render time, preserving
            // the source asset and all original SVG layers byte-for-byte.
            tspan->setAttribute("style","font-family: Inter; font-size: "+
                juce::String(juce::String(graphicEq31Labels[band]).length()>3?22:28)+"px");
            frequency->setAttribute("text-anchor","middle");
            frequency->setAttribute("style",
                "transform: matrix(5.4,0,0,4.167,237.76,1850.466); font-family: Inter");
            if(auto* hz=labelGroup->getChildByAttribute("id","Hz")) {
                if(auto* text=hz->getFirstChildElement()) {
                    text->setAttribute("font-family","Inter");
                    if(auto* unit=text->getFirstChildElement())
                        unit->setAttribute("style","font-family: Inter");
                }
            }
            auto drawable=juce::Drawable::createFromSVG(*document);
            if(!drawable)return;
            auto* authoredFader=drawable->findChildWithID("Slider_EQ");
            auto* authoredThumb=authoredFader?authoredFader->findChildWithID("THUMB"):nullptr;
            if(!authoredThumb)return;
            thumbGroups_[band]=authoredThumb;
            drawables_[band]=std::move(drawable);
        }
        void createKnobArtwork() {
            if(knobSvg_.isEmpty())return;
            auto document=juce::XmlDocument::parse(knobSvg_);
            if(!document)return;
            // Crop transparent authored margins only in the UI viewBox,
            // never edit/flatten the original asset, KNOB or POINTER paths.
            document->setAttribute("viewBox","-56 20 180 187");
            knobArt_=juce::Drawable::createFromSVG(*document);
            knobPointer_=findGroup(knobArt_.get(),"POINTER");
        }

        juce::String originalSvg_,knobSvg_;
        std::array<std::unique_ptr<juce::Drawable>,GraphicEq31Core::bandCount> drawables_{};
        std::array<juce::Component*,GraphicEq31Core::bandCount> thumbGroups_{};
        std::array<float,GraphicEq31Core::bandCount> lastGain_{};
        std::unique_ptr<juce::Drawable> knobArt_;
        juce::Component* knobPointer_=nullptr;
        float lastKnobPosition_=std::numeric_limits<float>::quiet_NaN();
    };

    void timerCallback() override {
        const bool clip=processor_.observedPeak()>=.999f;
        peakLabel_.setText(clip?"CLIP":"OUTPUT LEVEL",juce::dontSendNotification);
        peakLabel_.setColour(juce::Label::textColourId,
            clip?juce::Colour::fromRGB(226,78,58):
                 juce::Colour::fromRGB(55,158,136));
    }
    using Attachment=juce::AudioProcessorValueTreeState::SliderAttachment;
    static constexpr int bandCellWidth=37;
    GraphicEq31Processor& processor_;
    EqSkin skin_;
    juce::Component bandSurface_;
    juce::Viewport viewport_;
    std::array<juce::Slider,GraphicEq31Core::bandCount> sliders_;
    std::vector<std::unique_ptr<Attachment>> attachments_;
    juce::Slider input_;
    std::unique_ptr<Attachment> inputAttachment_;
    juce::TextButton flat_,smile_;
    std::array<juce::TextButton,3> jump_;
    juce::Label peakLabel_;
};
inline juce::AudioProcessorEditor* GraphicEq31Processor::createEditor() {
    return new GraphicEq31Editor(*this);
}
} // namespace armix::builtin_fx
