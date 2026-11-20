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
        outputValue_=params_.getRawParameterValue("outputDb");
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
        outputSmooth_.reset(sampleRate,.006);
        inputSmooth_.setCurrentAndTargetValue(dbToGain(inputValue_->load()));
        outputSmooth_.setCurrentAndTargetValue(dbToGain(outputValue_->load()));
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
        outputSmooth_.setTargetValue(dbToGain(outputValue_->load(std::memory_order_relaxed)));
        for(int n=0;n<count;++n) {
            const float g=static_cast<float>(inputSmooth_.getNextValue());
            l[n]*=g;r[n]*=g;
        }
        core_.processStereo(l,r,static_cast<std::size_t>(count));
        float peak=0.f;
        for(int n=0;n<count;++n) {
            const float g=static_cast<float>(outputSmooth_.getNextValue());
            l[n]*=g;r[n]*=g;
            peak=std::max(peak,std::max(std::abs(l[n]),std::abs(r[n])));
        }
        peak_.store(peak,std::memory_order_relaxed);
    }
    void processBlock(juce::AudioBuffer<double>&,juce::MidiBuffer&) override {}
    void getStateInformation(juce::MemoryBlock& destination) override {
        // CONTROL thread. Only one fixed-size, versioned state per insert.
        juce::MemoryOutputStream out(destination,false);
        out.writeInt(stateMagic);out.writeInt(1);
        for(auto* p:bandValues_)out.writeFloat(p->load(std::memory_order_acquire));
        out.writeFloat(inputValue_->load(std::memory_order_acquire));
        out.writeFloat(outputValue_->load(std::memory_order_acquire));
    }
    bool restoreUnpublishedState(const void* bytes,int length) {
        constexpr int stateBytes=8+static_cast<int>(GraphicEq31Core::bandCount+2)*4;
        if(published_||!bytes||length!=stateBytes)return false;
        juce::MemoryInputStream input(bytes,static_cast<std::size_t>(length),false);
        if(input.readInt()!=stateMagic||input.readInt()!=1)return false;
        std::array<float,GraphicEq31Core::bandCount+2> data{};
        for(auto& v:data) {
            v=input.readFloat();
            if(!std::isfinite(v)||v < -12.f||v>12.f)return false;
        }
        if(!input.isExhausted())return false;
        for(std::size_t i=0;i<GraphicEq31Core::bandCount;++i)
            setParameter(paramId(i),data[i]);
        setParameter("inputDb",data[GraphicEq31Core::bandCount]);
        setParameter("outputDb",data[GraphicEq31Core::bandCount+1]);
        if(ready_) {
            // Only an unpublished processor may be rebuilt on CONTROL.
            prepareToPlay(core_.sampleRate(),0);
        }
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
        for(const auto& id:{"inputDb","outputDb"})
            result.add(std::make_unique<juce::AudioParameterFloat>(
                juce::ParameterID{id,1},juce::String(id),
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
    std::atomic<float>* outputValue_=nullptr;
    juce::SmoothedValue<double,juce::ValueSmoothingTypes::Linear> inputSmooth_,outputSmooth_;
    std::atomic<float> peak_{0.f};
    bool published_=false,ready_=false;
};

class GraphicEq31Editor final : public juce::AudioProcessorEditor,private juce::Timer {
public:
    explicit GraphicEq31Editor(GraphicEq31Processor& processor)
        : juce::AudioProcessorEditor(processor),processor_(processor) {
        setLookAndFeel(&skin_);
        for(std::size_t i=0;i<GraphicEq31Core::bandCount;++i) {
            auto& slider=sliders_[i];
            slider.setSliderStyle(juce::Slider::LinearVertical);
            slider.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);
            slider.setDoubleClickReturnValue(true,0.);
            slider.setTooltip(juce::String(graphicEq31Labels[i])+
                " Hz \xC2\xB1 12 dB; double-click for 0 dB");
            addAndMakeVisible(slider);
            auto& label=labels_[i];
            label.setText(graphicEq31Labels[i],juce::dontSendNotification);
            label.setJustificationType(juce::Justification::centred);
            label.setFont(juce::Font(11.f,juce::Font::plain));
            label.setColour(juce::Label::textColourId,juce::Colours::black);
            addAndMakeVisible(label);
            attachments_.emplace_back(std::make_unique<Attachment>(
                processor_.parameters(), "band"+juce::String(static_cast<int>(i)),slider));
        }
        for(auto* knob:{&input_,&output_}) {
            knob->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
            knob->setTextBoxStyle(juce::Slider::TextBoxBelow,false,68,19);
            knob->setDoubleClickReturnValue(true,0.);
            addAndMakeVisible(*knob);
        }
        inputAttachment_=std::make_unique<Attachment>(processor_.parameters(),"inputDb",input_);
        outputAttachment_=std::make_unique<Attachment>(processor_.parameters(),"outputDb",output_);
        flat_.setButtonText("FLAT");
        flat_.onClick=[this]{processor_.setFlatFromUi();};
        addAndMakeVisible(flat_);
        smile_.setButtonText("SMILE");
        smile_.onClick=[this] {
            for(std::size_t i=0;i<GraphicEq31Core::bandCount;++i) {
                const auto f=graphicEq31Hz[i];
                processor_.setBandFromUi(i,f<110?3.f:(f>4500?2.5f:-1.f));
            }
        };
        addAndMakeVisible(smile_);
        peakLabel_.setJustificationType(juce::Justification::centred);
        peakLabel_.setColour(juce::Label::textColourId,juce::Colours::black);
        addAndMakeVisible(peakLabel_);
        setSize(1500,328);
        startTimerHz(20);
    }
    ~GraphicEq31Editor() override {stopTimer();setLookAndFeel(nullptr);}
    void paint(juce::Graphics& g) override {
        auto b=getLocalBounds().toFloat();
        g.setGradientFill(juce::ColourGradient(
            juce::Colour::fromRGB(241,242,242),b.getTopLeft(),
            juce::Colour::fromRGB(202,207,211),b.getBottomLeft(),false));
        g.fillRoundedRectangle(b.reduced(2.f),11.f);
        g.setColour(juce::Colour::fromRGB(72,78,82));
        g.drawRoundedRectangle(b.reduced(3.f),11.f,2.f);
        g.setColour(juce::Colours::black);
        g.setFont(juce::Font(20.f,juce::Font::plain));
        g.drawText("GRAPHIC EQ 31",juce::Rectangle<int>(20,12,250,31),
                   juce::Justification::centredLeft);
        g.setFont(juce::Font(11.f,juce::Font::plain));
        g.drawText("ARSONMIX  |  1/3 OCTAVE",juce::Rectangle<int>(23,44,190,22),
                   juce::Justification::centredLeft);
        g.drawText("INPUT",juce::Rectangle<int>(18,78,72,20),juce::Justification::centred);
        g.drawText("OUTPUT",juce::Rectangle<int>(92,78,72,20),juce::Justification::centred);
        g.drawText("+12",juce::Rectangle<int>(169,81,34,22),juce::Justification::centred);
        g.drawText("0",juce::Rectangle<int>(169,170,34,22),juce::Justification::centred);
        g.drawText("-12",juce::Rectangle<int>(169,259,34,22),juce::Justification::centred);
        g.setColour(juce::Colour::fromRGB(70,75,78));
        g.drawLine(202.f,65.f,202.f,291.f,1.f);
        g.setFont(juce::Font(11.f,juce::Font::plain));
        g.drawText("31 FIXED FREQUENCY BANDS   \xC2\xB1 12 dB",
                   juce::Rectangle<int>(220,294,getWidth()-235,20),
                   juce::Justification::centred);
    }
    void resized() override {
        input_.setBounds(19,102,69,83);
        output_.setBounds(92,102,69,83);
        flat_.setBounds(19,205,68,27);
        smile_.setBounds(93,205,68,27);
        peakLabel_.setBounds(16,253,150,26);
        constexpr int x0=211,step=41;
        for(std::size_t i=0;i<GraphicEq31Core::bandCount;++i) {
            const int x=x0+static_cast<int>(i)*step;
            labels_[i].setBounds(x,61,40,19);
            sliders_[i].setBounds(x+3,83,34,203);
        }
    }
private:
    class EqSkin final : public juce::LookAndFeel_V4 {
    public:
        EqSkin() {
#if defined(ARMIX_GEQ31_VECTOR_ASSET) && ARMIX_GEQ31_VECTOR_ASSET
            // UI thread only: user's original fader-track SVG, stripped of its
            // stationary thumb and baked label, stored as compact gzip.
            // The JUCE attachment controls the independent live silver thumb.
            juce::GZIPDecompressorInputStream decompressed(
                new juce::MemoryInputStream(BinaryData::slider_eq_vector_svgz,
                    static_cast<size_t>(BinaryData::slider_eq_vector_svgzSize),false),
                true,juce::GZIPDecompressorInputStream::gzipFormat);
            if(auto xml=juce::XmlDocument::parse(
                    decompressed.readEntireStreamAsString()))
                vectorTrack_=juce::Drawable::createFromSVG(*xml);
#endif
        }
        void drawLinearSlider(juce::Graphics& g,int x,int y,int width,int height,
                              float sliderPos,float /*minPos*/,float /*maxPos*/,
                              juce::Slider::SliderStyle,juce::Slider&) override {
            const float cx=x+width*.5f;
            if(vectorTrack_) {
                vectorTrack_->drawWithin(g,juce::Rectangle<float>(
                    static_cast<float>(x),static_cast<float>(y),
                    static_cast<float>(width),static_cast<float>(height)),
                    juce::RectanglePlacement::stretchToFit,1.f);
            }else {
            g.setColour(juce::Colour::fromRGB(175,180,184));
            for(int t=0;t<=12;++t) {
                const float yy=y+11.f+(height-22.f)*t/12.f;
                const float length=t==6?12.f:6.f;
                g.drawLine(cx-11.f,yy,cx-11.f+length,yy,.9f);
                g.drawLine(cx+11.f-length,yy,cx+11.f,yy,.9f);
            }
            g.setColour(juce::Colour::fromRGB(19,27,31));
            g.fillRoundedRectangle(cx-5.f,y+9.f,10.f,height-18.f,5.f);
            g.setColour(juce::Colour::fromRGB(61,67,70));
            g.drawRoundedRectangle(cx-5.f,y+9.f,10.f,height-18.f,5.f,1.f);
            }
            const auto py=juce::jlimit(static_cast<float>(y+11),
                            static_cast<float>(y+height-11),sliderPos);
            g.setColour(juce::Colour::fromRGB(94,98,103).withAlpha(.35f));
            g.fillRoundedRectangle(cx-17.f,py-13.f,34.f,27.f,7.f);
            g.setGradientFill(juce::ColourGradient(
                juce::Colours::white,cx,py-10.f,
                juce::Colour::fromRGB(187,191,195),cx,py+10.f,false));
            g.fillRoundedRectangle(cx-13.f,py-11.f,26.f,22.f,7.f);
            g.setColour(juce::Colour::fromRGB(131,137,141));
            g.drawRoundedRectangle(cx-13.f,py-11.f,26.f,22.f,7.f,1.f);
            g.setColour(juce::Colour::fromRGB(225,228,230));
            g.drawLine(cx-8.f,py,cx+8.f,py,1.2f);
        }
    private:
        std::unique_ptr<juce::Drawable> vectorTrack_;
    };
    void timerCallback() override {
        const auto p=processor_.observedPeak();
        const bool clip=p>=.999f;
        peakLabel_.setText(clip?"CLIP":"OUTPUT LEVEL",
                            juce::dontSendNotification);
        peakLabel_.setColour(juce::Label::backgroundColourId,
            clip?juce::Colour::fromRGB(233,76,55):
                 juce::Colour::fromRGB(173,192,177));
    }
    using Attachment=juce::AudioProcessorValueTreeState::SliderAttachment;
    GraphicEq31Processor& processor_;
    EqSkin skin_;
    std::array<juce::Slider,GraphicEq31Core::bandCount> sliders_;
    std::array<juce::Label,GraphicEq31Core::bandCount> labels_;
    std::vector<std::unique_ptr<Attachment>> attachments_;
    juce::Slider input_,output_;
    std::unique_ptr<Attachment> inputAttachment_,outputAttachment_;
    juce::TextButton flat_,smile_;
    juce::Label peakLabel_;
};
inline juce::AudioProcessorEditor* GraphicEq31Processor::createEditor() {
    return new GraphicEq31Editor(*this);
}
} // namespace armix::builtin_fx
