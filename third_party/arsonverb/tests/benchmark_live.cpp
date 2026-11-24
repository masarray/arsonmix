// Offline portable benchmark. Never called by the production audio callback.
#include <arverb/Reverb.hpp>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>
// Cross-build audio fingerprint. Runs only offline; not timed. Same
// compiler/flags and input stream, both profiles and all algorithms.
// Include sub-sample/zero/max delay, repeated morph retargeting, reset,
// modulation, parameter automation, and irregular callback sizes.
static int pcmFingerprint(unsigned profile=0) {
    using P=arverb::Parameter;
    constexpr std::array<int,7> blocks{{1,17,32,64,127,256,255}};
    constexpr std::array<float,6> delays{{0.f,.008f,.021f,27.f,110.f,250.f}};
    constexpr std::array<float,5> sizes{{.65f,2.f,1.08f,.82f,1.65f}};
    for(bool musical:{false,true})for(int fs:{8000,44100,48000,96000,192000})
    for(int mode=0;mode<4;++mode) {
        if(profile==2&&!musical)continue;
        arverb::Engine e;
        e.loadPreset(arverb::Preset::VocalPlateClean);
        e.setAlgorithm(static_cast<arverb::Algorithm>(mode));
        e.setParameter(P::Mix,.34f);
        #ifdef ARVERB_ALBUM_BENCHMARK
        if(profile==1)e.setAlbumReady(true);
#endif
#ifdef ARVERB_SIGNATURE_BENCHMARK
        if(profile==2)e.setVoicing(2);
#endif
        e.prepare(fs,musical);
        std::array<float,256> l{},r{},a{},b{};
        std::uint64_t hash=14695981039346656037ull;
        int pos=0;
        for(int block=0;block<1600;++block) {
            if(block%37==0) {
                const int k=block/37;
                e.setParameter(P::PreDelayMs,delays[k%delays.size()]);
                e.setParameter(P::Size,sizes[k%sizes.size()]);
                e.setParameter(P::ModDepthMs,(k%4)*.30f);
                e.setParameter(P::Mix,(k%5)*.25f);
                e.setParameter(P::DuckAmount,(k%3)*.40f);
                // Signature matrix retarget intentionally changes; test it separately.
                // Fixed algorithm Signature fingerprint locks all four steady voicings.
                if(profile!=2)e.setAlgorithm(static_cast<arverb::Algorithm>((mode+k)%4));
                e.setParameter(P::DecaySeconds,.15f+(k%8)*1.55f);
                e.setParameter(P::Diffusion,(k%6)*.2f);
                e.setParameter(P::DampingHz,800.f+(k%7)*2800.f);
                e.setParameter(P::LowCutHz,30.f+(k%7)*190.f);
                e.setParameter(P::HighCutHz,1000.f+(k%7)*3000.f);
                e.setParameter(P::ModRateHz,.05f+(k%7)*.3f);
                e.setParameter(P::Width,(k%9)*.2f);
                e.setParameter(P::EarlyLevel,(k%6)*.2f);
            }
            if(block==800)e.reset();
            const int n=blocks[block%blocks.size()];
            for(int i=0;i<n;++i) {
                const double t=(pos+i)/double(fs);
                l[i]=static_cast<float>(.13*std::sin(1382.3*t)+.07*std::sin(6283.2*t));
                r[i]=static_cast<float>(.12*std::sin(1382.3*t+.3)+.05*std::sin(3958.4*t));
            }
            e.process(l.data(),r.data(),a.data(),b.data(),n);
            for(int i=0;i<n;++i)for(float v:{a[i],b[i]}) {
                if(!std::isfinite(v))return 1;
                std::uint32_t bits=0;
                std::memcpy(&bits,&v,sizeof(bits));
                hash=(hash^bits)*1099511628211ull;
            }
            pos+=n;
        }
        std::printf("%d,%d,%d,%016llx\n",int(musical),fs,mode,
                    static_cast<unsigned long long>(hash));
    }
    return 0;
}
int main(int argc,char** argv) {
    if(argc==2&&std::strcmp(argv[1],"--pcm")==0)return pcmFingerprint();
    if(argc==2&&std::strcmp(argv[1],"--pcm-album")==0)return pcmFingerprint(1);
    if(argc==2&&std::strcmp(argv[1],"--pcm-signature")==0)return pcmFingerprint(2);
    using Clock=std::chrono::steady_clock;
    using P=arverb::Parameter;
    constexpr int count=2048;
    double checksum=0.;
    for(int fs:{48000,96000}) for(int n:{32,64,128,256}) {
        if(fs==96000&&n!=32&&n!=128)continue;
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
                x.setParameter(P::DuckAmount,.18f);
#ifdef ARVERB_ALBUM_BENCHMARK
                x.setAlbumReady(argc==2&&std::strcmp(argv[1],"--album")==0);
#ifdef ARVERB_SIGNATURE_BENCHMARK
                if(argc==2&&std::strcmp(argv[1],"--signature")==0)x.setVoicing(2);
#endif
#endif
                x.prepare(fs,true);
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
            std::printf("%d,%d,%d,%d,%.6f,%.6f,%.6f,%.6f\n",fs,n,mode,instances,times[count/2],times[count*99/100],times[count*999/1000],times.back());
        }
    }
    return std::isfinite(checksum)?0:1;
}
