// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "NativeSession.h"
#include "SingerWorkflow.h"
#include "../../core/RealtimeNativeFxControl.h"
#include <juce_audio_processors/juce_audio_processors.h>
namespace armix::builtin_fx::vocal_delay {
// Candidate host adapter. The mixer must bind a fresh graph lease while
// stopped before prepareToPlay; no implicit identity or autonomous routing.
// AVTH v1 is still dispatched to the original VocalThrowProcessor.
class JuceVocalDelayProcessor final : public juce::AudioProcessor,
                                     public armix::mixer::RealtimeNativeFxControl,private juce::Timer {
public:
    JuceVocalDelayProcessor():AudioProcessor(BusesProperties()
      .withInput("Input",juce::AudioChannelSet::stereo(),true)
      .withOutput("Output",juce::AudioChannelSet::stereo(),true)){setLatencySamples(0);}
    const juce::String getName() const override{return "Vocal Delay";}
    bool acceptsMidi() const override{return false;}
    bool producesMidi() const override{return false;}
    bool isMidiEffect() const override{return false;}
    double getTailLengthSeconds() const override{return 264.;}
    int getNumPrograms() override{return 1;}
    int getCurrentProgram() override{return 0;}
    void setCurrentProgram(int) override{}
    const juce::String getProgramName(int) override{return "Current";}
    void changeProgramName(int,const juce::String&) override{}
    bool hasEditor() const override{return true;}
    juce::AudioProcessorEditor* createEditor() override;
    ~JuceVocalDelayProcessor() override{stopTimer();}
    bool isBusesLayoutSupported(const BusesLayout& b) const override{
        return b.getMainInputChannelSet()==juce::AudioChannelSet::stereo()&&
               b.getMainOutputChannelSet()==juce::AudioChannelSet::stereo();
    }
    // CONTROL, only before publication or after the audio graph is stopped.
    bool bindLeaseStopped(Lease l) noexcept{
        if(active_||!l.valid())return false;
        next_=l;return true;
    }
    void prepareToPlay(double rate,int) override{
        if(active_)return; // host must stop first
        if(!next_.valid())return;
        active_=prepared_?session_.restartStopped(rate,next_):session_.prepareStopped(rate,next_,initial_);
        if(active_)prepared_=true;
        next_={};
    }
    void releaseResources() override{active_=false;}
    void processBlock(juce::AudioBuffer<float>& pcm,juce::MidiBuffer&) override{
        juce::ScopedNoDenormals noDenormals;
        if(active_&&pcm.getNumChannels()==2&&pcm.getNumSamples()>0)
            session_.processStereo(pcm.getWritePointer(0),pcm.getWritePointer(1),
                                   static_cast<std::size_t>(pcm.getNumSamples()));
    }
    void processBlock(juce::AudioBuffer<double>&,juce::MidiBuffer&) override{}
    void consumeRealtimeEdits() noexcept override{if(active_)session_.consumeRealtimeEdits();}
    void onRealtimeBypass(bool requested,bool fully) noexcept override{
        if(active_)session_.onRealtimeBypass(requested,fully);
    }
    void sealForRealtime() noexcept override{published_=true;}
    bool stateSnapshotQuiescent() const noexcept override{return active_&&session_.quiescent()&&!singer_.pending()&&!singer_.fault();}
    Submit submit(const Edit& edit) noexcept{
        if(editorOpen_||singer_.pending()||singer_.fault())return Submit::busy;
        return active_?session_.submit(edit):Submit::notReady;
    }
    bool poll(Receipt& receipt) noexcept{return active_&&!singer_.pending()&&session_.poll(receipt);}
    // All editor calls and timer callbacks share the JUCE message thread.
    bool beginSingerEditor(){
        if(editorOpen_||!active_||!session_.quiescent()||!singer_.attach(session_.acceptedSnapshot()))return false;
        editorOpen_=true;startTimerHz(30);return true;
    }
    void endSingerEditor() noexcept{editorOpen_=false;serviceSingerControl();}
    bool offerSinger(const Parameters& p) noexcept{
        return editorOpen_&&active_&&singer_.offer(session_.acceptedSnapshot(),p);
    }
    Parameters singerDisplay() const noexcept{return singer_.display(session_.acceptedSnapshot());}
    bool singerPending() const noexcept{return singer_.pending();}
    bool singerFault() const noexcept{return singer_.fault();}
    void serviceSingerControl() noexcept{
        if(active_&&(editorOpen_||singer_.pending()))singer_.service(session_);
        if(!editorOpen_&&!singer_.pending())stopTimer();
    }
    Snapshot acceptedSnapshot() const noexcept{return session_.acceptedSnapshot();}
    bool isReady() const noexcept{return active_;}
    bool restoreUnpublishedState(const void* data,int bytes) noexcept{
        if(published_||prepared_||active_||bytes<0)return false;
        State s{};if(!StateCodec::decode(data,static_cast<std::size_t>(bytes),s)||
                      s.kind!=State::Kind::continuous)return false;
        initial_=s;return true;
    }
    void setStateInformation(const void* data,int bytes) override{(void)restoreUnpublishedState(data,bytes);}
    void getStateInformation(juce::MemoryBlock& out) override{
        StateCodec::Bytes bytes{};out.reset();
        const bool ok=prepared_?(stateSnapshotQuiescent()&&session_.save(bytes)):StateCodec::encode(initial_,bytes);
        if(ok)out.append(bytes.data(),bytes.size());
    }
private:
    void timerCallback() override{serviceSingerControl();}
    NativeSession session_{};
    SingerControl singer_{};
    bool editorOpen_=false;
    State initial_{};
    Lease next_{};
    bool active_=false,prepared_=false,published_=false;
};
} // namespace armix::builtin_fx::vocal_delay
#include "SingerEditor.h"
