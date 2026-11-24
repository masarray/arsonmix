// SPDX-License-Identifier: AGPL-3.0-only
// Offline native Harmony A/B: genuine adult vocal WAVs, no live callback IO.
// Synthesized self-test is only a build and integrity gate, not an ear-test.
#include "../engine/builtin_fx/HarmonyDsp.h"
#include "../engine/builtin_fx/HarmonyMusicKey.h"
#include <algorithm>
#include <array>
#include <chrono>
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
// Precompute the *native* detector at a declared callback cadence.
// Every A/B variant and solo receives exactly the same tonal events.
// This is a synchronous source replay, not a JUCE graph/driver simulation.
struct GuideFrame {
    std::size_t at=0,end=0;
    armix::harmony::MusicKeyEstimate key{};
    armix::harmony::MusicChordEstimate chord{};
};
using GuideTimeline=std::vector<GuideFrame>;
static GuideTimeline guideTimeline(const Wave& vocal,const Wave& music,int block){
    if(vocal.rate!=music.rate||vocal.left.size()!=music.left.size()||
       music.left.size()!=music.right.size()||vocal.left.empty()||
       block<16||block>2048)
        throw std::runtime_error("Music guide requires aligned same-rate/same-length WAVs and block 16..2048");
    if(vocal.left.size()>static_cast<std::size_t>(vocal.rate)*30)
        throw std::runtime_error("Music guide audition: use <=30s");
    armix::harmony::MusicKeyDetector detector;
    if(!detector.prepare(music.rate))throw std::runtime_error("Music guide prepare failed");
    GuideTimeline out;out.reserve((music.left.size()+block-1)/block);
    for(std::size_t at=0;at<music.left.size();at+=static_cast<std::size_t>(block)){
        const auto end=std::min(music.left.size(),at+static_cast<std::size_t>(block));
        detector.process(music.left.data()+at,music.right.data()+at,static_cast<int>(end-at));
        out.push_back({at,end,detector.estimate(),detector.chordEstimate()});
    }
    return out;
}
static void applyGuide(Engine& e,const GuideFrame& f){
    // Match ChannelHarmonyProcessor's order and explicit unknown values.
    e.setGuideChord(f.chord.valid()?f.chord.root:-1,f.chord.valid()?f.chord.quality:-1);
    e.setGuideKey(f.key.valid()?f.key.key:-1,f.key.valid()?f.key.scale:-1);
}
static void writeGuideTimeline(const fs::path& file,const GuideTimeline& guide,unsigned rate){
    std::ofstream out(file,std::ios::trunc);
    if(!out)throw std::runtime_error("Music guide report open failed");
    out<<"callback_start_sample,callback_end_sample,callback_start_s,key,scale,key_confidence,chord_root,chord_quality,chord_confidence\n";
    for(const auto& f:guide)
        out<<f.at<<','<<f.end<<','<<double(f.at)/rate<<','<<f.key.key<<','
           <<f.key.scale<<','<<f.key.confidence<<','<<f.chord.root<<','
           <<f.chord.quality<<','<<f.chord.confidence<<'\n';
    if(!out)throw std::runtime_error("Music guide report write failed");
}
using SeamCounts=std::array<armix::harmony::BackingRenderSeam::Counts,Engine::maxVoices>;
struct TraceRow {std::size_t at=0;std::array<Engine::RendererTrace,Engine::maxVoices> counts{};SeamCounts seams{};};
static Wave process(const Wave& input,Settings settings,bool synchronous,int block=127,
                    bool enhancedDepth=true,bool focus=true,bool presence=true,
                    bool articulation=true,bool continuity=true,bool support=true,bool natural=true,bool carrier=true,bool slope=true,
                    bool drain=true,SeamCounts* counts=nullptr,std::vector<TraceRow>* trace=nullptr,bool recovery=true,bool cubic=false,const GuideTimeline* guide=nullptr,int aligned=0){
    auto e=std::make_unique<Engine>();
    if(!e->prepare(input.rate)||!e->setDoublerFastForOfflineReference(false)||!e->setHeadroomForOfflineReference(false)||!e->setDoublerMonoForOfflineReference(false)||!e->setSettings(settings))
        throw std::runtime_error("native Harmony configuration invalid");
    if(!e->setArticulationForOfflineReference(articulation))
        throw std::runtime_error("H9 reference rejected");
    if(!e->setContinuityForOfflineReference(continuity)||
       !e->setSupportForOfflineReference(support)||
       !e->setNaturalForOfflineReference(natural)||!e->setCarrierForOfflineReference(carrier)||
       !e->setSlopeForOfflineReference(slope)||!e->setDrainForOfflineReference(drain)||
       !e->setEpochRecoveryForOfflineReference(recovery)||!e->setCubicForOfflineReference(cubic)||!e->setSourceAlignedForOfflineReference(aligned!=0)||!e->setSourceConfirmationForOfflineReference(aligned==2))
        throw std::runtime_error("continuity reference rejected");
    e->setPitchSynchronousCandidateForOfflineReference(synchronous);
    e->setStageDepthForOfflineReference(enhancedDepth);
    e->setRoomFocusForOfflineReference(focus);
    if(!e->setBackingPresenceForOfflineReference(presence))
        throw std::runtime_error("Harmony presence offline reference rejected");
    if(trace&&!e->setRendererTraceForOfflineTests(true))
        throw std::runtime_error("renderer trace rejected");
    Wave out=input;
    const std::size_t window=std::max<std::size_t>(1,input.rate/100);
    TraceRow prev{};std::size_t start=0,guideIndex=0;
    for(std::size_t pos=0;pos<out.left.size();){
        const auto end=trace?std::min(out.left.size(),start+window):out.left.size();
        auto limit=end;
        if(guide){
            const auto& f=guide->at(guideIndex);
            if(pos==f.at)applyGuide(*e,f);
            limit=std::min(limit,f.end);
        }
        const auto n=std::min(static_cast<std::size_t>(block),limit-pos);
        e->process(out.left.data()+pos,out.right.data()+pos,static_cast<int>(n));
        pos+=n;
        if(guide&&pos==guide->at(guideIndex).end)++guideIndex;
        if(trace&&pos==end){
            TraceRow row{};row.at=start;
            for(int v=0;v<Engine::maxVoices;++v){
                const auto q=e->rendererTraceForOfflineTests(v);
                const auto c=e->seamCountsForOfflineTests(v);
                row.counts[v]=q.delta(prev.counts[v]);prev.counts[v]=q;
                row.seams[v]={c.losses-prev.seams[v].losses,c.acquisitions-prev.seams[v].acquisitions};
                prev.seams[v]=c;
            }
            trace->push_back(row);start=end;
        }
    }
    if(counts)for(int v=0;v<Engine::maxVoices;++v)(*counts)[v]=e->seamCountsForOfflineTests(v);
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
                     int singerIndex,const Wave& dry,bool natural=true,bool carrier=true,bool slope=true,bool drain=true,bool recovery=true,bool cubic=false,const GuideTimeline* guide=nullptr,int aligned=0){
    if(singerIndex<0||singerIndex>=Engine::maxVoices)
        throw std::runtime_error("invalid solo Harmony role");
    auto e=std::make_unique<Engine>();
    Engine::PreparedSettings prepared{};
    if(!e->prepare(input.rate)||!e->setDoublerFastForOfflineReference(false)||!e->setHeadroomForOfflineReference(false)||!e->setDoublerMonoForOfflineReference(false)||!e->setNaturalForOfflineReference(natural)||!e->setCarrierForOfflineReference(carrier)||!e->setSlopeForOfflineReference(slope)||!e->setDrainForOfflineReference(drain)||!e->setEpochRecoveryForOfflineReference(recovery)||!e->setCubicForOfflineReference(cubic)||!e->setSourceAlignedForOfflineReference(aligned!=0)||!e->setSourceConfirmationForOfflineReference(aligned==2)||
       !Engine::prepareSettings(settings,prepared))
        throw std::runtime_error("cannot prepare singer solo");
    for(int i=0;i<Engine::maxVoices;++i)
        if(i!=singerIndex){prepared.mixL[i]=0.f;prepared.mixR[i]=0.f;}
    if(!e->applyPreparedSettings(prepared))
        throw std::runtime_error("cannot apply singer solo");
    Wave out=input;
    std::size_t guideIndex=0;
    for(std::size_t pos=0;pos<out.left.size();){
        auto end=out.left.size();
        if(guide){
            const auto& f=guide->at(guideIndex);
            if(pos==f.at)applyGuide(*e,f);
            end=f.end;
        }
        const auto n=std::min(std::size_t(127),end-pos);
        e->process(out.left.data()+pos,out.right.data()+pos,static_cast<int>(n));
        pos+=n;
        if(guide&&pos==guide->at(guideIndex).end)++guideIndex;
    }
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
    // H9 matched five-singer audition: no normalization, identical
    // input, settings and declared delay, only articulation differs.
    auto five=settings;five.voices=5;five.wet=1.f;
    const auto beforeH9=process(input,five,true,127,true,true,true,false);
    const auto afterH9=process(input,five,true);
    writeWave(folder/"21_h8_five_mix.wav",beforeH9);
    writeWave(folder/"22_h9_five_mix.wav",afterH9);
    writeWave(folder/"23_h8_five_backing.wav",subtract(beforeH9,dry));
    writeWave(folder/"24_h9_five_backing.wav",subtract(afterH9,dry));
    for(int role=0;role<5;++role)
        writeWave(folder/("h9_role_"+std::to_string(role)+".wav"),
                  roleStem(input,five,role,dry));
    // Temporal evidence must compare source at the output's declared
    // timeline. The old M7 analysis compared undelayed input.
    const auto delayedSource=process(input,drySettings,true);
    settings.space=0.f;writeWave(folder/"06_new_space_000.wav",process(input,settings,true));
    settings.space=1.f;writeWave(folder/"07_new_space_100.wav",process(input,settings,true));
    writeQualityEvidence(folder,delayedSource,lowStem,midStem,highStem,
                         modern,partition64,partition257);
    generateReport(folder,input,dry,old,modern,backing,previousDepth,
                   previousPresence,lowStem,midStem,highStem);
}
// Per-ear metrics keep amplitude, discontinuity proxy and full-scale
// overload separate. No normalization of audition WAVs. A normalized
// maximum derivative is a diagnostic, not a validated crackle detector.
struct EarEvidence {double rms=0.,peak=0.,maxStep=0.,stepRms=0.;std::size_t overload=0;};
static EarEvidence earEvidence(const std::vector<float>& x,std::size_t start,std::size_t end){
    EarEvidence e;
    if(start>=end||end>x.size())return e;
    double energy=0.,stepEnergy=0.;
    for(std::size_t i=start;i<end;++i){
        const double v=x[i];
        if(!std::isfinite(v))throw std::runtime_error("nonfinite audition PCM");
        energy+=v*v;e.peak=std::max(e.peak,std::abs(v));
        if(std::abs(v)>=1.)++e.overload;
        if(i>0){
            const double step=v-x[i-1];stepEnergy+=step*step;
            e.maxStep=std::max(e.maxStep,std::abs(step));
        }
    }
    e.rms=std::sqrt(energy/(end-start));
    e.stepRms=std::sqrt(stepEnergy/std::max<std::size_t>(1,end-start-(start==0?1:0)));
    return e;
}
static void continuityReport(const Wave& input,const fs::path& folder,Settings s,const GuideTimeline* guide=nullptr,int guideBlock=0){
    if(input.left.size()>static_cast<std::size_t>(input.rate)*30)
        throw std::runtime_error("continuity audition: use <=30s");
    fs::create_directories(folder);
    auto ds=s;ds.wet=0.f;
    if(guide){
        if(s.guideMode!=1||s.guideChannelId==0)throw std::runtime_error("Music guide settings mismatch");
        writeGuideTimeline(folder/"guide_timeline.csv",*guide,input.rate);
    }
    // Tune OFF in this report; guide influences backing only. Render dry
    // with the identical timeline anyway to detect any future regression.
    const auto dry=process(input,ds,true,127,true,true,true,true,true,true,true,true,true,true,nullptr,nullptr,true,false,guide);
    std::ofstream manifest(folder/"replay_manifest.txt",std::ios::trunc);
    if(!manifest)throw std::runtime_error("replay manifest open failed");
    manifest<<"mode="<<(guide?"Music":"Manual")<<"\nrate="<<input.rate
            <<"\nframes="<<input.left.size()<<"\nvoices="<<s.voices
            <<"\nwet="<<s.wet<<"\nspace="<<s.space<<"\ncolor="<<s.color
            <<"\ntune="<<s.correction<<"\nmanual_key="<<s.key<<"\nmanual_scale="<<s.scale
            <<"\nlatency_samples="<<std::lround(input.rate*.024)
            <<"\nguide_callback_frames="<<guideBlock
            <<"\nH20_H21_reference_uses_optimized_search=true\nH22_offline_candidate=true\nH24C_offline_candidate=true\nH25_source_confirmation_offline_candidate=true\nH24A_B_guards_disabled_for_renderer_comparison=true\n";
    if(!manifest)throw std::runtime_error("replay manifest write failed");
    writeWave(folder/"dry_aligned.wav",dry);
    std::ofstream csv(folder/"continuity_10ms.csv",std::ios::trunc);
    if(!csv)throw std::runtime_error("continuity report open failed");
    csv<<"variant,channel,output_time_s,source_time_s,dry_rms,wet_rms,wet_peak,"
          "wet_max_step,wet_step_rms,wet_step_over_rms,mix_peak,mix_overload_samples\n";
    const auto window=std::max<std::size_t>(1,input.rate/100);
    const double delay=std::lround(input.rate*.024)/double(input.rate);
    std::ofstream events(folder/"seam_events.csv",std::ios::trunc);
    if(!events)throw std::runtime_error("seam event report open failed");
    events<<"variant,role,losses,acquisitions\n";
    std::ofstream traceCsv(folder/"renderer_trace_10ms.csv",std::ios::trunc);
    if(!traceCsv)throw std::runtime_error("renderer trace open failed");
    traceCsv<<"variant,role,output_time_s,source_time_s,calls,no_clock,causal_blocked,drained,safe_unready,ready,partial,weak_confidence,epoch_recovered,recovery_candidate_sites,losses,acquisitions\n";
    for(int version:{12,13,14,15,16,17,18,20,22,24,25}){
        SeamCounts counts{};std::vector<TraceRow> trace;
        const auto mix=process(input,s,true,127,true,true,true,true,
                               version>=13,version>=14,version>=15,version>=16,version>=17,version>=18,&counts,&trace,version>=20,version==22,guide,version==25?2:version==24?1:0);
        const auto backing=subtract(mix,dry);
        const auto tag="h"+std::to_string(version);
        for(int v=0;v<Engine::maxVoices;++v)
            events<<tag<<','<<v<<','<<counts[v].losses<<','<<counts[v].acquisitions<<'\n';
        for(const auto& row:trace)for(int v=0;v<Engine::maxVoices;++v){
            traceCsv<<tag<<','<<v<<','<<double(row.at)/input.rate<<','
                    <<double(row.at)/input.rate-delay;
            for(auto q:row.counts[v].values)traceCsv<<','<<q;
            traceCsv<<','<<row.seams[v].losses<<','<<row.seams[v].acquisitions<<'\n';
        }
        writeWave(folder/(tag+"_mix.wav"),mix);
        writeWave(folder/(tag+"_backing.wav"),backing);
        for(int channel:{0,1}){
            const auto& a=channel?dry.right:dry.left;
            const auto& b=channel?backing.right:backing.left;
            const auto& c=channel?mix.right:mix.left;
            for(std::size_t at=0;at<a.size();at+=window){
                const auto end=std::min(a.size(),at+window);
                const auto x=earEvidence(a,at,end),y=earEvidence(b,at,end),z=earEvidence(c,at,end);
                csv<<tag<<','<<(channel?"R":"L")<<','<<double(at)/input.rate<<','
                   <<double(at)/input.rate-delay<<','<<x.rms<<','<<y.rms<<','<<y.peak<<','
                   <<y.maxStep<<','<<y.stepRms<<','<<(y.rms>1.e-8?y.maxStep/y.rms:0.)<<','
                   <<z.peak<<','<<z.overload<<'\n';
            }
        }
    }
    for(int version:{14,15,16,17,18,20,22,24,25})for(int role=0;role<Engine::maxVoices;++role)
        writeWave(folder/("h"+std::to_string(version)+"_role_"+std::to_string(role)+".wav"),
                  roleStem(input,s,role,dry,version>=15,version>=16,version>=17,version>=18,version>=20,version==22,guide,version==25?2:version==24?1:0));
    if(!csv||!events||!traceCsv)throw std::runtime_error("continuity report write failed");
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
            const auto quiet=earEvidence(v.right,6000,6480);
            // Put the impulse at the exact analysis window boundary:
            // the report must not silently miss a callback-aligned click.
            auto spike=v.left;spike[6000]=1.2f;
            const auto noisy=earEvidence(spike,6000,6480);
            if(quiet.overload||noisy.overload!=1||noisy.maxStep<=quiet.maxStep*4.)return 14;
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
            if(!(beforeRms>.0001&&afterRms>beforeRms*1.50*Engine::backingUnityGain(s.wet,s.space,s.voiceMode,s.voices)&&
                 afterRms<beforeRms*1.90*Engine::backingUnityGain(s.wet,s.space,s.voiceMode,s.voices)))return 5;
            s.wet=0.f;
            if(!same(process(v,s,false),process(v,s,true))||
               !same(process(v,s,true,127,true,true,false),
                     process(v,s,true,127,true,true,true)))return 3;
            if(!same(process(v,s,true,127,false),
                     process(v,s,true,127,true)))return 4;
            // Exercise the actual file-export path, not just its metrics.
            const auto folder=fs::temp_directory_path()/("armix-harmony-ab-"+
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            if(!fs::create_directory(folder))return 15;
            struct Cleanup{fs::path path;~Cleanup(){std::error_code ec;fs::remove_all(path,ec);}} cleanup{folder};
            Settings reportSettings;reportSettings.voices=3;reportSettings.wet=1.f;
            reportSettings.space=1.f;reportSettings.color=.78f;
            continuityReport(v,folder,reportSettings);
            for(int version:{12,13,14,15,16,17,18,20,22,24,25}){
                const auto tag="h"+std::to_string(version);
                const auto mix=readWave(folder/(tag+"_mix.wav"));
                const auto backing=readWave(folder/(tag+"_backing.wav"));
                if(mix.rate!=v.rate||mix.left.size()!=v.left.size()||
                   backing.left.size()!=v.left.size()||rms(backing)<.001)return 16;
            }
            std::ifstream ev(folder/"seam_events.csv");
            std::string line;int rows=0;
            while(std::getline(ev,line))++rows;
            if(rows!=1+11*Engine::maxVoices)return 17;
            if(!fs::exists(folder/"h18_role_2.wav"))return 18;
            std::ifstream tr(folder/"renderer_trace_10ms.csv");int traceRows=0;
            while(std::getline(tr,line))++traceRows;
            const auto windows=(v.left.size()+v.rate/100-1)/(v.rate/100);
            if(static_cast<std::size_t>(traceRows)!=1+11*Engine::maxVoices*windows)return 19;
            std::cout<<"H19_TRACE_EXPORT rows="<<traceRows-1<<std::endl;
            std::cout<<"H24C_EXPORT variants=11 event_rows="<<rows-1<<" WAV readback PASS\n";
            // True native music detector, independent stereo WAV, two
            // chords, fixed 64-frame guide cadence. Trace window splits
            // and renderer block partitions must not move tonal events.
            Wave vocal,music;vocal.rate=music.rate=48000;
            vocal.left.resize(2*48000);vocal.right.resize(vocal.left.size());
            music.left.resize(vocal.left.size());music.right.resize(vocal.left.size());
            constexpr double tau=6.283185307179586;
            for(std::size_t i=0;i<vocal.left.size();++i){
                const double sec=double(i)/vocal.rate;
                vocal.left[i]=vocal.right[i]=static_cast<float>(.14*std::sin(tau*220.*sec)+.04*std::sin(tau*440.*sec));
                const std::array<double,3> hz=i<48000?std::array<double,3>{261.625565,329.627557,391.995436}:
                    std::array<double,3>{349.228231,440.,523.251131};
                double q=0.;for(auto f:hz)q+=.08*std::sin(tau*f*sec);
                music.left[i]=music.right[i]=static_cast<float>(q);
            }
            const auto timeline=guideTimeline(vocal,music,64);
            bool cMajor=false,fMajor=false;
            for(const auto& f:timeline){
                cMajor|=f.chord.valid()&&f.chord.root==0&&f.chord.quality==0;
                fMajor|=f.chord.valid()&&f.chord.root==5&&f.chord.quality==0;
            }
            if(!cMajor||!fMajor||timeline.size()!=1500)return 20;
            Settings gs=reportSettings;gs.guideMode=1;gs.guideChannelId=2;
            auto guided=[&](Settings settings,int block,std::vector<TraceRow>* trace,const GuideTimeline* g){
                return process(vocal,settings,true,block,true,true,true,true,true,true,true,true,true,true,nullptr,trace,true,false,g);
            };
            std::vector<TraceRow> gt;
            const auto m=guided(gs,127,&gt,&timeline),m64=guided(gs,64,nullptr,&timeline);
            const auto manual=guided(gs,127,nullptr,nullptr);
            if(!same(m,m64)||same(m,manual))return 21;
            gs.wet=0.f;
            if(!same(guided(gs,127,nullptr,&timeline),guided(gs,64,nullptr,nullptr)))return 22;
            writeGuideTimeline(folder/"guide_timeline.csv",timeline,vocal.rate);
            std::ifstream gc(folder/"guide_timeline.csv");int guideRows=0;
            while(std::getline(gc,line))++guideRows;
            if(guideRows!=1501)return 23;
            bool rejected=false;
            try{auto bad=music;bad.rate=96000;(void)guideTimeline(vocal,bad,64);}
            catch(const std::runtime_error&){rejected=true;}
            if(!rejected)return 24;
            std::cout<<"H23_MUSIC_REPLAY callbacks="<<timeline.size()<<" C/F major PASS, exact PCM partition and dry parity, independent guide\n";
            std::cout<<"Offline native A/B test passed (synthetic only)\n";
            return 0;
        }
        if(argc==9&&std::string(argv[1])=="--continuity-music"){
            const auto vocal=readWave(argv[2]),music=readWave(argv[3]);
            Settings s;s.key=std::stoi(argv[5]);s.scale=std::stoi(argv[6]);
            s.voices=std::stoi(argv[7]);s.wet=1.f;s.space=1.f;s.color=.78f;
            s.guideMode=1;s.guideChannelId=2;
            const int block=std::stoi(argv[8]);
            const auto timeline=guideTimeline(vocal,music,block);
            continuityReport(vocal,argv[4],s,&timeline,block);
            std::cout<<"Matched Music-guide WAV, PCM and detector timeline written; no hardware or acoustic qualification implied.\n";
            return 0;
        }
        if(argc==7&&std::string(argv[1])=="--continuity"){
            Settings s;s.key=std::stoi(argv[4]);s.scale=std::stoi(argv[5]);
            s.voices=std::stoi(argv[6]);s.wet=1.f;s.space=1.f;s.color=.78f;
            continuityReport(readWave(argv[2]),argv[3],s);
            std::cout<<"H12/H13/H14/H15/H16/H17/H18/H20/H22 matched manual-key WAV and per-ear CSV written; no acoustic qualification implied.\n";
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
                     <<"       harmony_offline_ab --lead-tune input.wav output_directory key scale\n"
                     <<"       harmony_offline_ab --continuity input.wav output_directory key scale voices\n"
                     <<"       harmony_offline_ab --continuity-music vocal.wav music.wav output_directory key scale voices callback_frames\n";
            return 1;
        }
        renderAb(readWave(argv[1]),argv[2]);
        std::cout<<"WAV A/B written. No human-singing/formant qualification implied.\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"Harmony A/B: "<<e.what()<<"\n";return 4;}
}
