// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <utility>

// CONTROL / IO only. Limits apply to actual bytes, not just file/ZIP metadata.
// This is structural preflight; the existing typed JSON codecs remain authoritative.
namespace armix::storage {
struct BoundedDataInput final {
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
        if(maximum<0||!file.existsAsFile()||file.isSymbolicLink())return false;
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
        const auto value=bytes.toString();
        if(value.getNumBytesAsUTF8()>static_cast<std::size_t>(maximum))return false;
        result=value;
        return true;
    }
};
} // namespace armix::storage
