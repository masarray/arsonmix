// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/builtin_fx/HarmonyStageSpatial.h"
#include "../engine/builtin_fx/HarmonyRoomFocus.h"
#include "../engine/builtin_fx/HarmonyEnsembleSpace.h"
#include "../engine/builtin_fx/HarmonyDsp.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <vector>
using namespace armix::harmony;
static void need(bool b,const char* reason) {
    if(!b){std::cerr<<reason<<"\n";std::exit(2);}
}
struct SceneEnergy {double direct=0.,early=0.,mono=0.,stereo=0.;};
static SceneEnergy scene(double rate,float space,int singer,int voices){
    // Keep the larger singer reflection history on heap for Windows CTest.
    auto e=std::make_unique<EnsembleSpace>();
    auto stage=std::make_unique<BackingStageSpatial>();
    need(e->prepare(rate)&&stage->prepare(rate),"stage prepare failed");
    need(stage->farEarDelaySamples()>=2,"missing far-ear cue");
    need(e->secondReflectionSamples(singer)>e->reflectionSamples(singer),
         "secondary reflection not after first");
    const float pan=.72f*space*(2.f*singer/(voices-1)-1.f);
    const float angle=(pan+1.f)*3.14159265358979323846f*.25f;
    const float ml=std::cos(angle),mr=std::sin(angle);
    const auto g=BackingStageSpatial::gains(space);
    SceneEnergy x{};
    for(int i=0;i<static_cast<int>(rate*.16);++i){
        const float input=i==0?.7f:0.f;
        const float a=e->exchangeReflection(singer,input);
        const float b=e->secondReflection(singer);
        const auto p=stage->place(singer,input,a,b,ml,mr,space);
        const double dl=p.directL*g.direct,dr=p.directR*g.direct;
        const double rl=p.earlyL*g.early,rr=p.earlyR*g.early;
        x.direct+=dl*dl+dr*dr;x.early+=rl*rl+rr*rr;
        x.mono+=.25*(dl+dr+rl+rr)*(dl+dr+rl+rr);
        x.stereo+=(dl+rl-dr-rr)*(dl+rl-dr-rr);
        need(std::isfinite(dl)&&std::isfinite(dr)&&
             std::isfinite(rl)&&std::isfinite(rr),"nonfinite stage PCM");
        e->advance(true);stage->advance();
    }
    need(x.direct>.001&&x.mono>.0001,"backing collapsed in mono");
    if(space==0.f)need(x.early==0.,"Space 0 leaked reflection");
    else need(x.early>1.e-5,"room reflection inaudible");
    return x;
}
static std::vector<float> render(int block,float space,bool wet){
    auto e=std::make_unique<Engine>();
    need(e->prepare(48000.),"native engine prepare");
    Settings s;s.voices=3;s.wet=wet?.65f:0.f;s.space=space;
    need(e->setSettings(s),"engine settings invalid");
    constexpr int n=24000;
    std::vector<float> l(n),r(n);
    for(int i=0;i<n;++i){
        const double p=6.283185307179586*220.*i/48000.;
        l[i]=static_cast<float>(.18*std::sin(p)+.045*std::sin(2*p));
        r[i]=static_cast<float>(.12*std::sin(p)+.07*std::sin(3*p));
    }
    for(int i=0;i<n;i+=block)e->process(l.data()+i,r.data()+i,std::min(block,n-i));
    for(float value:l)need(std::isfinite(value),"render nonfinite");
    need(e->latencySamples()==1536,"native PDC changed");
    return l;
}

// Separate synthetic frequency-band check for the modest head-shadow cue.
// Compare far/near energy at 350 Hz and 8 kHz while keeping pan and Space
// constant. Not a binaural localization or human HRTF listening test.
static double farNearRatio(double rate,double frequency,float space){
    BackingStageSpatial stage;
    need(stage.prepare(rate),"head shadow rate prepare failed");
    const int frames=static_cast<int>(rate*.09);
    double nearEnergy=0.,farEnergy=0.;
    for(int t=0;t<frames;++t){
        const float input=static_cast<float>(.4*std::sin(
            6.283185307179586*frequency*t/rate));
        const auto p=stage.place(0,input,0.f,0.f,.28f,.96f,space);
        if(t>static_cast<int>(rate*.04)){
            // Compare against direct ear gains to remove normal panning.
            nearEnergy+=(p.directR/.96f)*(p.directR/.96f);
            farEnergy+=(p.directL/.28f)*(p.directL/.28f);
        }
        stage.advance();
    }
    need(nearEnergy>1.e-5&&farEnergy>0.,"head shadow null output");
    return farEnergy/nearEnergy;
}
// Synthetic room clarity and phrase-envelope proof, not real singer listening.
static double focusEnergy(double rate,double hz){
    BackingRoomFocus f;need(f.prepare(rate),"room focus rate");
    const int n=static_cast<int>(rate*.16);double sum=0.;
    for(int i=0;i<n;++i){
        const float x=static_cast<float>(.3*std::sin(
            6.283185307179586*hz*i/rate));
        const auto s=f.filterSend(x,x);
        need(std::isfinite(s.left)&&std::isfinite(s.right),"room finite");
        if(i>n/2)sum+=s.left*s.left;
    }
    return sum/(n/2);
}
static void focusTest(double rate){
    BackingRoomFocus f;need(f.prepare(rate),"room init");
    need(focusEnergy(rate,75.)<focusEnergy(rate,600.)*.5,
         "backing room subbass not cleaned");
    float last=1.f;
    for(int i=0;i<static_cast<int>(rate*.35);++i){
        const float g=f.returnGain(.40f,.40f,1.f);
        need(g>.78f&&g<=1.f&&std::abs(g-last)<.003f,
             "room level jump or overduck");last=g;
    }
    need(last<.85f,"room activity follower inactive");
    need(f.returnGain(.4f,.4f,0.f)==1.f,"Space0 room not neutral");
    for(int i=0;i<static_cast<int>(rate*1.5);++i)
        last=f.returnGain(0.f,0.f,1.f);
    need(last>.995f&&last<=1.f,"room tail did not bloom in gaps");
    need(f.prepare(rate)&&f.leadEnvelope()==0.f,"room restart stale envelope");
    need(f.filterSend(0.f,0.f).left==0.f&&
         f.returnGain(0.f,0.f,1.f)==1.f,"room restart stale sample");
}
int main(){
    for(double rate:{32000.,44100.,48000.,96000.,192000.}){
        focusTest(rate);
        double previous=-1.;
        for(float space:{.25f,.6f,1.f}){
            const auto left=scene(rate,space,0,3);
            const auto relative=left.early/left.direct;
            need(relative>previous,"Space no longer lowers direct/reflected energy ratio");
            previous=relative;
            need(left.stereo>1.e-5,"left image collapsed to mono");
            const auto center=scene(rate,space,1,3);
            const auto right=scene(rate,space,2,3);
            need(center.mono>.0001&&right.mono>.0001,"other voices lost in mono");
        }
        scene(rate,0.f,0,3);
        scene(rate,.8f,4,5);
    }
    // A center harmony voice competes directly with the lead phantom center.
    // Center-only direct recession is modest; its room send remains intact.
    for(double rate:{48000.,96000.,192000.}){
        BackingStageSpatial x;
        need(x.prepare(rate),"center recession prepare failed");
        auto zero=x.place(1,.5f,0.f,0.f,.5f,.5f,0.f);
        x.advance();
        auto deep=x.place(1,.5f,0.f,0.f,.5f,.5f,1.f);
        need(std::abs(zero.directL-.25f)<1.e-6f&&
             std::abs(zero.roomL-.25f)<1.e-6f,
             "Space0 center direct changed");
        need(deep.directL<deep.roomL*.86f&&
             deep.directL>deep.roomL*.80f,
             "center backing direct did not move behind lead");
        need(std::abs(deep.roomL-.25f)<1.e-6f,
             "center stage lost shared-room input");
        const double low=farNearRatio(rate,350.,1.f);
        const double high=farNearRatio(rate,8000.,1.f);
        need(high<low*.85,"far ear missing high-frequency shadow");
        need(high>.08,"far ear over-attenuated or disappeared");
        need(std::abs(farNearRatio(rate,350.,0.f)-1.)<.015,
             "Space zero changed neutral stereo signal");
        need(std::abs(farNearRatio(rate,8000.,0.f)-1.)<.015,
             "Space zero changed high-frequency signal");
    }
    for(float space:{0.f,.6f,1.f})
        need(render(64,space,true)==render(257,space,true),
             "callback partition affected spatial audio");
    need(render(64,0.f,false)==render(257,1.f,false),
         "Space changed true lead at Wet0");
    std::cout<<"Harmony stage depth, reflections, mono, PDC and parity PASS\n";
}
