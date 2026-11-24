// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <vector>
#include <stdexcept>

namespace armix::harmony::offline {
// OFFLINE research only. Do not include in Engine or realtime callbacks.
// Source/shifted LPC envelope ratio, bounded frequency correction and
// normalized weighted overlap-add. No formant labels or perceptual claim.
class SpectralEnvelope final {
public:
    static constexpr double pi=3.14159265358979323846;
    static constexpr int order=14;
    using Complex=std::complex<double>;
    struct Model {
        std::array<double,order+1> a{};
        bool valid=false;
        double periodicity=0.;
        double logEnvelope(double hz,double rate)const noexcept {
            const Complex z=std::polar(1.,-2.*pi*hz/rate);
            Complex p=a[order];
            for(int k=order-1;k>=0;--k)p=p*z+a[k];
            return -.5*std::log(std::max(1.e-8,std::norm(p)));
        }
    };
    struct Frame {std::size_t center=0;bool accepted=false;double sourcePeriodicity=0.,wetPeriodicity=0.,maxDb=0.;};
    struct Result {std::vector<float> left,right;std::vector<Frame> frames;};
    static Model fit(const std::vector<double>& x,double rate){
        Model m;m.a[0]=1.;
        if(x.size()<128||rate<8000.||rate>16000.)return m;
        double energy=0.;
        for(double v:x){if(!std::isfinite(v))return m;energy+=v*v;}
        if(energy/x.size()<1.e-7)return m;
        // Reject unvoiced/noisy evidence, not a singer F0 estimator.
        const int lo=std::max(4,int(rate/1800.)),hi=std::min(int(rate/70.),int(x.size()/2));
        for(int lag=lo;lag<=hi;++lag){
            double xy=0.,xx=0.,yy=0.;
            for(std::size_t i=lag;i<x.size();i+=2){const double a=x[i],b=x[i-lag];xy+=a*b;xx+=a*a;yy+=b*b;}
            m.periodicity=std::max(m.periodicity,xy/std::sqrt(xx*yy+1.e-20));
        }
        if(m.periodicity<.72)return m;
        std::vector<double> y(x.size());
        for(std::size_t i=0;i<x.size();++i){
            const double v=x[i]-.94*(i?x[i-1]:x[i]);
            y[i]=v*(.54-.46*std::cos(2.*pi*i/(x.size()-1)));
        }
        std::array<double,order+1> r{};
        for(int k=0;k<=order;++k)
            for(std::size_t i=k;i<y.size();++i)r[k]+=y[i]*y[i-k];
        if(r[0]<1.e-9)return m;
        // Diagonal load + reflection coefficient bound keeps denominator
        // stable even on nearly sinusoidal high-F0 sources.
        r[0]*=1.001;double error=r[0];
        for(int k=1;k<=order;++k){
            double acc=r[k];
            for(int j=1;j<k;++j)acc+=m.a[j]*r[k-j];
            const double refl=std::clamp(-acc/std::max(error,r[0]*1.e-8),-.97,.97);
            const auto prev=m.a;
            for(int j=1;j<k;++j)m.a[j]=prev[j]+refl*prev[k-j];
            m.a[k]=refl;error*=1.-refl*refl;
        }
        m.valid=true;return m;
    }
    static Result correct(const std::vector<float>& sourceL,const std::vector<float>& sourceR,
                          const std::vector<float>& wetL,const std::vector<float>& wetR,
                          unsigned rate,int delaySamples,double strength=.8){
        const std::size_t count=wetL.size();
        if(rate<32000||rate>192000||!count||sourceL.size()!=count||sourceR.size()!=count||
           wetR.size()!=count||delaySamples<0||delaySamples>int(rate)||!std::isfinite(strength))
            throw std::invalid_argument("spectral envelope dimensions");
        strength=std::clamp(strength,0.,1.);
        Result result;result.left.resize(count);result.right.resize(count);
        if(strength==0.){result.left=wetL;result.right=wetR;return result;}
        const int decimation=std::max(1,int(std::lround(rate/12000.)));
        const double analysisRate=double(rate)/decimation;
        const auto source=decimate(sourceL,sourceR,decimation);
        const auto wet=decimate(wetL,wetR,decimation);
        std::size_t n=1;while(n<rate*.032)n*=2;
        const auto hop=n/4;
        std::vector<double> window(n),sum(count),left(count),right(count),correction(n/2+1);
        for(std::size_t i=0;i<n;++i)window[i]=.5-.5*std::cos(2.*pi*i/n);
        std::vector<Complex> a(n),b(n);
        const auto half=static_cast<long long>(n/2);
        const int analysisHalf=int(std::lround(analysisRate*.020));
        std::vector<double> frameSource(2*analysisHalf+1),frameWet(frameSource.size());
        auto sample=[](const auto& v,long long at){return at>=0&&at<static_cast<long long>(v.size())?double(v[std::size_t(at)]):0.;};
        for(std::size_t center=0;center<count+n/2;center+=hop){
            const auto sourceAt=std::llround((double(center)-delaySamples)/decimation);
            const auto wetAt=std::llround(double(center)/decimation);
            const bool complete=sourceAt>=analysisHalf&&wetAt>=analysisHalf&&
                sourceAt+analysisHalf<static_cast<long long>(source.size())&&wetAt+analysisHalf<static_cast<long long>(wet.size());
            Frame f{center};
            if(complete){
                for(int j=-analysisHalf;j<=analysisHalf;++j){
                    frameSource[j+analysisHalf]=sample(source,sourceAt+j);
                    frameWet[j+analysisHalf]=sample(wet,wetAt+j);
                }
                const auto x=fit(frameSource,analysisRate),y=fit(frameWet,analysisRate);
                f.sourcePeriodicity=x.periodicity;f.wetPeriodicity=y.periodicity;
                f.accepted=x.valid&&y.valid;
                if(f.accepted){
                    // Remove mean log gain over the useful vocal band.
                    // Correct relative envelope, not phrase loudness.
                    double mean=0.;int bins=0;
                    const double upper=std::min(4700.,.41*analysisRate);
                    for(std::size_t k=0;k<=n/2;++k){
                        const double hz=double(k)*rate/n;
                        correction[k]=0.;
                        if(hz>=300.&&hz<=upper){
                            correction[k]=x.logEnvelope(hz,analysisRate)-y.logEnvelope(hz,analysisRate);
                            mean+=correction[k];++bins;
                        }
                    }
                    mean/=std::max(1,bins);
                    constexpr double cap=6.*std::log(10.)/20.;
                    for(std::size_t k=0;k<=n/2;++k){
                        const double hz=double(k)*rate/n;
                        const double taper=std::clamp((hz-300.)/200.,0.,1.)*
                            std::clamp((upper-hz)/500.,0.,1.);
                        const double logGain=taper*strength*std::clamp(correction[k]-mean,-cap,cap);
                        correction[k]=std::exp(logGain);
                        f.maxDb=std::max(f.maxDb,std::abs(logGain)*20./std::log(10.));
                    }
                }
            }
            if(center<count)result.frames.push_back(f);
            const auto start=static_cast<long long>(center)-half;
            for(std::size_t j=0;j<n;++j){
                a[j]=sample(wetL,start+static_cast<long long>(j))*window[j];
                b[j]=sample(wetR,start+static_cast<long long>(j))*window[j];
            }
            if(f.accepted){
                fft(a,false);fft(b,false);
                for(std::size_t k=0;k<n;++k){const double g=correction[std::min(k,n-k)];a[k]*=g;b[k]*=g;}
                fft(a,true);fft(b,true);
            }
            for(std::size_t j=0;j<n;++j){
                const auto pos=start+static_cast<long long>(j);
                if(pos<0||pos>=static_cast<long long>(count))continue;
                const auto i=std::size_t(pos);
                left[i]+=a[j].real()*window[j];right[i]+=b[j].real()*window[j];
                sum[i]+=window[j]*window[j];
            }
        }
        for(std::size_t i=0;i<count;++i){
            result.left[i]=static_cast<float>(left[i]/std::max(1.e-12,sum[i]));
            result.right[i]=static_cast<float>(right[i]/std::max(1.e-12,sum[i]));
            if(!std::isfinite(result.left[i])||!std::isfinite(result.right[i]))
                throw std::runtime_error("nonfinite spectral envelope candidate");
        }
        return result;
    }
private:
    static std::vector<double> decimate(const std::vector<float>& l,const std::vector<float>& r,int d){
        // Windowed-sinc antialias filter, same analysis response for both
        // streams; symmetric/offline, not a new live delay.
        const int radius=6*d;const double cutoff=.42/d;
        std::vector<double> kernel(2*radius+1);
        double norm=0.;
        for(int j=-radius;j<=radius;++j){
            const double w=.5+.5*std::cos(pi*j/radius);
            const double h=j?std::sin(2.*pi*cutoff*j)/(pi*j):2.*cutoff;
            kernel[j+radius]=w*h;norm+=w*h;
        }
        for(auto& x:kernel)x/=norm;
        std::vector<double> out((l.size()+d-1)/d);
        for(std::size_t i=0;i<out.size();++i){
            double v=0.;const auto center=static_cast<long long>(i)*d;
            for(int j=-radius;j<=radius;++j){
                const auto at=center+j;if(at<0||at>=static_cast<long long>(l.size()))continue;
                v+=.5*(double(l[std::size_t(at)])+r[std::size_t(at)])*kernel[j+radius];
            }
            out[i]=v;
        }
        return out;
    }
    static void fft(std::vector<Complex>& x,bool inverse){
        const auto n=x.size();
        for(std::size_t i=1,j=0;i<n;++i){
            auto bit=n>>1;for(;j&bit;bit>>=1)j^=bit;j^=bit;
            if(i<j)std::swap(x[i],x[j]);
        }
        for(std::size_t len=2;len<=n;len*=2){
            const auto root=std::polar(1.,(inverse?2.:-2.)*pi/len);
            for(std::size_t i=0;i<n;i+=len){
                Complex w=1.;
                for(std::size_t j=0;j<len/2;++j){
                    const auto a=x[i+j],b=x[i+j+len/2]*w;
                    x[i+j]=a+b;x[i+j+len/2]=a-b;w*=root;
                }
            }
        }
        if(inverse)for(auto& v:x)v/=double(n);
    }
};
} // namespace armix::harmony::offline
