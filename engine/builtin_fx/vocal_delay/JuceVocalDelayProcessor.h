// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "NativeSession.h"
#include "SingerWorkflow.h"
#include "MusicFollow.h"
#include "../../core/RealtimeNativeFxControl.h"
#include <juce_audio_processors/juce_audio_processors.h>
namespace armix::builtin_fx::vocal_delay {
// Host adapter: published graph ownership is held by FxInstance and the
// host's exact retained-editor guard. The private mailbox is scoped to
// this processor object and its stopped generation, NOT a graph identity.
// Explicit leases remain available for standalone contract tests. AVTH v1
// stays with the legacy VocalThrowProcessor and its original catalog ID.
class JuceVocalDelayProcessor final : public juce::AudioProcessor,
                                     public armix::mixer::RealtimeNativeFxControl,private juce::Timer {
public:
    static constexpr const char* catalogIdentity="armix.builtin.vocal-delay.v2";
    JuceVocalDelayProcessor():AudioProcessor(BusesProperties()
      .withInput("Input",juce::AudioChannelSet::stereo(),true)
      .withOutput("Output",juce::AudioChannelSet::stereo(),true)){setLatencySamples(0);initial_.continuous=presetSettings(0);}
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
        if(active_||localMailbox_||!l.valid())return false;
        next_=l;return true;
    }
    // CONTROL-only factory opt-in before prepare. No graph ID is invented.
    // Local tickets are never exposed as mixer ChannelId/InsertId or accepted
    // by public submit(); only the retained native editor may use them.
    bool useHostLocalMailbox() noexcept{
        if(active_||prepared_||next_.valid())return false;
        localMailbox_=true;return true;
    }
    void prepareToPlay(double rate,int frames) override{
        if(active_||!validRate(rate)||frames<16)return;
        if(localMailbox_){if(generation_==std::numeric_limits<std::uint64_t>::max())return;
            next_={1,1,++generation_,1};} // object-local namespace only
        if(!next_.valid())return;
        setRateAndBufferSizeDetails(rate,frames);
        active_=prepared_?session_.restartStopped(rate,next_):session_.prepareStopped(rate,next_,initial_);
        if(active_){prepared_=true;
            if(published_&&localMailbox_){const auto p=singerDisplay();
                if(p.smartStage&&!p.bpmQuarter&&p.timeMs>=140.f)follow_.enable(true,p.timeMs);
            }
        }
        next_={};
    }
    // Host retires/stops PCM before release. Discard unconsumed UI intent;
    // restartStopped preserves audio-applied state, even if ACK was unpolled.
    void releaseResources() override{stopTimer();active_=false;singer_.reset();follow_.enable(false);editorOpen_=false;}
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
    void sealForRealtime() noexcept override{
        const bool first=!published_;published_=true;
        // Only new/unlocked stage sounds auto-follow an already routed shared clock.
        if(first&&active_&&localMailbox_){const auto p=singerDisplay();
            if(p.smartStage&&!p.bpmQuarter&&p.timeMs>=140.f)follow_.enable(true,p.timeMs);
        }
    }
    bool stateSnapshotQuiescent() const noexcept override{return active_&&session_.quiescent()&&!singer_.pending()&&!singer_.fault();}
    Submit submit(const Edit& edit) noexcept{
        if(localMailbox_)return Submit::invalid;
        if(editorOpen_||singer_.pending()||singer_.fault())return Submit::busy;
        return active_?session_.submit(edit):Submit::notReady;
    }
    bool poll(Receipt& receipt) noexcept{return !localMailbox_&&active_&&!singer_.pending()&&session_.poll(receipt);}
    // All editor calls and timer callbacks share the JUCE message thread.
    bool beginSingerEditor(){
        if(editorOpen_||!active_||singer_.fault()||(!session_.quiescent()&&!singer_.pending())||!singer_.attach(session_.acceptedSnapshot()))return false;
        editorOpen_=true;if(!isDetached())startTimerHz(30);return true;
    }
    void endSingerEditor() noexcept{editorOpen_=false;serviceSingerControl();}
    bool offerSinger(const Parameters& p) noexcept{
        if(!editorOpen_||!active_||!valid(p))return false;
        if(isDetached())return session_.applyDetached(p);
        return singer_.offer(session_.acceptedSnapshot(),p);
    }
    // Runtime opt-in. A saved Show or portable preset restores the last
    // confirmed numeric tempo, not an armed source or performance mode.
    bool setMusicFollow(bool enabled) noexcept{
        if(!active_||!published_||!localMailbox_||!editorOpen_||singer_.fault())return false;
        follow_.enable(enabled);return true;
    }
    bool armStageTempo(const Parameters& p) noexcept{
        if(!active_||!published_||!localMailbox_||!editorOpen_||singer_.fault())return false;
        const bool use=p.smartStage&&p.timeMs>=140.f&&!p.bpmQuarter;
        follow_.enable(use,use?p.timeMs:0.f);return use;
    }
    bool musicFollowing() const noexcept{return follow_.enabled();}
    MusicFollow::Status musicStatus() const noexcept{return follow_.status();}
    // Only called by the installed native graph's CONTROL traversal.
    void publishMusicObservation(const MusicObservation& o,std::uint64_t own,double now) noexcept{
        if(!active_||!published_||!localMailbox_||!follow_.enabled()||singer_.fault())return;
        const auto base=singerDisplay();Parameters next{};
        if(follow_.update(o,own,now,base,next)&&singer_.offer(session_.acceptedSnapshot(),next)){
            startTimerHz(30);serviceSingerControl();
        }
    }
    Parameters singerDisplay() const noexcept{return singer_.display(session_.acceptedSnapshot());}
    bool isDetached() const noexcept{return localMailbox_&&!published_;}
    bool singerPending() const noexcept{return singer_.pending();}
    bool singerFault() const noexcept{return singer_.fault();}
    void serviceSingerControl() noexcept{
        if(active_&&(editorOpen_||singer_.pending()))singer_.service(session_);
        if(!editorOpen_&&!singer_.pending())stopTimer();
    }
    Snapshot acceptedSnapshot() const noexcept{return session_.acceptedSnapshot();}
    bool isReady() const noexcept{return active_;}
    ContinuousDelayCore::Meter meter() const noexcept{return active_?session_.meter():ContinuousDelayCore::Meter{};}
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
    MusicFollow follow_{};
    bool editorOpen_=false;
    State initial_{};
    Lease next_{};
    bool active_=false,prepared_=false,published_=false,localMailbox_=false;
    std::uint64_t generation_=0;
};
} // namespace armix::builtin_fx::vocal_delay
#include "SingerEditor.h"
