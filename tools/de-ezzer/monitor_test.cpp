// SPDX-License-Identifier: AGPL-3.0-only
#include "../../engine/builtin_fx/de_ezzer/DeEzzerInsert.h"
#include <iostream>
#include <cstdlib>
using namespace armix::builtin_fx::de_ezzer;
void require(bool v,const char* why){if(!v){std::cerr<<why<<'\n';std::exit(1);}}
int main(){
    for(double rate:{44100.,48000.,88200.,96000.})for(int channels:{1,2}){
        constexpr int n=127;
        Settings s;s.amount=1;s.sensitivity=1;s.rangeDb=18;s.shape=Shape::Focused;
        Control reference,observed,cue;require(reference.prepare({1,1},rate,channels,s)&&observed.prepare({2,1},rate,channels,s)&&cue.prepare({3,1},rate,channels,s),"prepare");
        std::array<float,n> l{},r{},a{},b{},c{},d{},ml{},mr{};
        double cueEnergy=0,deltaError=0;Observation measured;
        for(int block=0;block<220;++block){
            for(int j=0;j<n;++j){const double t=(block*n+j)/rate;
                l[j]=static_cast<float>(.12*std::sin(2*std::numbers::pi*800*t)+.25*std::sin(2*std::numbers::pi*6300*t));r[j]=-l[j];}
            a=c=l;b=d=r;
            observed.requestAnalysis();
            require(reference.process(l.data(),channels==2?r.data():nullptr,n),"reference");
            require(observed.process(a.data(),channels==2?b.data():nullptr,n),"observed");
            const auto mode=block<60?CueMode::Ess:block<120?CueMode::Sidechain:block<180?CueMode::Delta:CueMode::Mix;
            require(cue.processAudioWithCue(c.data(),channels==2?d.data():nullptr,n,mode,ml.data(),channels==2?mr.data():nullptr),"cue");
            require(l==a&&l==c&&(channels==1||(r==b&&r==d)),"programme PCM must be identical");
            for(int j=0;j<n;++j){
                require(std::isfinite(ml[j])&&std::abs(ml[j])<=.5f,"bounded cue");
                if(channels==2)require(ml[j]==-mr[j],"stereo phase preserved");
                if(mode==CueMode::Ess&&block>30)cueEnergy+=ml[j]*ml[j];
                if(mode==CueMode::Delta&&(block-120)*n+j>=static_cast<int>(rate*.005)){const double t=(block*n+j)/rate;
                    const auto dry=static_cast<float>(.12*std::sin(2*std::numbers::pi*800*t)+.25*std::sin(2*std::numbers::pi*6300*t));
                    deltaError=std::max(deltaError,std::abs(static_cast<double>(ml[j])-(dry-l[j])*.251188643150958));}
            }
            Snapshot snap;while(observed.pollTelemetry(snap)){if(snap.observation.valid)measured=snap.observation;}
        }
        require(cueEnergy>.01&&deltaError<1e-7,"nonzero ESS and actual Delta after fade");
        require(measured.valid&&measured.inputDb[2]>measured.inputDb[4]&&measured.inputDb[2]-measured.wetDb[2]>3.f,"actual 6.3k measurement");
        require(!cue.processAudioWithCue(c.data(),channels==2?d.data():nullptr,n,CueMode::Ess,c.data(),channels==2?mr.data():nullptr),"reject alias");
        require(c==l,"rejected cue leaves PCM untouched");
        for(int k=0;k<600;++k){a.fill(0);b.fill(0);require(observed.process(a.data(),channels==2?b.data():nullptr,n),"expiry");Snapshot tmp;while(observed.pollTelemetry(tmp)){}}
        require(!observed.quiescentSnapshot().observation.valid,"editor analysis expires");
    }
    std::cout<<"PASS: separate ESS/sidechain/delta/mix PCM, exact programme parity, real band readback, alias rejection and lease expiry\n";
}
