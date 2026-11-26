// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "NativePresetArchive.h"
#include <cstdint>
#include <cstring>
namespace armix::storage {
// Rack-only portable file. No channel, hardware, routing, fader, hotkey or
// instance token is imported. Binary state is opaque; executable identity
// must be validated against the current native catalog before preparation.
struct FxRackFile final {
    bool single=false,rackBypassed=false;
    std::vector<FxState> effects;
};
class NativeFxRackArchive final {
    static constexpr int maxBytes=72*1024*1024;
    static constexpr char magic[]="ARFX0001";
    static bool valid(const FxState& fx) {
        return fx.catalogId.isNotEmpty()&&fx.catalogId.getNumBytesAsUTF8()<=256&&
            fx.name.isNotEmpty()&&fx.name.getNumBytesAsUTF8()<=1024&&
            fx.path.getNumBytesAsUTF8()<=4096&&
            fx.binary.getSize()<=NativePresetArchive::maxStateBytes;
    }
    static bool put(juce::MemoryOutputStream& out,const juce::String& s){
        const auto n=s.getNumBytesAsUTF8();
        return out.writeInt(static_cast<int>(n))&&out.write(s.toRawUTF8(),n);
    }
    static bool get(juce::MemoryInputStream& in,juce::String& s,int limit){
        if(in.getNumBytesRemaining()<4)return false;
        const int n=in.readInt();
        if(n<0||n>limit||in.getNumBytesRemaining()<n)return false;
        std::vector<char> bytes(static_cast<std::size_t>(n)+1,0);
        if(in.read(bytes.data(),n)!=n||std::memchr(bytes.data(),0,static_cast<std::size_t>(n)))return false;
        s=juce::String::fromUTF8(bytes.data(),n);
        return s.getNumBytesAsUTF8()==static_cast<std::size_t>(n)&&
            std::memcmp(s.toRawUTF8(),bytes.data(),static_cast<std::size_t>(n))==0;
    }
public:
    static juce::String encode(const FxRackFile& rack,juce::MemoryBlock& output){
        if(rack.effects.size()>8||(rack.single&&rack.effects.size()!=1))
            return "Invalid FX rack count";
        juce::MemoryOutputStream out;
        if(!out.write(magic,8)||!out.writeByte(rack.single?1:0)||
           !out.writeByte(rack.rackBypassed?1:0)||
           !out.writeInt(static_cast<int>(rack.effects.size())))return "FX rack encode failed";
        for(const auto& fx:rack.effects){
            if(!valid(fx)||!put(out,fx.catalogId)||!put(out,fx.name)||!put(out,fx.path)||
               !out.writeInt(fx.uid)||!out.writeByte(fx.bypass?1:0)||
               !out.writeInt(static_cast<int>(fx.binary.getSize()))||
               (fx.binary.getSize()&&!out.write(fx.binary.getData(),fx.binary.getSize())))
                return "Invalid FX identity or state";
        }
        const auto hash=juce::SHA256(out.getData(),out.getDataSize()).getRawData();
        if(!out.write(hash.getData(),hash.getSize())||out.getDataSize()>maxBytes)
            return "FX rack size limit";
        output=out.getMemoryBlock();return {};
    }
    static juce::String decode(const juce::MemoryBlock& bytes,FxRackFile& output){
        if(bytes.getSize()<46||bytes.getSize()>maxBytes)return "Invalid FX rack size";
        const auto payload=bytes.getSize()-32;
        const auto hash=juce::SHA256(bytes.getData(),payload).getRawData();
        if(std::memcmp(hash.getData(),static_cast<const char*>(bytes.getData())+payload,32)!=0)
            return "FX rack integrity check failed";
        juce::MemoryInputStream in(bytes.getData(),payload,false);
        char header[8]{};
        if(in.read(header,8)!=8||std::memcmp(header,magic,8)!=0)return "Unsupported FX rack format";
        const int single=in.readByte(),bypass=in.readByte(),count=in.readInt();
        if(single<0||single>1||bypass<0||bypass>1||count<0||count>8||(single&&count!=1))
            return "Invalid FX rack manifest";
        FxRackFile candidate;candidate.single=single!=0;candidate.rackBypassed=bypass!=0;
        for(int i=0;i<count;++i){
            FxState fx;
            if(!get(in,fx.catalogId,256)||!get(in,fx.name,1024)||!get(in,fx.path,4096)||
               in.getNumBytesRemaining()<9)return "Invalid FX rack record";
            fx.uid=in.readInt();const int flag=in.readByte(),size=in.readInt();
            if(flag<0||flag>1||size<0||size>static_cast<int>(NativePresetArchive::maxStateBytes)||
               in.getNumBytesRemaining()<size)return "Invalid FX state size";
            fx.bypass=flag!=0;fx.binary.setSize(static_cast<std::size_t>(size));
            if(size&&in.read(fx.binary.getData(),size)!=size)return "Truncated FX state";
            if(!valid(fx))return "Invalid FX identity";
            candidate.effects.push_back(std::move(fx));
        }
        if(in.getNumBytesRemaining()!=0)return "Unexpected FX rack data";
        output=std::move(candidate);return {};
    }
    static juce::String save(const juce::File& file,const FxRackFile& rack){
        if(!file.hasFileExtension("arfx"))return "Use .arfx file extension";
        juce::MemoryBlock bytes;
        if(auto error=encode(rack,bytes);error.isNotEmpty())return error;
        juce::TemporaryFile temp(file);
        auto stream=temp.getFile().createOutputStream();
        if(!stream||!stream->write(bytes.getData(),bytes.getSize()))return "FX rack write failed";
        stream->flush();if(stream->getStatus().failed())return "FX rack flush failed";
        stream.reset();
        return temp.overwriteTargetFileWithTemporary()?juce::String{}:juce::String("FX rack commit failed");
    }
    static juce::String load(const juce::File& file,FxRackFile& rack){
        if(!file.hasFileExtension("arfx")||!file.existsAsFile()||file.getSize()<46||
           file.getSize()>maxBytes)return "FX rack missing or above safety limit";
        auto stream=file.createInputStream();
        if(!stream)return "FX rack read failed";
        juce::MemoryBlock bytes;
        stream->readIntoMemoryBlock(bytes,maxBytes+1);
        return decode(bytes,rack);
    }
};
}
