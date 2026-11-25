// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "Wav.h"
#include "../../engine/builtin_fx/de_ezzer/DeEzzerCore.h"
namespace dez_render {
using namespace armix::builtin_fx::de_ezzer;
struct Result { dez_wav::Audio wet,delta; };
inline Result render(const dez_wav::Audio& dry,Settings settings,unsigned block){
    Core core;dez_wav::require(block>0&&block<=65536&&core.prepare(dry.rate,static_cast<int>(dry.channels))&&core.setSettings(settings),"invalid render parameters");
    const auto n=dry.pcm[0].size();dez_wav::require(n>0&&(dry.channels==1||dry.pcm[1].size()==n),"invalid audio shape");
    Result r{dry,dry};
    for(std::size_t i=0;i<n;){const auto count=std::min<std::size_t>(block,n-i);core.process(r.wet.pcm[0].data()+i,dry.channels==2?r.wet.pcm[1].data()+i:nullptr,static_cast<int>(count));i+=count;}
    for(unsigned c=0;c<dry.channels;++c)for(std::size_t i=0;i<n;++i)r.delta.pcm[c][i]=dry.pcm[c][i]-r.wet.pcm[c][i];
    return r;
}
} // namespace dez_render
