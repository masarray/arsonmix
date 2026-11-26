// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
namespace armix::builtin_fx::anti_noiz {
// Positive-only evidence of a persistent narrow-band source. Two-hop lag
// avoids the shared samples of adjacent 50%-overlapped WOLA windows.
// It is not a speech detector and may also recognize tonal noise.
template<int MaxN> class ForegroundCoherence final {
    using Complex=std::complex<float>;
    static constexpr int tracked=5;
public:
    using Spectrum=std::array<std::array<Complex,MaxN>,2>;
    void prepare(double rate,int hop,int channels) noexcept {
        channels_=channels;alpha_=float(1.-std::exp(-double(hop)/(rate*.055)));reset();
    }
    void reset() noexcept {history_={};corr_={};energy_={};lagEnergy_={};evidence_={};index_=count_=0;}
    void update(const Spectrum& spectrum,bool enabled) noexcept {
        if(!enabled){if(count_)reset();return;}
        for(int c=0;c<channels_;++c)for(int k=2;k<tracked;++k){
            const auto x=spectrum[c][k],y=history_[index_][c][k];
            history_[index_][c][k]=x;
            if(count_<2)continue;
            auto& z=corr_[c][k];z+=alpha_*(x*std::conj(y)-z);
            auto& e=energy_[c][k];e+=alpha_*(std::norm(x)-e);
            auto& q=lagEnergy_[c][k];q+=alpha_*(std::norm(y)-q);
            evidence_[c][k]=std::clamp(std::norm(z)/std::max(e*q,1e-20f),0.f,1.f);
        }
        index_^=1;count_=std::min(count_+1,64);
    }
    float support(int c,int bin) const noexcept {return count_>=12&&bin>=2&&bin<tracked?evidence_[c][bin]:0.f;}
private:
    std::array<std::array<std::array<Complex,tracked>,2>,2> history_{};
    std::array<std::array<Complex,tracked>,2> corr_{};
    std::array<std::array<float,tracked>,2> energy_{},lagEnergy_{},evidence_{};
    int channels_=0,index_=0,count_=0;float alpha_=1.f;
};
}
