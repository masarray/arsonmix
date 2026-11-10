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
                    bool enhancedDepth=true,bool focus=true,bool presence=true,
                    int offlineLatencyMs=32){
    auto e=std::make_unique<Engine>();
    const bool prepared=offlineLatencyMs==32?e->prepare(input.rate):
        e->prepareForOfflineLatencyStudy(input.rate,offlineLatencyMs);
    if(!prepared||!e->setSettings(settings))
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
             const Wave& previousPresence){
    std::ofstream out(folder/"metrics.json");
    out<<"{\n  \"sampleRate\": "<<input.rate<<",\n  \"frames\": "<<input.left.size()
       <<",\n  \"dryRms\": "<<rms(dry)<<",\n  \"legacyRms\": "<<rms(legacy)
       <<",\n  \"synchronousRms\": "<<rms(modern)
       <<",\n  \"backingDifferenceRms\": "<<rms(backing)
       <<",\n  \"previousDepthRms\": "<<rms(previousStage)
       <<",\n  \"previousPresenceRms\": "<<rms(previousPresence)
       <<",\n  \"sourceFormantsMeasured\": false,\n  \"realSingerQualified\": false,\n"
       <<"  \"method\": \"same native input, independently reset engines; backing = wet mix - Wet0 lead, correction OFF, gain 0dB\"\n}\n";
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
    writeWave(folder/"05_new_mono_fold.wav",fold(modern));
    writeWave(folder/"08_previous_depth_space_060.wav",previousDepth);
    writeWave(folder/"09_previous_backing_only.wav",subtract(previousDepth,dry));
    writeWave(folder/"10_room_focus_off_space_060.wav",previousRoomFocus);
    writeWave(folder/"11_room_focus_off_backing_only.wav",subtract(previousRoomFocus,dry));
    writeWave(folder/"12_presence_old_space_060.wav",previousPresence);
    writeWave(folder/"13_presence_old_backing_only.wav",subtract(previousPresence,dry));
    settings.space=0.f;writeWave(folder/"06_new_space_000.wav",process(input,settings,true));
    settings.space=1.f;writeWave(folder/"07_new_space_100.wav",process(input,settings,true));
    generateReport(folder,input,dry,old,modern,backing,previousDepth,previousPresence);
}
// Exploratory, physical source-to-source grain study. Candidate WAVs are
// delayed to the ORIGINAL 32 ms lead arrival before perceptual A/B. This
// alignment is offline only and does not hide or change the candidate's
// declared PDC. RMS deltas cannot prove formant transparency or naturalness.
static Wave delayedCopy(const Wave& in,int frames){
    if(frames<0)throw std::runtime_error("negative offline alignment");
    Wave out=in;
    for(std::size_t i=in.left.size();i-->0;){
        out.left[i]=i>=static_cast<std::size_t>(frames)?
            in.left[i-static_cast<std::size_t>(frames)]:0.f;
        out.right[i]=i>=static_cast<std::size_t>(frames)?
            in.right[i-static_cast<std::size_t>(frames)]:0.f;
    }
    return out;
}
static double alignedDifferenceRms(const Wave& a,const Wave& b,int skip){
    if(a.left.size()!=b.left.size()||skip<0)
        throw std::runtime_error("unmatched WAV alignment");
    double sum=0.;std::size_t used=0;
    for(std::size_t i=static_cast<std::size_t>(skip);i<a.left.size();++i){
        const double l=double(a.left[i])-b.left[i];
        const double r=double(a.right[i])-b.right[i];
        sum+=l*l+r*r;used+=2;
    }
    return used?std::sqrt(sum/static_cast<double>(used)):0.;
}
// Lightweight offline diagnostics of the ALIGNED output. These are not
// perceptual scores and must never enter the realtime audio callback.
static double sideRms(const Wave& v){
    double sum=0.;
    for(std::size_t i=0;i<v.left.size();++i){
        const double side=.5*(double(v.left[i])-v.right[i]);
        sum+=side*side;
    }
    return v.left.empty()?0.:std::sqrt(sum/v.left.size());
}
static double peakAbsolute(const Wave& v){
    double peak=0.;
    for(std::size_t i=0;i<v.left.size();++i)
        peak=std::max(peak,std::max(std::abs(double(v.left[i])),
                                     std::abs(double(v.right[i]))));
    return peak;
}
static double largestOneSampleStep(const Wave& v){
    double maximum=0.;
    for(std::size_t i=1;i<v.left.size();++i)
        maximum=std::max(maximum,std::max(
            std::abs(double(v.left[i])-v.left[i-1]),
            std::abs(double(v.right[i])-v.right[i-1])));
    return maximum;
}
// A reproducible non-human signal with sustained/soft vowel harmonics,
// quiet sections, onset/release and deterministic breath-like noise. It is
// deliberately NOT a substitute for original male/female singer recordings.
static Wave syntheticVocal(unsigned rate,double hz,bool antiphase){
    const int frames=static_cast<int>(rate*1.60);
    Wave out;out.rate=rate;
    out.left.resize(static_cast<std::size_t>(frames));
    out.right.resize(static_cast<std::size_t>(frames));
    constexpr double twoPi=6.2831853071795864769;
    std::uint32_t noise=0x574f4341u; // repeatable across Windows/Linux
    for(int i=0;i<frames;++i){
        const double t=static_cast<double>(i)/rate;
        // A brief consonant-like noise onset and periodic late release.
        double amp=0.;
        if(t<.05)amp=.11*(t/.05);
        else if(t<.85)amp=.11;
        else if(t<1.02)amp=.11*(1.02-t)/.17;
        else if(t<1.18)amp=0.;
        else if(t<1.22)amp=.095*(t-1.18)/.04;
        else if(t<1.48)amp=.095;
        else amp=.095*std::max(0.,(1.60-t)/.12);
        const double phase=twoPi*hz*t+(.015*std::sin(twoPi*4.4*t));
        const double vowel=std::sin(phase)+
            .40*std::sin(2*phase+.2)+.19*std::sin(3*phase+.31)+
            .09*std::sin(4*phase+.12);
        noise=noise*1664525u+1013904223u;
        const double white=double((noise>>8)&0xffffu)/32767.5-1.;
        const double burst=(t>=.015&&t<.055)||(t>=1.17&&t<1.21)?
            .015*white:0.;
        const float sample=static_cast<float>(amp*vowel+burst);
        const auto at=static_cast<std::size_t>(i);
        out.left[at]=sample;
        out.right[at]=antiphase?-sample:sample;
    }
    return out;
}
static void renderLatencyStudy(const Wave& input,const fs::path& folder);
static void makeSyntheticCorpus(const fs::path& folder){
    // Names disclose synthetic origin so nobody mistakes this package for
    // male/female human-singer listening or formant preservation evidence.
    fs::create_directories(folder);
    struct Fixture {const char* name;double hz;bool antiphase;};
    for(const auto& cfg:std::array<Fixture,4>{{
        {"SYNTHETIC_low_C2_65Hz",65.,false},
        {"SYNTHETIC_mid_A3_220Hz",220.,false},
        {"SYNTHETIC_high_A4_440Hz",440.,false},
        {"SYNTHETIC_antiphase_A3_220Hz",220.,true}
    }}){
        const auto input=syntheticVocal(48000,cfg.hz,cfg.antiphase);
        writeWave(folder/(std::string(cfg.name)+".wav"),input);
        renderLatencyStudy(input,folder/cfg.name);
    }
    std::ofstream description(folder/"READ_ME_FIRST.txt");
    description<<"SYNTHETIC Harmonice DSP/A-B diagnostics ONLY.\n"
                 "These generated waveforms are NOT recordings of humans,\n"
                 "cannot qualify naturalness, real-singer formants, consonants,\n"
                 "live ASIO round-trip latency, crackles or physical XRUNs.\n"
                 "Listen to aligned 32/24/16ms WAVs as a debugging aid only.\n"
                 "For acoustic acceptance, rerun --latency-study with genuine\n"
                 "consented adult-singer WAVs at identical source settings.\n";
    if(!description)throw std::runtime_error("corpus README write failed");
}
static void renderLatencyStudy(const Wave& input,const fs::path& folder){
    fs::create_directories(folder);
    Settings settings;
    settings.voices=5;settings.wet=.65f;settings.space=.60f;
    settings.correction=false;settings.gainDb=0.f;
    auto dry=settings;dry.wet=0.f;
    const Wave baseline=process(input,settings,true);
    const Wave baselineDry=process(input,dry,true);
    writeWave(folder/"harmonice_32ms_reference.wav",baseline);
    writeWave(folder/"harmonice_32ms_dry.wav",baselineDry);
    std::ofstream report(folder/"latency-study.json");
    if(!report)throw std::runtime_error("latency study report open failed");
    const int baselineDelay=2*static_cast<int>(std::round(input.rate*.016));
    report<<"{\n  \"sourceRate\": "<<input.rate<<",\n  \"sourceFrames\": "
          <<input.left.size()<<",\n  \"productionPdcFrames\": "<<baselineDelay
          <<",\n  \"productionLatencyMs\": "
          <<1000.*baselineDelay/input.rate<<",\n"
          <<"  \"physicalAsioMeasured\": false,\n"
          <<"  \"humanSingerListeningPassed\": false,\n"
          <<"  \"experimentalCandidates\": [\n";
    const double baselineBackingRms=rms(subtract(baseline,baselineDry));
    report<<"  \"baselineBackingRms\": "<<baselineBackingRms<<",\n"
          <<"  \"syntheticFormantsProved\": false,\n";
    bool first=true;
    for(int ms:{24,16}){
        const int frames=2*static_cast<int>(std::round(
            input.rate*static_cast<double>(ms)/2000.));
        const Wave candidate=process(input,settings,true,127,true,true,true,ms);
        const Wave candidateDry=process(input,dry,true,127,true,true,true,ms);
        const int align=baselineDelay-frames;
        const auto aligned=delayedCopy(candidate,align);
        const auto alignedDry=delayedCopy(candidateDry,align);
        const std::string prefix="harmonice_"+std::to_string(ms)+"ms";
        writeWave(folder/(prefix+"_aligned.wav"),aligned);
        writeWave(folder/(prefix+"_dry_aligned.wav"),alignedDry);
        if(!first)report<<",\n";
        first=false;
        const Wave backing=subtract(aligned,alignedDry);
        const double candidateBackingRms=rms(backing);
        const double minimumPitchSyncHz=1.2*input.rate/frames;
        report<<"    {\"targetMs\": "<<ms<<", \"reportedPdcFrames\": "<<frames
              <<", \"actualLatencyMs\": "<<1000.*frames/input.rate
              <<", \"offlineAlignmentFrames\": "<<align
              <<", \"pitchSyncMinimumSourceHz\": "<<minimumPitchSyncHz
              <<", \"pitchSync65HzCausallySupported\": "
              <<(minimumPitchSyncHz<=65.?"true":"false")
              <<", \"alignedMixDifferenceRms\": "
              <<alignedDifferenceRms(baseline,aligned,baselineDelay+256)
              <<", \"alignedDryDifferenceRms\": "
              <<alignedDifferenceRms(baselineDry,alignedDry,baselineDelay+256)
              <<", \"wetBackingRms\": "<<candidateBackingRms
              <<", \"wetBackingRmsRelativeTo32ms\": "
              <<candidateBackingRms/(baselineBackingRms+1.e-12)
              <<", \"stereoSideRms\": "<<sideRms(aligned)
              <<", \"monoFoldRms\": "<<rms(fold(aligned))
              <<", \"maximumAbsSample\": "<<peakAbsolute(aligned)
              <<", \"maximumOneSampleStep\": "<<largestOneSampleStep(aligned)
              <<", \"qualityAccepted\": false}";
    }
    report<<"\n  ],\n  \"note\": \"Synthetic metrics are not perceptual quality; "
             "listen to original adult singer WAVs before selecting a candidate.\"\n}\n";
    if(!report)throw std::runtime_error("latency study report write failed");
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
            // A/B WAVs must be objectively aligned before a singer compares
            // latency candidates; false dry timing differences bias listening.
            const auto dry32=process(v,s,true);
            const int baselineFrames=2*static_cast<int>(std::round(v.rate*.016));
            for(int ms:{24,16}){
                const auto shorter=process(v,s,true,127,true,true,true,ms);
                const int shorterFrames=2*static_cast<int>(std::round(
                    v.rate*static_cast<double>(ms)/2000.));
                const auto aligned=delayedCopy(shorter,baselineFrames-shorterFrames);
                if(alignedDifferenceRms(dry32,aligned,baselineFrames+256)>1.e-7)
                    return 6;
            }
            std::cout<<"Offline native A/B and candidate dry alignment passed"
                         " (synthetic only)\n";
            return 0;
        }
        if(argc==3&&std::string(argv[1])=="--synthetic-corpus"){
            makeSyntheticCorpus(argv[2]);
            std::cout<<"Synthetic-only WAV corpus and aligned A/B diagnostics written; "
                         "no human-singer qualification.\n";
            return 0;
        }
        if(argc==4&&std::string(argv[1])=="--latency-study"){
            renderLatencyStudy(readWave(argv[2]),argv[3]);
            std::cout<<"32/24/16 ms latency A/B WAVs aligned. Human listening NOT verified.\n";
            return 0;
        }
        if(argc!=3){std::cerr<<"usage: harmony_offline_ab input.wav output_directory"
                                     " | harmony_offline_ab --latency-study input.wav output_directory"
                                     " | harmony_offline_ab --synthetic-corpus output_directory\n";return 1;}
        renderAb(readWave(argv[1]),argv[2]);
        std::cout<<"WAV A/B written. No human-singing/formant qualification implied.\n";
        return 0;
    }catch(const std::exception& e){std::cerr<<"Harmony A/B: "<<e.what()<<"\n";return 4;}
}
