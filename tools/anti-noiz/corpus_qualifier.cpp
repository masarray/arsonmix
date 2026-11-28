// SPDX-License-Identifier: AGPL-3.0-only
// Offline qualification only; uses the same native Core, never a second DSP.
#include "OfflineAudio.h"
#include <iomanip>
#include <iostream>
#include <random>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
using namespace armix::builtin_fx::anti_noiz;
using offline::Audio;
using offline::require;
namespace {
struct Segment { std::string label; std::size_t begin, end; bool idle; };
struct Metrics {
    double clean=0, inputError=0, outputError=0, input=0, output=0;
    double projection=0, removedProjection=0, cleanOnlyError=0;
    std::size_t count=0;
};
double db(double a,double b) {
    // Explicit numerical floor; never manufacture an infinite result.
    return 10*std::log10(std::max(a,1e-24)/std::max(b,1e-24));
}
void sameFormat(const Audio& a,const Audio& b) {
    require(a.rate==b.rate&&a.channels==b.channels,"audio format mismatch");
}
void sameTimeline(const Audio& a,const Audio& b) {
    sameFormat(a,b);
    require(a.samples.size()==b.samples.size(),"audio timeline mismatch");
}
Metrics measure(const Audio& clean,const Audio& input,const Audio& output,
                const Audio& cleanOnly,const Segment& s,int channel) {
    sameTimeline(clean,input);sameTimeline(clean,output);sameTimeline(clean,cleanOnly);
    require(s.begin<s.end&&s.end<=clean.samples.size()/clean.channels,"segment out of bounds");
    require(channel==-1||(channel>=0&&unsigned(channel)<clean.channels),"invalid channel");
    Metrics m;
    for(std::size_t f=s.begin;f<s.end;++f) {
        for(unsigned c=0;c<clean.channels;++c) {
            if(channel>=0&&c!=unsigned(channel))continue;
            const auto i=f*clean.channels+c;
            const double x=clean.samples[i],u=input.samples[i],y=output.samples[i],z=cleanOnly.samples[i];
            require(std::isfinite(x)&&std::isfinite(u)&&std::isfinite(y)&&std::isfinite(z),"nonfinite metric input");
            m.clean+=x*x;m.input+=u*u;m.output+=y*y;
            m.inputError+=(u-x)*(u-x);m.outputError+=(y-x)*(y-x);
            m.projection+=x*y;m.removedProjection+=x*(u-y);
            m.cleanOnlyError+=(z-x)*(z-x);++m.count;
        }
    }
    return m;
}
std::vector<Segment> segments(const std::filesystem::path& path,std::size_t frames) {
    std::ifstream in(path);require(bool(in),"cannot read segment labels");
    std::vector<Segment> result;std::set<std::string> names;std::string line;
    while(std::getline(in,line)) {
        if(line.empty()||line[0]=='#')continue;
        std::istringstream row(line);Segment s{};std::string kind,extra;
        require(bool(row>>s.label>>s.begin>>s.end>>kind)&&!(row>>extra),"expected label begin end active|idle");
        require(s.label.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")==std::string::npos,"invalid label");
        require(s.begin<s.end&&s.end<=frames&&names.insert(s.label).second,"invalid or duplicate segment");
        require(kind=="active"||kind=="idle","invalid segment kind");s.idle=kind=="idle";
        result.push_back(s);
    }
    require(in.eof()&&!result.empty(),"missing or invalid segments");return result;
}
void provenance(const std::filesystem::path& path) {
    // Human-reviewed local manifest, not an automated legal/license verifier.
    std::ifstream in(path);require(bool(in),"cannot read provenance");
    std::set<std::string> roles;std::string line;
    while(std::getline(in,line)) {
        if(line.empty()||line[0]=='#')continue;
        std::istringstream row(line);std::string role,license,origin,extra;
        require(bool(std::getline(row,role,'\t')&&std::getline(row,license,'\t')&&std::getline(row,origin,'\t'))&&
                !license.empty()&&!origin.empty()&&!(row>>extra),"expected role<TAB>license<TAB>source/credit");
        require((role=="clean"||role=="noise"||role=="learn")&&roles.insert(role).second,"invalid or duplicate provenance role");
    }
    require(in.eof()&&roles.size()==3,"provenance must cover clean, noise and learn");
}
float number(const char* text) {
    std::size_t used=0;const std::string s(text);const float value=std::stof(s,&used);
    require(used==s.size()&&std::isfinite(value),"invalid numeric parameter");return value;
}
void policy(std::string_view name,Core::Settings& s,Core::SourceRole& role) {
    if(name=="voice")s.mode=Core::Mode::Voice;
    else if(name=="singing")s.mode=Core::Mode::Singing;
    else if(name=="music")s.mode=Core::Mode::Music;
    else if(name=="auto-mic")role=Core::SourceRole::SpeechMic;
    else if(name=="auto-singing")role=Core::SourceRole::SingingMic;
    else if(name=="auto-music")role=Core::SourceRole::Music;
    else if(name=="auto-mixed")role=Core::SourceRole::Mixed;
    else require(name=="auto","unknown mode");
}
Audio mix(const Audio& clean,const Audio& noise) {
    sameTimeline(clean,noise);Audio input=clean;
    for(std::size_t i=0;i<input.samples.size();++i) {
        input.samples[i]+=noise.samples[i];
        require(std::isfinite(input.samples[i])&&std::abs(input.samples[i])<=1.f,"mixture clips: scale both stems before evaluation");
    }
    return input;
}
constexpr std::string_view csvHeader="label,kind,channel_minus1_all,begin_frame,end_frame,error_improvement_db,desired_projection,removed_projection,clean_only_error_db_relative,idle_attenuation_db";
void report(std::ostream& out,const Metrics& m,const Segment& s,int channel) {
    out<<s.label<<','<<(s.idle?"idle":"active")<<','<<channel<<','<<s.begin<<','<<s.end<<',';
    // For idle labels require actual near-silence of the clean reference.
    if(s.idle) {
        require(m.clean<=1e-10*double(m.count),"idle label contains desired signal");
        out<<",,,,"<<db(m.input,m.output)<<'\n';
    } else {
        require(m.clean>1e-12*double(m.count),"active label has no measurable reference");
        out<<db(m.inputError,m.outputError)<<','<<m.projection/m.clean<<','
                 <<m.removedProjection/m.clean<<','<<db(m.cleanOnlyError,m.clean)<<",\n";
    }
}
// Offline diagnosis reproduces the current Learn WOLA analysis, including
// two discarded warm-up frames, but continues measuring after the first
// rejected frame. It never alters the production NoiseProfile.
void learnDiagnostics(const Audio& a) {
    const int n=a.rate>48000?512:256,hop=n/2,bins=hop+1;
    const int target=int(std::ceil(2.*a.rate/hop));
    require(a.samples.size()/a.channels>=std::size_t(target+2)*hop,"diagnostic Learn too short");
    FftPlan fft;fft.prepare(n);
    std::array<float,FftPlan::maxN> window{};
    for(int i=0;i<n;++i)window[i]=std::sqrt(.5f-.5f*std::cos(2.f*std::numbers::pi_v<float>*float(i)/float(n)));
    std::array<std::array<float,FftPlan::maxN/2+1>,2> power{};
    std::array<FftPlan::Complex,FftPlan::maxN> work{};
    std::array<float,2> running{},windowEnergy{},runningWindow{};
    std::array<int,2> high{},low{},windowHigh{},windowLow{};
    std::array<float,2> minRatio{1.f,1.f},maxRatio{1.f,1.f},minWindow{1.f,1.f},maxWindow{1.f,1.f};
    int first=-1,firstChannel=-1,firstDirection=0,windows=0;
    float firstRatio=0;
    const int windowFrames=int(std::ceil(.05*a.rate/hop));
    for(int frame=0;frame<target;++frame) {
        const auto begin=std::size_t(frame+1)*hop;
        for(unsigned c=0;c<a.channels;++c) {
            for(int i=0;i<n;++i)work[i]={a.samples[(begin+std::size_t(i))*a.channels+c]*window[i],0};
            fft.transform(work,false);
            float total=0;
            for(int k=0;k<bins;++k){power[c][k]=std::norm(work[k]);total+=power[c][k];}
            if(frame>=8){
                const float mean=running[c]/float(frame);
                const float ratio=total/std::max(mean,1e-12f);
                minRatio[c]=std::min(minRatio[c],ratio);maxRatio[c]=std::max(maxRatio[c],ratio);
                const bool hi=total>4.f*std::max(mean,1e-12f),lo=total<.1f*mean;
                high[c]+=hi;low[c]+=lo;
                if(first<0&&(hi||lo)){first=frame;firstChannel=int(c);firstDirection=hi?1:-1;firstRatio=ratio;}
            }
            running[c]+=total;windowEnergy[c]+=total;
            if((frame+1)%windowFrames==0){
                if(windows>0){
                    const float ratio=windowEnergy[c]/std::max(runningWindow[c]/float(windows),1e-12f);
                    minWindow[c]=std::min(minWindow[c],ratio);maxWindow[c]=std::max(maxWindow[c],ratio);
                    windowHigh[c]+=ratio>4.f;windowLow[c]+=ratio<.1f;
                }
                runningWindow[c]+=windowEnergy[c];windowEnergy[c]=0;
            }
        }
        if((frame+1)%windowFrames==0)++windows;
    }
    std::cout<<std::setprecision(9)<<"{\"frames\":"<<target<<",\"window_frames\":"<<windowFrames
             <<",\"first_frame\":"<<first<<",\"first_channel\":"<<firstChannel
             <<",\"first_direction\":"<<firstDirection<<",\"first_ratio\":"<<firstRatio<<",\"channels\":[";
    for(unsigned c=0;c<a.channels;++c) {
        if(c)std::cout<<',';
        std::cout<<"{\"high_frames\":"<<high[c]<<",\"low_frames\":"<<low[c]
                 <<",\"min_ratio\":"<<minRatio[c]<<",\"max_ratio\":"<<maxRatio[c]
                 <<",\"high_windows\":"<<windowHigh[c]<<",\"low_windows\":"<<windowLow[c]
                 <<",\"min_window_ratio\":"<<minWindow[c]<<",\"max_window_ratio\":"<<maxWindow[c]<<'}';
    }
    std::cout<<"]}\n";
}
void selfTest() {
    Audio c{48000,1,{.25f,-.25f,.25f,-.25f}},n{48000,1,{.125f,.125f,-.125f,-.125f}};
    const auto x=mix(c,n);Audio y=c;
    for(std::size_t i=0;i<y.samples.size();++i)y.samples[i]+=.5f*n.samples[i];
    const auto m=measure(c,x,y,c,{"analytic",0,4,false},-1);
    require(std::abs(db(m.inputError,m.outputError)-6.020599913)<1e-8&&
            m.projection/m.clean==1&&m.removedProjection==0&&m.cleanOnlyError==0,"analytic metric failure");
    // Wire-format regression: exact column names and ten fields in both row types.
    require(csvHeader=="label,kind,channel_minus1_all,begin_frame,end_frame,error_improvement_db,desired_projection,removed_projection,clean_only_error_db_relative,idle_attenuation_db","CSV schema changed");
    std::ostringstream csv;report(csv,m,{"analytic",0,4,false},-1);
    const auto activeRow=csv.str();
    require(std::count(activeRow.begin(),activeRow.end(),',')==9&&activeRow.starts_with("analytic,active,-1,0,4,")&&activeRow.ends_with(",\n"),"active CSV row malformed");
    Metrics idle{};idle.count=4;idle.input=4;idle.output=1;
    std::ostringstream idleCsv;report(idleCsv,idle,{"pause",0,4,true},-1);
    const auto idleRow=idleCsv.str();
    require(std::count(idleRow.begin(),idleRow.end(),',')==9&&idleRow.starts_with("pause,idle,-1,0,4,,,,,"),"idle CSV row malformed");
    Audio c2{48000,2,{.25f,0.f,-.25f,0.f}},zero=c2;zero.samples.assign(4,0);
    const auto a=measure(c2,c2,zero,c2,{"left",0,2,false},0);
    require(a.clean==.125&&a.projection==0&&a.removedProjection==.125,"channel or signal-loss metric failure");
    bool rejected=false;try{(void)measure(c,x,y,c,{"bad",0,5,false},-1);}catch(const std::exception&){rejected=true;}
    require(rejected,"invalid segment accepted");
    Audio signal{48000,1,std::vector<float>(48000)},noise=signal;
    Audio learn{48000,1,std::vector<float>(144000)};
    std::mt19937 rng(1739);std::uniform_real_distribution<float> random(-.015f,.015f);
    for(auto& v:learn.samples)v=random(rng);
    for(std::size_t i=0;i<signal.samples.size();++i) {
        signal.samples[i]=.08f*std::sin(2.f*std::numbers::pi_v<float>*750.f*float(i)/48000.f);
        noise.samples[i]=random(rng);
    }
    const auto input=mix(signal,noise);Core::Settings s;s.silenceDb=0;s.cleanDb=0;
    const auto render=offline::render(input,&learn,s);
    const auto cleanOnly=offline::render(signal,&learn,s);
    const auto unity=measure(signal,input,render.processed,cleanOnly.processed,{"unity",0,48000,false},-1);
    require(std::abs(db(unity.inputError,unity.outputError))<.001&&unity.cleanOnlyError<1e-8,"aligned unity metric regression");
    s.cleanDb=14;s.mode=Core::Mode::Voice;
    const auto active=offline::render(input,&learn,s);
    const auto activeClean=offline::render(signal,&learn,s);
    const auto v=measure(signal,input,active.processed,activeClean.processed,{"voice",12000,48000,false},-1);
    require(std::isfinite(db(v.inputError,v.outputError))&&v.count==36000,"native qualification path failed");
    std::cout<<"Synthetic qualification self-test PASS; active clean-reference error improvement "<<db(v.inputError,v.outputError)<<" dB (not real corpus)\n";
}
}
int main(int argc,char** argv) {
    try {
        if(argc==2&&std::string_view(argv[1])=="--self-test"){selfTest();return 0;}
        if(argc==3&&std::string_view(argv[1])=="--learn-diagnostics"){learnDiagnostics(offline::read(argv[2]));return 0;}
        if(argc!=10&&argc!=11&&argc!=14&&argc!=16){std::cerr<<"Usage: anti_noiz_qualify clean.wav noise.wav learn.wav segments.tsv provenance.tsv output-prefix mode cleanDb musicProtect(0..1) [silenceDb [noiseOffsetDb artifactControl vocalProtection | thresholdDb hpHz marginDb maxLiftDb smart(0|1)]]\n";return 2;}
        provenance(argv[5]);const auto clean=offline::read(argv[1]),noise=offline::read(argv[2]),learn=offline::read(argv[3]);
        sameFormat(clean,learn);
        require(!std::filesystem::equivalent(argv[2],argv[3])&&noise.samples!=learn.samples,"Learn and test noise must be independent recordings/segments");
        const auto input=mix(clean,noise);const auto labels=segments(argv[4],clean.samples.size()/clean.channels);
        Core::Settings s;s.silenceDb=argc>=11?number(argv[10]):0;s.cleanDb=number(argv[8]);s.musicProtect=number(argv[9]);
        if(argc==14){s.spectralManual=true;s.noiseOffsetDb=number(argv[11]);s.artifactControl=number(argv[12]);s.vocalProtection=number(argv[13]);}
        if(argc==16){
            const float smart=number(argv[15]);require(smart==0||smart==1,"smart flag");
            s.manual.enabled=true;s.manual.gate=true;s.manual.rangeDb=96;s.manual.thresholdDb=number(argv[11]);s.manual.hpHz=number(argv[12]);s.manual.smartMarginDb=number(argv[13]);s.manual.smartMaxLiftDb=number(argv[14]);s.manual.smartVoice=smart==1;
        }
        Core::SourceRole role=Core::SourceRole::Unknown;policy(argv[7],s,role);
        const auto transparent=Core::resolveMode(s.mode,role)==Core::Mode::Music;
        const auto prefix=std::string(argv[6]);
        const std::array<std::string,4> paths{prefix+"-input.wav",prefix+"-processed.wav",prefix+"-delta.wav",prefix+"-clean-only.wav"};
        for(const auto& p:paths)require(!std::filesystem::exists(p),"output already exists");
        const auto result=offline::render(input,transparent?nullptr:&learn,s,128,role);
        const auto cleanOnly=offline::render(clean,transparent?nullptr:&learn,s,128,role);
        std::cout<<std::setprecision(12)<<csvHeader<<'\n';
        for(const auto& label:labels) {
            report(std::cout,measure(clean,input,result.processed,cleanOnly.processed,label,-1),label,-1);
            for(unsigned c=0;c<clean.channels;++c) {
                const auto m=measure(clean,input,result.processed,cleanOnly.processed,label,int(c));
                // An active stereo label may contain an intentionally silent channel.
                if(!label.idle&&m.clean<=1e-12*double(m.count))continue;
                report(std::cout,m,label,int(c));
            }
        }
        offline::write(paths[0],input);offline::write(paths[1],result.processed);
        offline::write(paths[2],result.delta);offline::write(paths[3],cleanOnly.processed);
        if(argc==16)std::cerr<<std::setprecision(12)<<"SMART_GATE "<<result.sidechainReference<<' '<<result.sidechainNoisePeak<<' '<<result.sidechainThreshold<<'\n';
        std::cerr<<"Silence="<<s.silenceDb<<"; latency aligned "<<result.latency<<" samples; report is objective error, not perceptual or isolated noise attenuation in active segments.\n";
    } catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
    return 0;
}
