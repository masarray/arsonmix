// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/AntiNoizProcessor.h"
#include "../engine/core/LiveChannelFxGraph.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
using armix::builtin_fx::AntiNoizProcessor;
using Core=AntiNoizProcessor::Core;
using Control=AntiNoizProcessor::Control;
using Instance=armix::mixer::LiveChannelFxGraph::FxInstance;
void check(bool ok,const char* why){if(!ok){std::cerr<<why<<'\n';std::abort();}}
int main(){
    juce::ScopedJuceInitialiser_GUI gui;
    for(double rate:{44100.,48000.,88200.,96000.}){
        auto p=std::make_unique<AntiNoizProcessor>();auto* fx=p.get();
        p->prepareToPlay(rate,128);const int latency=rate>48000?512:256;
        check(p->isReady()&&p->getLatencySamples()==latency&&p->getSampleRate()==rate,"JUCE prepare");
        juce::MemoryBlock saved;p->getStateInformation(saved);
        check(saved.getSize()==28,"state size");
        Instance lease(std::move(p),AntiNoizProcessor::catalogIdentity,"Anti Noiz",128);
        check(lease.continuousBypassProcessing&&lease.latencySamples==latency,"actual host PDC");
        check(!fx->restoreUnpublishedState(saved.getData(),28),"published restore denied");
        Core::Settings s;s.mode=Core::Mode::Voice;s.silenceDb=0;
        check(fx->queueSettings(s,1)&&!lease.tryCaptureState(saved),"pending state denied");
        std::array<float,128> l{},r{};
        l[0]=.25f;r[0]=-.125f;
        Control::Completion ack;
        for(int pos=0;pos<=latency;pos+=128){
            lease.render(l.data(),r.data(),128);
            if(pos==0)check(fx->pollCompletion(ack)&&ack.applied&&ack.id==1,"native ACK");
            for(int i=0;i<128;++i){
                const float want=pos+i==latency?.25f:0.f;
                check(std::abs(l[i]-want)<2e-6f&&std::abs(r[i]+want*.5f)<2e-6f,"wet unity latency");
            }
            l.fill(0);r.fill(0);
        }
        check(lease.tryCaptureState(saved)&&saved.getSize()==28,"accepted native capture");
        check(fx->queueLearn(2),"native learn");lease.render(l.data(),r.data(),128);
        check(fx->pollCompletion(ack)&&ack.applied&&ack.state.learning,"native learning");
        for(int i=0;i<16;++i)lease.render(l.data(),r.data(),128,true);
        check(fx->queueLearn(3),"native bypass command");lease.render(l.data(),r.data(),128,true);
        check(fx->pollCompletion(ack)&&!ack.applied&&ack.state.learnStatus==Core::LearnStatus::Cancelled,"bypass learn cancelled");
        for(int pos=0;pos<=latency;pos+=128){
            if(!pos){l[0]=.125f;r[0]=-.25f;}
            lease.render(l.data(),r.data(),128,true);
            for(int i=0;i<128;++i){
                const float want=pos+i==latency?.125f:0.f;
                check(std::abs(l[i]-want)<2e-6f&&std::abs(r[i]+2*want)<2e-6f,"host bypass exact PDC");
            }
            l.fill(0);r.fill(0);
        }
        auto restored=std::make_unique<AntiNoizProcessor>();
        check(restored->restoreUnpublishedState(saved.getData(),28),"cold reopen");
        restored->prepareToPlay(rate,128);
        check(restored->isReady()&&restored->acceptedSettings().mode==Core::Mode::Voice,"native state roundtrip");
        std::cout<<"JUCE_HOST,"<<rate<<",latency="<<latency<<",FxInstance ACK/restore/bypass PASS\n";
    }
}
