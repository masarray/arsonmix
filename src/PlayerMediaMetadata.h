// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_core/juce_core.h>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <initializer_list>

namespace armix::player {
// Worker-only media probe; never run on the audio callback or message thread.
struct PlayerMediaMetadata {
    juce::String title,artist,album,format;
    double seconds=0,sampleRate=0;
    unsigned channels=0;
    bool missing=false;
    static std::uint32_t be(const unsigned char* p){
        return (std::uint32_t(p[0])<<24)|(std::uint32_t(p[1])<<16)|(std::uint32_t(p[2])<<8)|p[3];
    }
    static std::uint32_t le(const unsigned char* p){
        return (std::uint32_t(p[3])<<24)|(std::uint32_t(p[2])<<16)|(std::uint32_t(p[1])<<8)|p[0];
    }
    static std::uint32_t syncSafe(const unsigned char* p){
        if((p[0]|p[1]|p[2]|p[3])&128)return 0;
        return (std::uint32_t(p[0])<<21)|(std::uint32_t(p[1])<<14)|(std::uint32_t(p[2])<<7)|p[3];
    }
    static juce::String text(const unsigned char* p,std::size_t n){
        if(!n)return {};
        return juce::String::fromUTF8(reinterpret_cast<const char*>(p),static_cast<int>(std::min<std::size_t>(n,2048))).trim().substring(0,512);
    }
    // Bounded, read-only ID3v2.3/v2.4 frames. Skip unsupported flags
    // (compressed, encrypted, unsynchronised, extended header) entirely.
    template<class Visitor> static void id3(const juce::File& file,Visitor visit){
        auto in=file.createInputStream();unsigned char head[10]{};
        if(!in||in->read(head,10)!=10||std::memcmp(head,"ID3",3)!=0||
           (head[3]!=3&&head[3]!=4)||head[5]!=0)return;
        const auto size=syncSafe(head+6);
        if(size==0||size>2097152||size>in->getTotalLength()-10)return;
        juce::MemoryBlock block(size);
        if(in->read(block.getData(),static_cast<int>(size))!=static_cast<int>(size))return;
        auto* p=static_cast<const unsigned char*>(block.getData());std::size_t pos=0;
        while(pos+10<=size&&p[pos]!=0){
            const auto n=head[3]==4?syncSafe(p+pos+4):be(p+pos+4);
            if(!n||n>size-pos-10)break;
            if(p[pos+8]==0&&p[pos+9]==0&&visit(p+pos,p+pos+10,n))return;
            pos+=10+n;
        }
    }
    static juce::String id3Text(const unsigned char* p,std::size_t n){
        if(n<2)return {};
        if(p[0]==3)return text(p+1,n-1);
        if(p[0]==0){
            juce::String out;for(std::size_t i=1;i<n&&i<513&&p[i];++i)out+=juce::String::charToString(p[i]);
            return out.trim();
        }
        // UTF16 BOM is handled by JUCE's bounded text conversion.
        if(p[0]==1&&n>=3)return juce::String::createStringFromData(p+1,static_cast<int>(std::min<std::size_t>(n-1,2048)&~std::size_t(1))).trim().substring(0,512);
        return {};
    }
    template<class Visitor> static void flacBlocks(const juce::File& file,Visitor visit){
        auto in=file.createInputStream();
        if(!in||in->readIntBigEndian()!=0x664c6143)return;
        for(int blocks=0;blocks<64&&in->getPosition()<4194304;++blocks){
            unsigned char h[4]{};if(in->read(h,4)!=4)break;
            const auto bytes=(std::uint32_t(h[1])<<16)|(std::uint32_t(h[2])<<8)|h[3];
            const auto start=in->getPosition();
            if(bytes>4194304||start+bytes>in->getTotalLength())break;
            if(((h[0]&127)==4||(h[0]&127)==6)&&bytes<=270336){
                juce::MemoryBlock block(bytes);
                if(in->read(block.getData(),static_cast<int>(bytes))==static_cast<int>(bytes)&&
                   visit(h[0]&127,static_cast<const unsigned char*>(block.getData()),bytes))return;
            }
            if(h[0]&128)break;
            if(!in->setPosition(start+bytes))break;
        }
    }
    static PlayerMediaMetadata probe(const juce::File& file,juce::AudioFormatManager& formats){
        PlayerMediaMetadata m;
        m.title=file.getFileNameWithoutExtension().substring(0,512);
        m.missing=!file.existsAsFile();
        if(m.missing)return m;
        std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
        if(!reader)return m;
        m.format=reader->getFormatName().substring(0,64);
        if(std::isfinite(reader->sampleRate)&&reader->sampleRate>0&&reader->lengthInSamples>0){
            m.sampleRate=reader->sampleRate;m.seconds=static_cast<double>(reader->lengthInSamples)/m.sampleRate;
        }
        m.channels=reader->numChannels;
        const auto tag=[&](std::initializer_list<const char*> keys){
            for(auto key:keys){auto value=reader->metadataValues.getValue(key,{}).trim().substring(0,512);if(value.isNotEmpty())return value;}
            return juce::String{};
        };
        const auto title=tag({"title","TITLE","id3title","INAM"});
        if(title.isNotEmpty())m.title=title;
        m.artist=tag({"artist","ARTIST","id3artist","IART"});
        m.album=tag({"album","ALBUM","id3album","IPRD"});
        if(file.hasFileExtension("mp3"))id3(file,[&](const unsigned char* frame,const unsigned char* p,std::size_t n){
            auto value=id3Text(p,n);if(value.isEmpty())return false;
            if(std::memcmp(frame,"TIT2",4)==0)m.title=value;
            else if(std::memcmp(frame,"TPE1",4)==0)m.artist=value;
            else if(std::memcmp(frame,"TALB",4)==0)m.album=value;
            return false;
        });
        if(file.hasFileExtension("flac"))flacBlocks(file,[&](int type,const unsigned char* p,std::size_t n){
            if(type!=4||n<8)return false;
            std::size_t pos=0;
            auto read=[&](std::uint32_t& v){if(pos+4>n)return false;v=le(p+pos);pos+=4;return true;};
            std::uint32_t len=0,count=0;
            if(!read(len)||len>n-pos)return false;
            pos+=len;if(!read(count))return false;
            for(std::uint32_t i=0;i<count&&i<4096;++i){
                if(!read(len)||len>n-pos)break;
                auto entry=text(p+pos,len);pos+=len;
                const auto key=entry.upToFirstOccurrenceOf("=",false,false);
                const auto value=entry.fromFirstOccurrenceOf("=",false,false);
                if(value.isEmpty())continue;
                if(key.equalsIgnoreCase("TITLE"))m.title=value;
                else if(key.equalsIgnoreCase("ARTIST"))m.artist=value;
                else if(key.equalsIgnoreCase("ALBUM"))m.album=value;
            }
            return false;
        });
        return m;
    }
    juce::var project(int id)const{
        auto obj=std::make_unique<juce::DynamicObject>();
        obj->setProperty("catalogId",id);obj->setProperty("title",title);
        obj->setProperty("artist",artist);obj->setProperty("album",album);
        obj->setProperty("format",format);obj->setProperty("durationSeconds",seconds);
        obj->setProperty("sampleRate",sampleRate);obj->setProperty("channels",static_cast<int>(channels));
        obj->setProperty("missing",missing);return juce::var(obj.release());
    }
    // Do not decode untrusted artwork in the native audio process.
    // Accept only bounded PNG/JPEG bytes and dimensions.
    static juce::String imageUri(const unsigned char* p,std::size_t n){
        if(n<24||n>262144)return {};
        const auto valid=[](std::uint32_t w,std::uint32_t h){return w>0&&h>0&&w<=2048&&h<=2048;};
        const char* mime=nullptr;
        if(std::memcmp(p,"\x89PNG\r\n\x1a\n",8)==0&&std::memcmp(p+12,"IHDR",4)==0&&valid(be(p+16),be(p+20)))mime="image/png";
        else if(p[0]==0xff&&p[1]==0xd8){
            std::size_t i=2;
            while(i+4<=n&&p[i]==0xff){
                const auto marker=p[i+1];i+=2;
                if(marker==0xd9||marker==0xda)break;
                if(marker==0xff){--i;continue;}
                const std::size_t len=(std::size_t(p[i])<<8)|p[i+1];
                if(len<2||len>n-i)break;
                if((marker==0xc0||marker==0xc1||marker==0xc2)&&len>=8){
                    if(valid((std::uint32_t(p[i+5])<<8)|p[i+6],(std::uint32_t(p[i+3])<<8)|p[i+4]))mime="image/jpeg";
                    break;
                }
                i+=len;
            }
        }
        return mime?juce::String("data:")+mime+";base64,"+juce::Base64::toBase64(p,n):juce::String{};
    }
    static juce::String cover(const juce::File& file){
        juce::String result;
        if(file.hasFileExtension("flac"))flacBlocks(file,[&](int type,const unsigned char* p,std::size_t n){
            if(type!=6)return false;
            std::size_t pos=0;
            auto read=[&](std::uint32_t& v){if(pos+4>n)return false;v=be(p+pos);pos+=4;return true;};
            std::uint32_t kind=0,len=0;
            if(!read(kind)||kind!=3||!read(len)||len>n-pos)return false;
            pos+=len;if(!read(len)||len>n-pos)return false;
            pos+=len;if(n-pos<16)return false;
            pos+=16;if(!read(len)||len>n-pos)return false;
            result=imageUri(p+pos,len);return result.isNotEmpty();
        });
        if(file.hasFileExtension("mp3"))id3(file,[&](const unsigned char* frame,const unsigned char* p,std::size_t n){
            if(std::memcmp(frame,"APIC",4)!=0||n<5||(p[0]!=0&&p[0]!=3))return false;
            std::size_t i=1;while(i<n&&p[i])++i;
            if(i+2>=n||p[++i]!=3)return false; // front cover
            ++i;while(i<n&&p[i])++i;
            if(i>=n)return false;
            ++i;result=imageUri(p+i,n-i);return result.isNotEmpty();
        });
        if(result.isNotEmpty())return result;
        // Exact-basename sidecar, no arbitrary folder art/online fetching.
        for(const auto* ext:{".jpg",".jpeg",".png"}){
            const auto cover=file.withFileExtension(ext);
            auto stream=cover.createInputStream();
            if(!stream||stream->getTotalLength()<24||stream->getTotalLength()>262144)continue;
            juce::MemoryBlock bytes(static_cast<std::size_t>(stream->getTotalLength()));
            if(stream->read(bytes.getData(),static_cast<int>(bytes.getSize()))==static_cast<int>(bytes.getSize())){
                auto uri=imageUri(static_cast<const unsigned char*>(bytes.getData()),bytes.getSize());if(uri.isNotEmpty())return uri;
            }
        }
        return {};
    }
};
}
