// SPDX-License-Identifier: AGPL-3.0-only
#include "../tools/anti-noiz/OfflineAudio.h"
#include "../engine/builtin_fx/anti_noiz/AntiNoizControl.h"
#include <iostream>
#include <random>
using namespace armix::builtin_fx::anti_noiz;
int main(){try{
    using Role=Core::SourceRole;using Mode=Core::Mode;
    for(unsigned rate:{44100u,48000u,88200u,96000u}){
        std::mt19937 gen(912);std::uniform_real_distribution<float> rng(-.003f,.003f);
        offline::Audio noise{rate,2,std::vector<float>(3*rate*2)},clean=noise,input=noise;
        std::array<float,2> prev{};
        auto hiss=[&](unsigned c){float x=rng(gen),y=x-prev[c];prev[c]=x;return y;};
        for(unsigned i=0;i<3*rate;++i)for(unsigned c=0;c<2;++c)noise.samples[i*2+c]=hiss(c);
        for(unsigned i=0;i<3*rate;++i)for(unsigned c=0;c<2;++c){
            const auto j=i*2+c;const double phase=2.*std::numbers::pi*double(i)/rate;
            clean.samples[j]=float((.05*std::sin(phase*375)+.02*std::sin(phase*750)+.008*std::sin(phase*1125))*(c?.7:1.));
            input.samples[j]=clean.samples[j]+hiss(c);
        }
        Core::Settings s;s.silenceDb=0;
        auto mic=offline::render(input,&noise,s,64,Role::SpeechMic);
        auto music=offline::render(input,nullptr,s,127,Role::Music);
        auto mixed=offline::render(input,&noise,s,64,Role::Mixed);
        auto unknown=offline::render(input,&noise,s,64);
        s.mode=Mode::Voice;auto voice=offline::render(input,&noise,s,127);
        offline::require(mic.processed.samples==voice.processed.samples,"Auto mic must equal explicit Voice");
        offline::require(music.deltaRms<1e-7&&mixed.processed.samples==unknown.processed.samples,"music/mixed routing safety");
        s.mode=Mode::AutoProtect;auto singing=offline::render(input,&noise,s,64,Role::SingingMic);
        s.mode=Mode::Singing;auto explicitSinging=offline::render(input,&noise,s,127);
        offline::require(singing.processed.samples==explicitSinging.processed.samples,"Auto singing parity");
        double e0=0,e1=0,p=0,projection=0;
        for(std::size_t j=rate*2;j<input.samples.size();++j){const double x=input.samples[j],y=mic.processed.samples[j],r=clean.samples[j];e0+=(x-r)*(x-r);e1+=(y-r)*(y-r);p+=r*r;projection+=r*y;}
        const double benefit=10*std::log10(e0/e1),retain=projection/p;
        std::cout<<rate<<" Auto mic active error improvement "<<benefit<<" dB; projection "<<retain<<'\n';
        offline::require(benefit>=10&&retain>.97&&retain<1.03,"Auto mic active benefit");
    }
    Control c;Control::Completion ack;
    offline::require(c.prepare(48000,1,Role::SpeechMic),"mic graph prepare");
    offline::require(c.snapshot().sourceRole==Role::SpeechMic&&c.snapshot().effectiveMode==Mode::Voice,"mic effective readback");
    Core::Settings s;s.mode=Mode::Music;
    offline::require(c.submit({1,Control::Kind::settings,s}),"manual music override submit");c.consume();
    offline::require(c.poll(ack)&&ack.applied&&ack.state.effectiveMode==Mode::Music,"manual override ACK");
    s.mode=Mode::AutoProtect;offline::require(c.submit({2,Control::Kind::settings,s}),"auto restore submit");c.consume();
    offline::require(c.poll(ack)&&ack.applied&&ack.state.effectiveMode==Mode::Voice,"auto restore ACK");
    offline::require(c.submit({3,Control::Kind::learn,{}}),"mic learn request");
    std::mt19937 gen(77);std::uniform_real_distribution<float> rng(-.003f,.003f);
    std::array<float,128> pcm{};Control::Snapshot meter;
    for(int i=0;i<1125;++i){
        for(auto& x:pcm)x=rng(gen);
        c.process(pcm.data(),nullptr,128);while(c.pollTelemetry(meter)){}
    }
    offline::require(c.poll(ack)&&ack.applied&&c.snapshot().profile,"mic learned before graph replacement");
    offline::require(c.prepare(48000,2,Role::Music),"music graph epoch");
    offline::require(c.snapshot().effectiveMode==Mode::Music&&!c.snapshot().profile,"new epoch policy and invalidated profile");
    offline::require(c.submit({4,Control::Kind::learn,{}}),"learn request");c.consume();
    offline::require(c.poll(ack)&&!ack.applied,"music Auto learn rejected");
    offline::require(!c.prepare(48000,2,static_cast<Role>(99)),"invalid source role rejected");
    std::cout<<"Source policy, override ACK, graph epoch and music protection PASS\n";
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
