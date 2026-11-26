// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <utility>
#include <limits>

// CONTROL / IO only. Limits apply to actual bytes, not just file/ZIP metadata.
// This is structural preflight; the existing typed JSON codecs remain authoritative.
namespace armix::storage {
struct BoundedDataInput final {
    // Strict UTF-8 for persisted text and receipts, including embedded NUL
    // rejection. JUCE String alone cannot distinguish truncated NUL input.
    static bool decodeUtf8(const juce::MemoryBlock& bytes,juce::String& result) {
        const auto* p=static_cast<const unsigned char*>(bytes.getData());
        const auto n=bytes.getSize();
        if(n==0||n>static_cast<std::size_t>(std::numeric_limits<int>::max()))return false;
        std::size_t i=0;
        if(n>=3&&p[0]==0xef&&p[1]==0xbb&&p[2]==0xbf)i=3;
        const auto start=i;
        if(start==n)return false;
        while(i<n) {
            const auto a=p[i++];
            if(a==0)return false;
            if(a<0x80)continue;
            unsigned remaining=0; unsigned cp=0,minimum=0;
            if(a>=0xc2&&a<=0xdf){remaining=1;cp=a&0x1f;minimum=0x80;}
            else if(a>=0xe0&&a<=0xef){remaining=2;cp=a&0x0f;minimum=0x800;}
            else if(a>=0xf0&&a<=0xf4){remaining=3;cp=a&7;minimum=0x10000;}
            else return false;
            if(n-i<remaining)return false;
            for(unsigned j=0;j<remaining;++j){
                const auto b=p[i++];if((b&0xc0)!=0x80)return false;
                cp=(cp<<6)|(b&0x3f);
            }
            if(cp<minimum||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))return false;
        }
        auto value=juce::String::fromUTF8(reinterpret_cast<const char*>(p+start),static_cast<int>(n-start));
        if(value.getNumBytesAsUTF8()!=n-start)return false;
        result=std::move(value);
        return true;
    }
    static bool jsonStructure(const juce::String& text) {
        const auto* bytes=text.toRawUTF8();
        const auto size=text.getNumBytesAsUTF8();
        std::array<char,64> stack{};
        std::size_t depth=0;
        bool quoted=false, escaped=false;
        for(std::size_t i=0;i<size;++i) {
            const char c=bytes[i];
            if(quoted) {
                if(static_cast<unsigned char>(c)<0x20)return false;
                if(escaped)escaped=false;
                else if(c=='\\')escaped=true;
                else if(c=='"')quoted=false;
                continue;
            }
            if(c=='"'){quoted=true;continue;}
            if(c=='{'||c=='[') {
                if(depth==stack.size())return false;
                stack[depth++]=c;
            } else if(c=='}'||c==']') {
                if(depth==0||stack[depth-1]!=(c=='}'?'{':'['))return false;
                --depth;
            } else if(static_cast<unsigned char>(c)<0x20&&c!='\t'&&c!='\r'&&c!='\n')return false;
        }
        return !quoted&&!escaped&&depth==0;
    }
    // Reject symbolic links on the managed path below (and including) root.
    // Not a kernel handle-based sandbox or protection from concurrent local
    // replacement; user-selected external workspace root remains trusted.
    static bool managedPath(const juce::File& root,const juce::File& child) {
        if(root==juce::File()||child==juce::File()||
           (child!=root&&!child.isAChildOf(root)))return false;
        auto current=child;
        for(int i=0;i<128;++i) {
            if(current.isSymbolicLink())return false;
            if(current==root)return true;
            const auto parent=current.getParentDirectory();
            if(parent==current)return false;
            current=parent;
        }
        return false;
    }
    static bool read(const juce::File& file,juce::int64 maximum,
                     juce::MemoryBlock& result,bool allowEmpty=false) {
        if(maximum<0||maximum==std::numeric_limits<juce::int64>::max()||!file.existsAsFile()||file.isSymbolicLink())return false;
        juce::FileInputStream input(file);
        if(!input.openedOk())return false;
        const auto size=input.getTotalLength();
        if(size<0||size>maximum||(!allowEmpty&&size==0))return false;
        juce::MemoryBlock temp;
        const auto count=input.readIntoMemoryBlock(temp,maximum+1);
        if(count!=size||static_cast<juce::int64>(temp.getSize())!=size||
           input.getStatus().failed())return false;
        result=std::move(temp);
        return true;
    }
    static bool text(const juce::File& file,juce::int64 maximum,juce::String& result) {
        juce::MemoryBlock bytes;
        if(!read(file,maximum,bytes))return false;
        return decodeUtf8(bytes,result);
    }
};
} // namespace armix::storage
