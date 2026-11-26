// SPDX-License-Identifier: AGPL-3.0-only
#include "WindPeriodCandidate.h"
#include <bit>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>
using armix::anti_noiz::research::WindPeriodCandidate;
using Audio=std::vector<float>;
void require(bool b,const char* s){if(!b)throw std::runtime_error(s);}
Audio render(const Audio& input,int rate,int channels,int block,float amount=1){
    require(channels==1||channels==2,"channels");
    require(!input.empty()&&input.size()%channels==0&&block>0&&block<=1024,"layout");
    WindPeriodCandidate c;require(c.prepare(rate,channels)&&c.setAmount(amount),"prepare");
    const int delay=c.latencySamples();require(delay==(rate>48000?512:256),"latency");
    const auto frames=input.size()/channels;
    Audio result(input.size());std::array<float,1024> l{},r{};
    for(std::size_t p=0;p<frames+delay;){
        const int n=int(std::min<std::size_t>(block,frames+delay-p));
        for(int i=0;i<n;++i){const auto j=p+i;l[i]=j<frames?input[j*channels]:0;
            r[i]=channels==2&&j<frames?input[j*channels+1]:0;}
        c.process(l.data(),r.data(),n);
        for(int i=0;i<n;++i){const auto t=p+i;if(t<std::size_t(delay)||t-delay>=frames)continue;
            const auto j=(t-delay)*channels;result[j]=l[i];if(channels==2)result[j+1]=r[i];}
        p+=n;
    }
    return result;
}
void selfTest(){
    for(int rate:{44100,48000,88200,96000})for(int ch:{1,2}){
        Audio x(std::size_t(rate)*2*ch),s=x;
        for(std::size_t i=0;i<x.size()/ch;++i){const double t=double(i)/rate;
            for(int c=0;c<ch;++c){const double phase=c*.6;const auto j=i*ch+c;
                s[j]=float(.1*std::sin(2*std::numbers::pi*120*t+phase)+.03*std::sin(2*std::numbers::pi*600*t+phase));
                x[j]=s[j]+float(.12*std::sin(2*std::numbers::pi*21*t+phase));}}
        const auto start=std::chrono::steady_clock::now();const auto a=render(x,rate,ch,127);
        const auto elapsed=std::chrono::duration<double>(std::chrono::steady_clock::now()-start).count();
        require(a==render(x,rate,ch,64)&&a==render(x,rate,ch,1),"partition");
        require(x==render(x,rate,ch,127,0),"unity");
        Audio prefix(x.begin(),x.begin()+rate*ch);const auto b=render(prefix,rate,ch,71);
        require(std::equal(b.begin(),b.end()-(rate>48000?512:256)*ch,a.begin()),"bounded lookahead");
        double in=0,out=0,ref=0,proj=0;
        for(std::size_t i=std::size_t(rate/2)*ch;i<x.size()-1024*ch;++i){in+=std::pow(x[i]-s[i],2);out+=std::pow(a[i]-s[i],2);ref+=s[i]*s[i];proj+=s[i]*a[i];}
        const double db=10*std::log10(in/out),projection=proj/ref;
        std::cout<<"WIND_PERIOD_NATIVE "<<rate<<' '<<ch<<"ch "<<db<<"dB projection "<<projection<<" wall/audio "<<elapsed/2<<'\n';
        require(std::isfinite(out)&&db>10&&projection>.97&&projection<1.03,"synthetic pitch ambiguity");
        Audio dc(std::size_t(rate)*ch,.1f);const auto zero=render(dc,rate,ch,127);
        require(std::all_of(zero.begin()+rate/2*ch,zero.end()-1024*ch,[](float v){return std::abs(v)<1e-6f;}),"bounded prefix wrap DC");
        if(ch==2){Audio right(x.size()/2);for(std::size_t i=0;i<right.size();++i)right[i]=x[2*i+1];const auto mono=render(right,rate,1,127);for(std::size_t i=0;i<right.size();++i)require(mono[i]==a[2*i+1],"channel isolation");}
    }
    WindPeriodCandidate c;require(!c.prepare(0,1)&&c.prepare(48000,1)&&!c.setAmount(-1)&&!c.setAmount(NAN),"invalid parameters");
    std::array<float,512> z{};z[0]=NAN;z[1]=INFINITY;c.process(z.data(),nullptr,int(z.size()));
    require(std::all_of(z.begin(),z.end(),[](float v){return std::isfinite(v);}),"finite PCM");
    c.reset();z.fill(0);c.process(z.data(),nullptr,int(z.size()));require(std::all_of(z.begin(),z.end(),[](float v){return v==0;}),"reset");
    std::cout<<"WIND_PERIOD_NATIVE_SELFTEST PASS\n";
}
int main(int argc,char** argv){try{
    if(argc==2&&std::string_view(argv[1])=="--self-test"){selfTest();return 0;}
    require(argc==4,"usage: wind_period_candidate rate mono-f32le-input mono-f32le-output");
    static_assert(sizeof(float)==4&&std::endian::native==std::endian::little);
    const int rate=std::stoi(argv[1]);std::ifstream f(argv[2],std::ios::binary|std::ios::ate);
    require(bool(f),"input");const auto bytes=f.tellg();require(bytes>0&&bytes%4==0&&bytes<=64*1024*1024,"input size");
    f.seekg(0);Audio x(static_cast<std::size_t>(bytes)/4);
    require(bool(f.read(reinterpret_cast<char*>(x.data()),bytes)),"read PCM");
    const auto y=render(x,rate,1,127);std::ofstream o(argv[3],std::ios::binary);
    require(bool(o.write(reinterpret_cast<const char*>(y.data()),bytes)),"write PCM");
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;}
