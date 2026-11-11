// SPDX-License-Identifier: AGPL-3.0-only
// Offline native Harmony A/B: genuine adult vocal WAVs, no live callback IO.
// Synthesized self-test is only a build and integrity gate, not an ear-test.
#include "../engine/builtin_fx/HarmonyDsp.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
using armix::harmony::Engine;
using armix::harmony::Settings;
namespace fs=std::filesystem;
struct Wave {unsigned rate=0;std::vector<float> left,right;};
static std::uint16_t u16(const unsigned char* p){return std::uint16_t(p[0])|(std::uint16_t(p[1])<<8);}
static std::uint32_t u32(const unsigned char* p){return std::uint32_t(p[0])|(std::uint32_t(p[1])<<8)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[3])<<24);}
static void put16(std::ostream& out,std::uint16_t v){out.put(char(v&255));out.put(char((v>>8)&255));}
static void put32(std::ostream& out,std::uint32_t v){for(int k=0;k<4;++k)out.put(char((v>>(8*k))&255));}
static Wave readWave(const fs::path& file){
    if(!fs::exists(file)||fs::file_size(file)>220000000)throw std::runtime_error("WAV missing/too large");
    std::ifstream stream(file,std::ios::binary);
    if(!stream)throw std::runtime_error("WAV open failed");
    const std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(stream)),{});
    if(bytes.size()<44||std::memcmp(bytes.data(),"RIFF",4)!=0||
       std::memcmp(bytes.data()+8,"WAVE",4)!=0)
        throw std::runtime_error("expected RIFF/WAVE");
    std::uint16_t fmt=0,ch=0,bits=0,align=0;std::uint32_t hz=0;
    std::size_t offset=12,dataStart=0,dataBytes=0;
    while(offset+8<=bytes.size()){
        const auto size=static_cast<std::size_t>(u32(bytes.data()+offset+4));
        const auto start=offset+8;
        if(size>bytes.size()-start)throw std::runtime_error("truncated WAV chunk");
        if(std::memcmp(bytes.data()+offset,"fmt ",4)==0){
            if(size<16)throw std::runtime_error("WAV fmt too short");
            const auto* p=bytes.data()+start;
            fmt=u16(p);ch=u16(p+2);hz=u32(p+4);align=u16(p+12);bits=u16(p+14);
        }else if(std::memcmp(bytes.data()+offset,"data",4)==0){
            dataStart=start;dataBytes=size;
        }
        offset=start+size+(size&1);
    }
    if(hz<32000||hz>192000||(ch!=1&&ch!=2)||
       !((fmt==1&&(bits==16||bits==24))||(fmt==3&&bits==32))||
       align!=ch*(bits/8)||!dataBytes||dataBytes%align)
       throw std::runtime_error("use 32-192kHz mono/stereo PCM16/24 or IEEE float32 WAV");
    const auto frames=dataBytes/align;
    if(frames>static_cast<std::size_t>(hz)*60)throw std::runtime_error("use <=60s original adult-vocal excerpt");
    Wave out;out.rate=hz;out.left.resize(frames);out.right.resize(frames);
    const auto* data=bytes.data()+dataStart;
    for(std::size_t i=0;i<frames;++i){
        auto sample=[&](int c)->float{
            const auto* p=data+i*align+static_cast<std::size_t>(c)*(bits/8);
            if(fmt==3){
                const auto raw=u32(p);float x=0;std::memcpy(&x,&raw,sizeof(x));
                return std::isfinite(x)?std::clamp(x,-4.f,4.f):0.f;
            }
            if(bits==16)return static_cast<float>(std::int16_t(u16(p)))/32768.f;
            std::int32_t value=std::int32_t(p[0])|(std::int32_t(p[1])<<8)|
                               (std::int32_t(p[2])<<16);
            if(value&0x800000)value|=~0xffffff;
            return static_cast<float>(value)/8388608.f;
        };
        out.left[i]=sample(0);
        out.right[i]=sample(ch==2?1:0);
    }
    return out;
}
static void writeWave(const fs::path& path,const Wave& wave){
    if(wave.left.size()!=wave.right.size()||wave.left.size()>0x1fffffff)
        throw std::runtime_error("WAV output too large");
    std::ofstream out(path,std::ios::binary|std::ios::trunc);
    if(!out)throw std::runtime_error("WAV output open failed");
    const std::uint32_t n=static_cast<std::uint32_t>(wave.left.size()*8);
    out.write("RIFF",4);put32(out,n+36);out.write("WAVEfmt ",8);
    put32(out,16);put16(out,3);put16(out,2);put32(out,wave.rate);
    put32(out,wave.rate*8);put16(out,8);put16(out,32);
    out.write("data",4);put32(out,n);
    for(std::size_t i=0;i<wave.left.size();++i)for(float x:{wave.left[i],wave.right[i]}){
        std::uint32_t raw=0;std::memcpy(&raw,&x,sizeof(raw));put32(out,raw);
    }
    if(!out)throw std::runtime_error("WAV output write failed");
}
static Wave process(const Wave& input,Settings settings,bool synchronous,int block=127,
                    bool enhancedDepth=true,bool focus=true,bool presence=true){
    auto e=std::make_unique<Engine>();
    if(!e->prepare(input.rate)||!e->setSettings(settings))
        throw std::runtime_error("native Harmony configuration invalid");
    e->setPitchSynchronousCandidateForOfflineReference(synchronous);
    e->setStageDepthForOfflineReference(enhancedDepth);
    e->setRoomFocusForOfflineReference(focus);
    if(!e->setBackingPresenceForOfflineReference(presence))
        throw std::runtime_error("Harmony presence offline reference rejected");
    Wave out=input;
    for(std::size_t pos=0;pos<out.left.size();pos+=static_cast<std::size_t>(block))
        e->process(out.left.data()+pos,out.right.data()+pos,
                   static_cast<int>(std::min(static_cast<std::size_t>(block),out.left.size()-pos)));
    return out;
}
static double rms(const Wave& x){
    if(x.left.empty())return 0.;double acc=0.;
    for(std::size_t i=0;i<x.left.size();++i)
        acc+=double(x.left[i])*x.left[i]+double(x.right[i])*x.right[i];
    return std::sqrt(acc/(2.*x.left.size()));
}
static Wave subtract(const Wave& x,const Wave& y){
    Wave z=x;
    for(std::size_t i=0;i<x.left.size();++i){
        z.left[i]-=y.left[i];z.right[i]-=y.right[i];
    }
    return z;
}
// Auditable isolated BACKING roles from the *same* live native PCM DSP.
// Rebuild an independent engine for each role, using one five/singer planner
// and the identical dry lead, then set only that role's prepared output gains
// to zero for every other singer. No browser masks or in-place muting.
static Wave roleStem(const Wave& input,Settings settings,
                     int singerIndex,const Wave& dry){
    if(singerIndex<0||singerIndex>=Engine::maxVoices)
        throw std::runtime_error("invalid solo Harmony role");
    auto e=std::make_unique<Engine>();
    Engine::PreparedSettings prepared{};
    if(!e->prepare(input.rate)||!Engine::prepareSettings(settings,prepared))
        throw std::runtime_error("cannot prepare singer solo");
    for(int i=0;i<Engine::maxVoices;++i)
        if(i!=singerIndex){prepared.mixL[i]=0.f;prepared.mixR[i]=0.f;}
    if(!e->applyPreparedSettings(prepared))
        throw std::runtime_error("cannot apply singer solo");
    Wave out=input;
    for(std::size_t pos=0;pos<out.left.size();pos+=127)
        e->process(out.left.data()+pos,out.right.data()+pos,
                   static_cast<int>(std::min(std::size_t(127),out.left.size()-pos)));
    return subtract(out,dry);
}
static bool same(const Wave& x,const Wave& y){
    if(x.left.size()!=y.left.size())return false;
    for(std::size_t i=0;i<x.left.size();++i)
        if(x.left[i]!=y.left[i]||x.right[i]!=y.right[i])return false;
    return true;
}
static Wave fold(const Wave& in){
    Wave out=in;
    for(std::size_t i=0;i<out.left.size();++i)
        out.left[i]=out.right[i]=.5f*(in.left[i]+in.right[i]);
    return out;
}
static void generateReport(const fs::path& folder,const Wave& input,
             const Wave& dry,const Wave& legacy,const Wave& modern,
             const Wave& backing,const Wave& previousStage,
             const Wave& previousPresence,const Wave& lowStem,
             const Wave& midStem,const Wave& highStem){
    std::ofstream out(folder/"metrics.json");
    out<<"{\n  \"sampleRate\": "<<input.rate<<",\n  \"frames\": "<<input.left.size()
       <<",\n  \"dryRms\": "<<rms(dry)<<",\n  \"legacyRms\": "<<rms(legacy)
       <<",\n  \"synchronousRms\": "<<rms(modern)
       <<",\n  \"backingDifferenceRms\": "<<rms(backing)
       <<",\n  \"previousDepthRms\": "<<rms(previousStage)
       <<",\n  \"previousPresenceRms\": "<<rms(previousPresence)
       <<",\n  \"lowSoloRms\": "<<rms(lowStem)
       <<",\n  \"midSoloRms\": "<<rms(midStem)
       <<",\n  \"highSoloRms\": "<<rms(highStem)
       <<",\n  \"sourceFormantsMeasured\": false,\n  \"realSingerQualified\": false,\n"
       <<"  \"method\": \"same native input, independently reset engines; backing = wet mix - Wet0 lead, correction OFF, gain 0dB\"\n}\n";
}
// M7: Offline-only temporal evidence. RMS activity is NOT voiced-F0 truth.
// These flags are REVIEW CANDIDATES, not automatic acoustic certification.
// The RT callback remains completely unchanged.
struct QualityWindow {double rms=0.,maxStep=0.,stepRatio=0.;};
static QualityWindow qualityWindow(const Wave& w,std::size_t first,std::size_t end){
    double power=0.,derivative=0.,peakStep=0.;
    for(std::size_t i=first;i<end;++i){
        const double l=w.left[i],r=w.right[i];
        power+=l*l+r*r;
        if(i){
            const double dl=l-w.left[i-1],dr=r-w.right[i-1];
            derivative+=dl*dl+dr*dr;
            peakStep=std::max({peakStep,std::abs(dl),std::abs(dr)});
        }
    }
    const double scale=2.*static_cast<double>(end-first);
    return {std::sqrt(power/scale),peakStep,
            peakStep/(std::sqrt(derivative/scale)+1.e-9)};
}
static double maximumWaveDifference(const Wave& a,const Wave& b){
    if(a.rate!=b.rate||a.left.size()!=b.left.size()||
       a.right.size()!=b.right.size())
        throw std::runtime_error("WAV partition dimensions do not match");
    double difference=0.;
    for(std::size_t i=0;i<a.left.size();++i){
        difference=std::max({difference,
            std::abs(double(a.left[i])-b.left[i]),
            std::abs(double(a.right[i])-b.right[i])});
    }
    return difference;
}
static bool nearSilentWithActiveSource(QualityWindow source,QualityWindow role){
    return source.rms>=.025&&role.rms<.0008;
}
static bool abruptStepCandidate(QualityWindow role){
    return role.rms>=.001&&role.maxStep>=.025&&role.stepRatio>=6.;
}
static void writeQualityEvidence(const fs::path& folder,const Wave& input,
                                 const Wave& low,const Wave& mid,const Wave& high,
                                 const Wave& mix127,const Wave& mix64,
                                 const Wave& mix257){
    const auto frames=input.left.size();
    const unsigned rate=input.rate;
    if(!rate||input.right.size()!=frames)
        throw std::runtime_error("invalid M7 source WAV");
    for(const auto* w:{&low,&mid,&high,&mix127,&mix64,&mix257})
        if(w->rate!=rate||w->left.size()!=frames||w->right.size()!=frames)
            throw std::runtime_error("M7 WAV shape mismatch");
    const double diff64=maximumWaveDifference(mix127,mix64);
    const double diff257=maximumWaveDifference(mix127,mix257);
    std::ofstream out(folder/"m7_quality_windows.csv",std::ios::trunc);
    if(!out)throw std::runtime_error("M7 CSV output failed");
    out<<"timeSec,sourceRms,lowRms,midRms,highRms,"
          "lowMaxStep,midMaxStep,highMaxStep,"
          "lowStepRatio,midStepRatio,highStepRatio,"
          "lowNearSilent,midNearSilent,highNearSilent,"
          "lowAbruptCandidate,midAbruptCandidate,highAbruptCandidate\n";
    const std::size_t window=std::max<std::size_t>(1,rate/100); // 10ms
    std::size_t sourceActive=0,lowSilent=0,midSilent=0,highSilent=0;
    std::size_t lowAbrupt=0,midAbrupt=0,highAbrupt=0;
    for(std::size_t start=0;start<frames;start+=window){
        const auto end=std::min(frames,start+window);
        const auto source=qualityWindow(input,start,end);
        const auto a=qualityWindow(low,start,end);
        const auto b=qualityWindow(mid,start,end);
        const auto c=qualityWindow(high,start,end);
        if(source.rms>=.025)++sourceActive;
        const bool sa=nearSilentWithActiveSource(source,a);
        const bool sb=nearSilentWithActiveSource(source,b);
        const bool sc=nearSilentWithActiveSource(source,c);
        const bool ja=abruptStepCandidate(a);
        const bool jb=abruptStepCandidate(b);
        const bool jc=abruptStepCandidate(c);
        lowSilent+=sa;midSilent+=sb;highSilent+=sc;
        lowAbrupt+=ja;midAbrupt+=jb;highAbrupt+=jc;
        out<<double(start)/rate<<','<<source.rms<<','
           <<a.rms<<','<<b.rms<<','<<c.rms<<','
           <<a.maxStep<<','<<b.maxStep<<','<<c.maxStep<<','
           <<a.stepRatio<<','<<b.stepRatio<<','<<c.stepRatio<<','
           <<sa<<','<<sb<<','<<sc<<','<<ja<<','<<jb<<','<<jc<<'\n';
    }
    if(!out)throw std::runtime_error("M7 CSV write failed");
    std::ofstream report(folder/"m7_quality_summary.txt",std::ios::trunc);
    if(!report)throw std::runtime_error("M7 quality summary output failed");
    report<<"M7 offline evidence only; no real singer or physical ASIO approval.\n"
          <<"Source RMS activity is not proof of voiced pitch; consonants may "
            "trigger abrupt-step candidates. Inspect timestamps and listen.\n"
          <<"Window ms: 10; source active RMS >= 0.025; near silent stem RMS < 0.0008.\n"
          <<"Abrupt candidate: stem RMS >= 0.001, peak step >= 0.025, "
            "step-to-local-difference-RMS >= 6.\n"
          <<"Source-active windows: "<<sourceActive<<"\n"
          <<"LOW near-silent candidates: "<<lowSilent
          <<"; abrupt-step candidates: "<<lowAbrupt<<"\n"
          <<"MID near-silent candidates: "<<midSilent
          <<"; abrupt-step candidates: "<<midAbrupt<<"\n"
          <<"HIGH near-silent candidates: "<<highSilent
          <<"; abrupt-step candidates: "<<highAbrupt<<"\n"
          <<"M6 same-source PCM partition max absolute difference (64 vs 127): "
          <<diff64<<"\n"
          <<"M6 same-source PCM partition max absolute difference (257 vs 127): "
          <<diff257<<"\n"
          <<"Partition parity (<= 1e-6): "
          <<((std::max(diff64,diff257)<=1.e-6)?"PASS":"REVIEW")<<"\n"
          <<"Only on-device ASIO/XRUN + singer A/B can complete field acceptance.\n";
    if(!report)throw std::runtime_error("M7 quality summary write failed");
}
static void renderAb(const Wave& input,const fs::path& folder){
    fs::create_directories(folder);
    Settings settings;settings.voices=3;settings.wet=.45f;settings.space=.6f;
    settings.correction=false;settings.gainDb=0.f;
    auto drySettings=settings;drySettings.wet=0.f;
    const Wave dry=process(input,drySettings,false);
    const Wave drySync=process(input,drySettings,true);
    const Wave dryRoomOld=process(input,drySettings,true,127,true,false);
    if(!same(dry,drySync)||!same(dry,dryRoomOld))
        throw std::runtime_error("Lead Wet0 no longer sample-exact");
    const Wave old=process(input,settings,false);
    const Wave modern=process(input,settings,true);
    const Wave partition64=process(input,settings,true,64);
    const Wave partition257=process(input,settings,true,257);
    // A subtraction stem is invalid when the host output hard-clipped.
    for(std::size_t i=0;i<modern.left.size();++i)
        if(std::abs(modern.left[i])>=3.999f||std::abs(modern.right[i])>=3.999f)
            throw std::runtime_error("mixed output clipped: lower recording level for valid stem subtraction");
    const Wave backing=subtract(modern,dry);
    const Wave previousDepth=process(input,settings,true,127,false);
    const Wave previousRoomFocus=process(input,settings,true,127,true,false);
    const Wave previousPresence=process(input,settings,true,127,true,true,false);
    writeWave(folder/"01_lead_reference.wav",dry);
    writeWave(folder/"02_legacy_grain_mix.wav",old);
    writeWave(folder/"03_new_space_060.wav",modern);
    writeWave(folder/"04_new_backing_only.wav",backing);
    // Each solo includes its own direct path, early reflections and the
    // corresponding contribution to the single shared room (not a fake
    // volume-only subtraction). These three files make the owner's HIGH
    // robotic/stutter report independently listenable.
    const auto lowStem=roleStem(input,settings,0,dry);
    const auto midStem=roleStem(input,settings,1,dry);
    const auto highStem=roleStem(input,settings,2,dry);
    writeWave(folder/"14_low_backing_only.wav",lowStem);
    writeWave(folder/"15_mid_backing_only.wav",midStem);
    writeWave(folder/"16_high_backing_only.wav",highStem);
    // Product P1 audition: true harmonic DUO vs a separate SAME-MELODY
    // unison DOUBLER. Both use the identical original vocal and exact dry/PDC.
    // No new audio graph or hidden GUI playback path is involved.
    auto duoSettings=settings;
    duoSettings.voices=2;
    duoSettings.voiceMode=0;
    auto doubleSettings=duoSettings;
    doubleSettings.voiceMode=1;
    auto duoDrySettings=duoSettings;
    duoDrySettings.wet=0.f;
    auto doublerDrySettings=doubleSettings;
    doublerDrySettings.wet=0.f;
    if(!same(process(input,duoDrySettings,true),dry)||
       !same(process(input,doublerDrySettings,true),dry))
        throw std::runtime_error("Duo/Doubler Wet0 changed source lead or PDC");
    const Wave duoMix=process(input,duoSettings,true);
    const Wave doublerMix=process(input,doubleSettings,true);
    const Wave duoBacking=subtract(duoMix,dry);
    const Wave doublerBacking=subtract(doublerMix,dry);
    writeWave(folder/"17_duo_mix.wav",duoMix);
    writeWave(folder/"18_duo_backing_only.wav",duoBacking);
    writeWave(folder/"19_doubler_mix.wav",doublerMix);
    writeWave(folder/"20_doubler_backing_only.wav",doublerBacking);
    {
        std::ofstream metrics(folder/"m7_duo_doubler_comparison.json");
        if(!metrics)throw std::runtime_error("Duo/Doubler report open failed");
        metrics<<"{\n  \"sampleRate\": "<<input.rate
               <<",\n  \"frames\": "<<input.left.size()
               <<",\n  \"duoBackingRms\": "<<rms(duoBacking)
               <<",\n  \"doublerBackingRms\": "<<rms(doublerBacking)
               <<",\n  \"modeDifferenceRms\": "
               <<rms(subtract(duoBacking,doublerBacking))
               <<",\n  \"leadPitchTuneEnabled\": false"
               <<",\n  \"duoVoiceCount\": 2"
               <<",\n  \"doublerUnisonTakeCount\": 1"
               <<",\n  \"realSingerQualified\": false"
               <<",\n  \"physicalAsioQualified\": false\n}\n";
        if(!metrics)throw std::runtime_error("Duo/Doubler report write failed");
    }
    writeWave(folder/"05_new_mono_fold.wav",fold(modern));
    writeWave(folder/"08_previous_depth_space_060.wav",previousDepth);
    writeWave(folder/"09_previous_backing_only.wav",subtract(previousDepth,dry));
    writeWave(folder/"10_room_focus_off_space_060.wav",previousRoomFocus);
    writeWave(folder/"11_room_focus_off_backing_only.wav",subtract(previousRoomFocus,dry));
    writeWave(folder/"12_presence_old_space_060.wav",previousPresence);
    writeWave(folder/"13_presence_old_backing_only.wav",subtract(previousPresence,dry));
    settings.space=0.f;writeWave(folder/"06_new_space_000.wav",process(input,settings,true));
    settings.space=1.f;writeWave(folder/"07_new_space_100.wav",process(input,settings,true));
    writeQualityEvidence(folder,input,lowStem,midStem,highStem,
                         modern,partition64,partition257);
    generateReport(folder,input,dry,old,modern,backing,previousDepth,
                   previousPresence,lowStem,midStem,highStem);
}
int main(int argc,char** argv){
    try{
        if(argc==2&&std::string(argv[1])=="--self-test"){
            Wave v;v.rate=48000;v.left.resize(18000);v.right.resize(18000);
            for(int i=0;i<18000;++i){
                const double ph=6.283185307179586*220.*i/48000.;
                v.left[i]=v.right[i]=static_cast<float>(
                   .13*std::sin(ph)+.06*std::sin(2*ph)+.03*std::sin(3*ph));
            }
            Settings s;s.wet=.45f;
            auto before=process(v,s,false);
            auto after=process(v,s,true);
            if(!(rms(before)>0.01&&rms(after)>0.01))return 2;
            // M7 classifier controls: steady harmonics must not be called
            // stutter, whereas injected one-sample spikes and dropouts must
            // be observable. No synthetic result certifies human naturalness.
            const auto steady=qualityWindow(v,6000,6480);
            if(nearSilentWithActiveSource(steady,steady)||
               abruptStepCandidate(steady))return 8;
            Wave spiked=v;
            spiked.left[6005]+=.9f;spiked.right[6005]-=.9f;
            if(!abruptStepCandidate(qualityWindow(spiked,6000,6480)))
                return 9;
            Wave silent=v;
            for(int i=6000;i<6480;++i)
                silent.left[i]=silent.right[i]=0.f;
            if(!nearSilentWithActiveSource(
                steady,qualityWindow(silent,6000,6480)))return 10;
            if(maximumWaveDifference(process(v,s,true,64),
                                     process(v,s,true,257))>1.e-6)return 11;
            // Isolated LOW/MID/HIGH renders must recombine with the
            // simultaneously rendered (unclipped) full wet backing, with
            // only bounded float rounding in the shared linear room.
            Settings dryVoiceSettings=s;
            dryVoiceSettings.wet=0.f;
            const auto dryRole=process(v,dryVoiceSettings,true);
            const auto full=process(v,s,true);
            const auto lowRole=roleStem(v,s,0,dryRole);
            const auto midRole=roleStem(v,s,1,dryRole);
            const auto highRole=roleStem(v,s,2,dryRole);
            double parts=0.;
            for(std::size_t i=0;i<v.left.size();++i){
                const float l=lowRole.left[i]+midRole.left[i]+highRole.left[i];
                const float r=lowRole.right[i]+midRole.right[i]+highRole.right[i];
                const float wholeL=full.left[i]-dryRole.left[i];
                const float wholeR=full.right[i]-dryRole.right[i];
                if(std::abs(l-wholeL)>2.e-4f||
                   std::abs(r-wholeR)>2.e-4f)return 6;
                parts+=std::abs(highRole.left[i])+std::abs(highRole.right[i]);
            }
            if(!(parts>10.))return 7;
            // Native PCM proves 2 pitched DUO and 1 unison DOUBLER
            // are genuinely different; Wet0 remains exact delayed dry.
            Settings duo=s;duo.voices=2;duo.voiceMode=0;duo.wet=.75f;
            Settings doubler=duo;doubler.voiceMode=1;
            const auto duoMix=process(v,duo,true);
            const auto doubleMix=process(v,doubler,true);
            auto modeDry=duo;modeDry.wet=0.f;
            const auto modeReference=process(v,modeDry,true);
            auto doublerDry=doubler;doublerDry.wet=0.f;
            if(!same(modeReference,process(v,doublerDry,true)))return 12;
            if(!(rms(subtract(duoMix,modeReference))>.0001&&
                 rms(subtract(doubleMix,modeReference))>.0001&&
                 rms(subtract(duoMix,doubleMix))>.0001))return 13;
            s.wet=1.f;s.space=.6f;s.voices=4;
            const auto baseline=process(v,s,true,127,true,true,false);
            const auto boosted=process(v,s,true,127,true,true,true);
            auto drySettings=s;drySettings.wet=0.f;
            const auto dry=process(v,drySettings,true);
            const double beforeRms=rms(subtract(baseline,dry));
            const double afterRms=rms(subtract(boosted,dry));
            if(!(beforeRms>.0001&&afterRms>beforeRms*1.50&&
                 afterRms<beforeRms*1.90))return 5;
            s.wet=0.f;
            if(!same(process(v,s,false),process(v,s,true))||
               !same(process(v,s,true,127,true,true,false),
                     process(v,s,true,127,true,true,true)))return 3;
            if(!same(process(v,s,true,127,false),
                     process(v,s,true,127,true)))return 4;
            std::cout<<"Offline native A/B test passed (synthetic only)\n";
            return 0;
        }
        if(argc==6&&std::string(argv[1])=="--lead-tune"){
            const auto input=readWave(argv[2]);
            const fs::path folder=argv[3];
            fs::create_directories(folder);
            Settings s;
            s.wet=0.f;s.key=std::stoi(argv[4]);s.scale=std::stoi(argv[5]);
            const auto dry=process(input,s,true);
            s.correction=true;
            const auto tuned=process(input,s,true);
            writeWave(folder/"lead-dry.wav",dry);
            writeWave(folder/"lead-pitch-tune.wav",tuned);
            s.correction=false;s.wet=.45f;
            const auto mix=process(input,s,true);
            writeWave(folder/"lead-and-backing.wav",mix);
            writeWave(folder/"backing-only.wav",subtract(mix,dry));
            return 0;
        }
        if(argc!=3){
            std::cerr<<"usage: harmony_offline_ab input.wav output_directory\n"
                     <<"       harmony_offline_ab --lead-tune input.wav output_directory key scale\n";
            return 1;
        }
        renderAb(readWave(argv[1]),argv[2]);
        std::cout<<"WAV A/B written. No human-singing/formant qualification implied.\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"Harmony A/B: "<<e.what()<<"\n";return 4;}
}
