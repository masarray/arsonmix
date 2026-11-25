// SPDX-License-Identifier: AGPL-3.0-only
#include "OfflineAudio.h"
#include <iostream>
#include <string_view>
using namespace armix::builtin_fx::anti_noiz;
int main(int argc,char** argv){
    if(argc!=5){std::cerr<<"Usage: anti_noiz_render noise.wav|- input.wav output-prefix auto|voice|singing|music|auto-mic|auto-singing|auto-music|auto-mixed\n";return 2;}
    try{
        Core::Settings s;Core::SourceRole source=Core::SourceRole::Unknown;const std::string_view mode=argv[4];
        if(mode=="voice")s.mode=Core::Mode::Voice;else if(mode=="singing")s.mode=Core::Mode::Singing;else if(mode=="music")s.mode=Core::Mode::Music;else if(mode=="auto-mic")source=Core::SourceRole::SpeechMic;else if(mode=="auto-singing")source=Core::SourceRole::SingingMic;else if(mode=="auto-music")source=Core::SourceRole::Music;else if(mode=="auto-mixed")source=Core::SourceRole::Mixed;else offline::require(mode=="auto","unknown mode");
        const auto input=offline::read(argv[2]);offline::Audio noise;const bool learn=std::string_view(argv[1])!="-";if(learn)noise=offline::read(argv[1]);
        const std::string prefix=argv[3];const auto processed=prefix+"-processed.wav",delta=prefix+"-delta.wav";
        offline::require(!std::filesystem::exists(processed)&&!std::filesystem::exists(delta),"output already exists");
        const auto result=offline::render(input,learn?&noise:nullptr,s,128,source);offline::write(processed,result.processed);offline::write(delta,result.delta);
        std::cout<<"Latency compensated: "<<result.latency<<" samples; input/output/delta RMS: "<<result.inputRms<<' '<<result.outputRms<<' '<<result.deltaRms<<'\n';
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}return 0;
}
