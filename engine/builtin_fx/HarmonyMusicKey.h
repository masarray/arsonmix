// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include "HarmonyStereoFold.h"

// Audio-thread-only, bounded chroma guide from a DIFFERENT ChannelId's real
// pre-fader PCM. Not a chord recognizer and never an audible sidechain.
// Conservative confidence/hold; uncertain music falls back to Manual Key.
// No FFT allocation, locks, async worker, trig or heap in process().
namespace armix::harmony {
// Do not indefinitely pin an obsolete key when a new candidate is stable but
// moderately confident. Strong evidence can retarget after 0.8s; weaker yet
// valid evidence needs 2.0s of consecutive agreement. A candidate that
// oscillates resets its hold in evaluate(), so this never forces ambiguity.
constexpr bool acceptMusicKeyCandidate(bool currentValid,bool sameKey,
                                       float confidence,int stableWindows) noexcept {
    if(stableWindows<8)return false;
    return !currentValid||sameKey||confidence>0.10f||stableWindows>=20;
}
struct MusicKeyEstimate final {
    int key=-1,scale=-1; // -1 means no reliable tonal evidence
    float confidence=0.f;
    bool valid() const noexcept {return key>=0&&key<12&&scale>=0&&scale<2;}
};
class MusicKeyDetector final {
public:
    static constexpr int bins=36; // C3..B5, 3 octaves of music harmonics
    // A 100ms chroma boundary must never score 24 tonal candidates in ONE
    // ASIO sample. Spread two scores per decimated sample (<2.1ms total).
    static constexpr int maxKeyScoresPerDecimatedSample=2;
    static constexpr int maxRotorNormalizationsPerDecimatedSample=3;
    int peakKeyScoresPerSample() const noexcept {return peakKeyScores_;}
    int peakRotorNormalizationsPerSample() const noexcept {return peakNormalizations_;}
    bool prepare(double rate) noexcept {
        if(!std::isfinite(rate)||rate<32000.||rate>192000.)return false;
        decimation_=std::max(1,static_cast<int>(std::round(rate/6000.)));
        if(!stereoFold_.prepare(rate))return false;
        const double fs=rate/decimation_;
        sampleAlpha_=static_cast<float>(1.-std::exp(-1./(fs*.18)));
        lowpassAlpha_=static_cast<float>(1.-std::exp(-2.*3.141592653589793*1500./rate));
        period_=std::max(128,static_cast<int>(std::round(fs*.10)));
        for(int i=0;i<bins;++i) {
            const double hz=130.81278265*std::pow(2.,static_cast<double>(i)/12.);
            const double step=2.*3.141592653589793*hz/fs;
            rotorC_[i]=static_cast<float>(std::cos(step));
            rotorS_[i]=static_cast<float>(std::sin(step));
            weight_[i]=static_cast<float>(std::sqrt(261.625565/hz));
        }
        reset();return true;
    }
    void reset() noexcept {
        stereoFold_.reset();
        inC_.fill(0.f);inS_.fill(0.f);
        oscC_.fill(1.f);oscS_.fill(0.f);hist_.fill(0.f);
        downCount_=0;periodCount_=0;normalizeCount_=0;normalizeIndex_=bins;
        scoringActive_=false;scoringIndex_=0;scoringBest_=-2.f;
        scoringSecond_=-2.f;scoringRoot_=-1;scoringScale_=-1;
        scoringHist_.fill(0.f);peakKeyScores_=0;peakNormalizations_=0;
        framesSeen_=0;candidate_=-1;candidateScale_=-1;candidateHold_=0;
        silentFrames_=0;uncertainFrames_=0;windowEnergy_=0.;filtered_=0.f;
        current_={};
    }
    void process(const float* left,const float* right,int count) noexcept {
        if(!left||!right||count<=0||decimation_<=0)return;
        for(int n=0;n<count;++n) {
            const float l=std::isfinite(left[n])?left[n]:0.f;
            const float r=std::isfinite(right[n])?right[n]:0.f;
            filtered_+=lowpassAlpha_*(stereoFold_.process(l,r)-filtered_);
            if(++downCount_<decimation_)continue;
            downCount_=0;
            const float x=filtered_;
            windowEnergy_+=static_cast<double>(x)*x;
            for(int k=0;k<bins;++k){
                // Leaky complex resonator: a small constant workload on each
                // decimated sample, not a large Goertzel burst at a callback.
                inC_[k]+=(x*oscC_[k]-inC_[k])*sampleAlpha_;
                inS_[k]+=(x*oscS_[k]-inS_[k])*sampleAlpha_;
                const float c=oscC_[k],s=oscS_[k];
                oscC_[k]=c*rotorC_[k]-s*rotorS_[k];
                oscS_[k]=s*rotorC_[k]+c*rotorS_[k];
            }
            // Rotor housekeeping is also partitioned. Normalizing all 36
            // oscillators in the same ASIO sample caused an avoidable spike.
            if(++normalizeCount_>=4096){
                normalizeCount_=0;normalizeIndex_=0;
            }
            int normalized=0;
            while(normalizeIndex_<bins &&
                  normalized<maxRotorNormalizationsPerDecimatedSample){
                const int k=normalizeIndex_++;
                const float size=std::sqrt(oscC_[k]*oscC_[k]+oscS_[k]*oscS_[k]);
                if(size>0.5f&&std::isfinite(size)){
                    oscC_[k]/=size;oscS_[k]/=size;
                }else {oscC_[k]=1.f;oscS_[k]=0.f;}
                ++normalized;
            }
            peakNormalizations_=std::max(peakNormalizations_,normalized);
            if(++periodCount_>=period_){
                periodCount_=0;
                evaluate(static_cast<float>(std::sqrt(windowEnergy_/period_)));
                windowEnergy_=0.;
            }
            // Score order matches the historical major-then-minor, root 0..11
            // loop. The scoring input is a frozen chroma snapshot, so the
            // answer is independent of host callback partition boundaries.
            continueScoring();
        }
    }
    MusicKeyEstimate estimate() const noexcept{return current_;}
private:
    static constexpr std::array<float,12> majorProfile{{
      6.35f,2.23f,3.48f,2.33f,4.38f,4.09f,2.52f,5.19f,2.39f,3.66f,2.29f,2.88f}};
    static constexpr std::array<float,12> minorProfile{{
      6.33f,2.68f,3.52f,5.38f,2.60f,3.53f,2.54f,4.75f,3.98f,2.69f,3.34f,3.17f}};
    static float correlation(const std::array<float,12>& chroma,
                             const std::array<float,12>& profile,int root) noexcept {
        float mc=0.f,mp=0.f;
        for(int i=0;i<12;++i){
            mc+=chroma[i];mp+=profile[i];
        }
        mc/=12.f;mp/=12.f;
        float dot=0.f,cc=0.f,pp=0.f;
        for(int i=0;i<12;++i){
            const float a=chroma[i]-mc,b=profile[(i-root+12)%12]-mp;
            dot+=a*b;cc+=a*a;pp+=b*b;
        }
        return dot/std::sqrt(cc*pp+1.e-12f);
    }
    void evaluate(float rms) noexcept {
        scoringActive_=false;
        if(!std::isfinite(rms)||rms<0.002f) {
            if(++silentFrames_>=10) {
                current_={};hist_.fill(0.f);
                framesSeen_=0;candidate_=-1;candidateScale_=-1;candidateHold_=0;
                uncertainFrames_=0;
            }
            return;
        }
        silentFrames_=0;
        std::array<float,12> chroma{};
        float sum=0.f;
        for(int i=0;i<bins;++i){
            const float magnitude=std::sqrt(inC_[i]*inC_[i]+inS_[i]*inS_[i])*weight_[i];
            if(!std::isfinite(magnitude))return;
            chroma[i%12]+=magnitude;
            sum+=magnitude;
        }
        if(sum<0.0005f)return;
        for(int k=0;k<12;++k)
            hist_[k]=hist_[k]*0.91f+0.09f*(chroma[k]/sum);
        if(framesSeen_<1000)++framesSeen_;
        if(framesSeen_<12)return; // at least ~1.2 seconds of music
        scoringHist_=hist_; // fixed-size snapshot; zero allocation
        scoringIndex_=0;scoringBest_=-2.f;scoringSecond_=-2.f;
        scoringRoot_=-1;scoringScale_=-1;scoringActive_=true;
    }
    void continueScoring() noexcept {
        if(!scoringActive_)return;
        int scored=0;
        while(scoringIndex_<24 && scored<maxKeyScoresPerDecimatedSample){
            const int scale=scoringIndex_/12,root=scoringIndex_%12;
            const float score=correlation(scoringHist_,
                                     scale?minorProfile:majorProfile,root);
            if(score>scoringBest_){
                scoringSecond_=scoringBest_;scoringBest_=score;
                scoringRoot_=root;scoringScale_=scale;
            } else scoringSecond_=std::max(scoringSecond_,score);
            ++scoringIndex_;++scored;
        }
        peakKeyScores_=std::max(peakKeyScores_,scored);
        if(scoringIndex_<24)return;
        scoringActive_=false;
        const float best=scoringBest_;
        const int bestRoot=scoringRoot_,bestScale=scoringScale_;
        // This is KEY estimation, not chord detection. A complete, frozen
        // evaluation commits atomically only after all 24 candidate scores.
        const float confidence=std::clamp(best-scoringSecond_,0.f,1.f);
        if(best<0.48f||confidence<0.045f) {
            candidate_=-1;candidateScale_=-1;candidateHold_=0;
            // A new ambiguous musical section cannot retain the previous
            // accepted key forever. After ~1.5 s fall back to Manual Key.
            if(++uncertainFrames_>=15)current_={};
            return;
        }
        uncertainFrames_=0;
        if(bestRoot!=candidate_||bestScale!=candidateScale_){
            candidate_=bestRoot;candidateScale_=bestScale;candidateHold_=1;
        }else if(candidateHold_<20)++candidateHold_;
        if(acceptMusicKeyCandidate(current_.valid(),
            current_.key==bestRoot&&current_.scale==bestScale,
            confidence,candidateHold_))
            current_={bestRoot,bestScale,confidence};
    }
    int decimation_=0,downCount_=0,period_=0,periodCount_=0,normalizeCount_=0;
    int normalizeIndex_=bins,scoringIndex_=0;
    int scoringRoot_=-1,scoringScale_=-1,peakKeyScores_=0,peakNormalizations_=0;
    float scoringBest_=-2.f,scoringSecond_=-2.f;
    bool scoringActive_=false;
    std::array<float,12> scoringHist_{};
    int framesSeen_=0,silentFrames_=0,uncertainFrames_=0;
    int candidate_=-1,candidateScale_=-1,candidateHold_=0;
    float sampleAlpha_=0.f,lowpassAlpha_=0.f,filtered_=0.f;
    double windowEnergy_=0.;
    MusicKeyEstimate current_{};
    StereoPhaseSafeFold stereoFold_{};
    std::array<float,bins> rotorC_{},rotorS_{},weight_{};
    std::array<float,bins> oscC_{},oscS_{},inC_{},inS_{};
    std::array<float,12> hist_{};
};
} // namespace armix::harmony
