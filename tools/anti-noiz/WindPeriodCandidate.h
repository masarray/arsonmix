// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numbers>

namespace armix::anti_noiz::research {
// Experimental streaming period-mean subtractor. Not connected to production.
// Fixed memory, no allocation, same 256/512 sample delay as Anti Noiz Core.
class WindPeriodCandidate final {
    static constexpr int ring=2048, mask=ring-1, history=240, highRing=512;
    struct Biquad {
        double b0=1,b1=0,b2=0,a1=0,a2=0,z1=0,z2=0;
        void prepare(double rate,double hz,bool high) noexcept {
            const double w=2*std::numbers::pi*hz/rate,c=std::cos(w),a=std::sin(w)/std::sqrt(2.0),inv=1/(1+a);
            b0=(high?(1+c):(1-c))*.5*inv;b1=(high?-2:2)*b0;b2=b0;
            a1=-2*c*inv;a2=(1-a)*inv;reset();
        }
        void reset() noexcept {z1=z2=0;}
        double process(double x) noexcept {const double y=b0*x+z1;z1=b1*x-a1*y+z2;z2=b2*x-a2*y;return y;}
    };
    struct Channel {
        std::array<float,ring> pcm{};
        std::array<double,ring+1> prefix{};
        std::array<double,highRing> high{};
        Biquad hp,lp;
        double previousTotal=0,period=480,previousPeriod=480;
        std::int64_t highCount=0;
        void reset(double initial) noexcept {
            pcm.fill(0);prefix.fill(0);high.fill(0);hp.reset();lp.reset();
            previousTotal=0;highCount=0;period=previousPeriod=initial;
        }
        void push(float x,std::int64_t t,int ds) noexcept {
            const int i=int(t&mask);pcm[i]=x;prefix[i+1]=prefix[i]+x;
            if(i==mask){previousTotal=prefix[ring];prefix[0]=0;}
            const double y=lp.process(hp.process(x));
            if(t%ds==0){high[highCount&(highRing-1)]=y;++highCount;}
        }
        double boundary(std::int64_t i,std::int64_t written) const noexcept {
            if(i<0)return 0;
            const double v=prefix[i&mask];
            return i/ring==written/ring?v:v-previousTotal;
        }
        double integral(double pos,std::int64_t written) const noexcept {
            if(pos<=0)return boundary(0,written);
            const auto i=std::int64_t(pos);const double f=pos-double(i),a=boundary(i,written);
            return f==0?a:a+f*(boundary(i+1,written)-a);
        }
        double pitch(std::int64_t aligned,int ds,double rate) const noexcept {
            // History ends strictly before the aligned sample, even though
            // the output has the existing lookahead budget available.
            const auto end=(aligned+ds-1)/ds;
            std::array<double,history> h{};double sum=0;
            for(int i=0;i<history;++i){const auto j=end-history+i;h[i]=j>=0?high[j&(highRing-1)]:0;sum+=h[i];}
            const double mean=sum/history;for(auto& v:h)v-=mean;
            std::array<double,history+1> energy{};
            for(int i=0;i<history;++i)energy[i+1]=energy[i]+h[i]*h[i];
            if(energy.back()<1e-18)return rate/100;
            const int first=std::max(2,int(rate/(ds*500.))),last=std::min(100,int(rate/(ds*100.)));
            std::array<double,101> corr{};
            for(int k=first;k<=last;++k){double dot=0;for(int i=k;i<history;++i)dot+=h[i]*h[i-k];
                corr[k]=dot/std::sqrt(std::max((energy.back()-energy[k])*energy[history-k],1e-40));}
            double best=0;
            for(int k=first+1;k<last;++k)if(corr[k]>=corr[k-1]&&corr[k]>=corr[k+1])best=std::max(best,corr[k]);
            if(best<=.65)return rate/100;
            for(int k=last-1;k>first;--k)if(corr[k]>=std::max(.65,.98*best)&&corr[k]>=corr[k-1]&&corr[k]>=corr[k+1]){
                const double offset=.5*(corr[k-1]-corr[k+1])/std::min(corr[k-1]-2*corr[k]+corr[k+1],-1e-12);
                return std::clamp(ds*(k+std::clamp(offset,-.5,.5)),rate/500,rate/100);
            }
            return rate/100;
        }
        float output(std::int64_t a,std::int64_t written,int hop,int ds,double rate,float amount) noexcept {
            if(a<0)return 0;
            if(a%hop==0){previousPeriod=period;period=pitch(a,ds,rate);}
            const double width=previousPeriod+(period-previousPeriod)*double(a%hop)/hop;
            const double center=double(a)+.5;
            const double m=(integral(center+width*.5,written)-integral(center-width*.5,written))/width;
            return float(pcm[a&mask]-amount*m);
        }
    };
public:
    bool prepare(double rate,int channels) noexcept {
        ready_=false;
        if(!std::isfinite(rate)||rate<44100||rate>96000||channels<1||channels>2)return false;
        rate_=rate;channels_=channels;delay_=rate>48000?512:256;hop_=delay_/2;ds_=int(std::lround(rate/6000));
        for(auto& c:channel_){c.hp.prepare(rate,350,true);c.lp.prepare(rate,3000,false);}
        reset();ready_=true;return true;
    }
    void reset() noexcept {t_=0;for(auto& c:channel_)c.reset(rate_/100);}
    int latencySamples() const noexcept {return ready_?delay_:0;}
    bool setAmount(float amount) noexcept {
        if(!std::isfinite(amount)||amount<0||amount>1)return false;
        amount_=amount;return true;
    }
    void process(float* left,float* right,int count) noexcept {
        if(!ready_||!left||count<=0||(channels_==2&&(!right||left==right)))return;
        float* const p[2]={left,right};
        for(int i=0;i<count;++i,++t_){for(int c=0;c<channels_;++c){
            const float x=std::isfinite(p[c][i])?std::clamp(p[c][i],-64.f,64.f):0;
            channel_[c].push(x,t_,ds_);
            p[c][i]=channel_[c].output(t_-delay_,t_+1,hop_,ds_,rate_,amount_);
        }}
    }
private:
    std::array<Channel,2> channel_{};
    double rate_=48000;std::int64_t t_=0;
    int channels_=0,delay_=256,hop_=128,ds_=8;
    float amount_=1;bool ready_=false;
};
}
