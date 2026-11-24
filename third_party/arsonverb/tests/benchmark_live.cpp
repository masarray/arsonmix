// Offline portable benchmark. Never called by the production audio callback.
#include <arverb/Reverb.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <memory>
#include <vector>
int main() {
    using Clock=std::chrono::steady_clock;
    using P=arverb::Parameter;
    constexpr int count=600;
    double checksum=0.;
    for(int fs:{48000,96000}) for(int n:{64,128,256}) {
        if(fs==96000&&n!=128)continue;
        for(int mode=0;mode<4;++mode)for(int instances:{1,2}) {
            std::array<std::unique_ptr<arverb::Engine>,2> e;
            for(int j=0;j<instances;++j) {
                e[j]=std::make_unique<arverb::Engine>();
                auto& x=*e[j];
                x.loadPreset(arverb::Preset::VocalPlateClean);
                x.setAlgorithm(static_cast<arverb::Algorithm>(mode));
                x.setParameter(P::Mix,.34f);x.setParameter(P::PreDelayMs,32.f);
                x.setParameter(P::DecaySeconds,2.45f);x.setParameter(P::Diffusion,.94f);
                x.setParameter(P::DampingHz,10400.f);x.setParameter(P::LowCutHz,150.f);
                x.setParameter(P::HighCutHz,15500.f);x.setParameter(P::Width,1.15f);
                x.setParameter(P::EarlyLevel,.36f);x.setParameter(P::ModDepthMs,.20f);
                x.setParameter(P::DuckAmount,.18f);x.prepare(fs,true);
            }
            std::array<float,256> l{},r{},a{},b{};
            std::vector<double> times;times.reserve(count);
            for(int block=-200;block<count;++block) {
                for(int i=0;i<n;++i) {
                    const double t=(block*n+i)/double(fs);
                    l[i]=static_cast<float>(.13*std::sin(1382.3*t)+.07*std::sin(6283.2*t));
                    r[i]=static_cast<float>(.12*std::sin(1382.3*t+.3)+.05*std::sin(3958.4*t));
                }
                const auto start=Clock::now();
                for(int j=0;j<instances;++j)e[j]->process(l.data(),r.data(),a.data(),b.data(),n);
                const auto end=Clock::now();
                checksum+=a[n-1]+b[n-1];
                if(block>=0)times.push_back(std::chrono::duration<double,std::micro>(end-start).count());
            }
            std::sort(times.begin(),times.end());
            std::printf("%d,%d,%d,%d,%.6f,%.6f\n",fs,n,mode,instances,times[count/2],times[count*99/100]);
        }
    }
    return std::isfinite(checksum)?0:1;
}
