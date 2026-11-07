// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "NativeMixerSessionDocument.h"
#include <juce_cryptography/juce_cryptography.h>
#include <algorithm>
#include <set>
#include <utility>
#include <vector>

// One durable device-independent MixerSessionDocument per Studio workspace.
// No audio thread access. Generations are immutable. A single atomic HEAD
// pointer is the commit boundary, and PREVIOUS remains explicit recovery only.
namespace armix::session {
class NativeMixerSessionStore final {
public:
    explicit NativeMixerSessionStore(juce::File studio)
        : root_(studio.getChildFile("MixerSession")) {}
    static constexpr juce::int64 maxJson=4*1024*1024;
    static constexpr juce::int64 maxState=8*1024*1024;
    static constexpr juce::int64 maxTotal=128*1024*1024;
    static constexpr std::size_t maxStates=16384;

    juce::String commit(const MixerSessionDocument& doc,
                        const std::vector<armix::shows::BinaryState>& states) const {
        if(!armix::shows::detail::uuid(doc.id)||states.size()>maxStates)
            return "Mixer session ID/state count invalid";
        const auto json=Codec::encode(doc);
        if(json.getNumBytesAsUTF8()>maxJson)return "Mixer session document exceeds 4 MB";
        MixerSessionDocument verified;
        if(!Codec::decode(json,verified))return "Mixer session graph cannot be serialized safely";
        std::set<juce::String> hashes,referenced;
        juce::int64 total=json.getNumBytesAsUTF8();
        for(const auto& fx:doc.inserts)referenced.insert(fx.sha256.toLowerCase());
        for(const auto& state:states) {
            if(!safeHash(state.sha256)||!hashes.insert(state.sha256.toLowerCase()).second||
               state.bytes.getSize()>static_cast<size_t>(maxState)||
               juce::SHA256(state.bytes).toHexString()!=state.sha256.toLowerCase())
                return "Mixer vendor state duplicate, oversized or wrong SHA";
            total+=static_cast<juce::int64>(state.bytes.getSize());
            if(total>maxTotal)return "Mixer session exceeds 128 MB budget";
        }
        if(hashes!=referenced)return "Mixer vendor state manifest is incomplete";
        if(!root_.isDirectory()&&root_.createDirectory().failed())
            return "MixerSession directory is not writable";
        const auto versions=root_.getChildFile("versions");
        if(!versions.isDirectory()&&versions.createDirectory().failed())
            return "MixerSession versions directory unavailable";
        const auto id=juce::Uuid().toDashedString().toLowerCase();
        if(!armix::shows::detail::uuid(id))return "Cannot allocate generation";
        const auto stage=root_.getChildFile(".staging-"+id);
        if(stage.exists()||stage.createDirectory().failed())
            return "Cannot create mixer staging directory";
        struct Clean final {
            juce::File dir;
            ~Clean(){if(dir.exists())dir.deleteRecursively();}
        } cleanup{stage};
        const auto payload=stage.getChildFile("blobs");
        if(payload.createDirectory().failed())return "Cannot create vendor state directory";
        const auto checksum=juce::SHA256(json.toRawUTF8(),json.getNumBytesAsUTF8()).toHexString();
        if(!stage.getChildFile("mixer.json").replaceWithText(json)||
           !stage.getChildFile("mixer.sha256").replaceWithText(checksum))
            return "Cannot write mixer document or checksum";
        for(const auto& state:states) {
            const auto path=payload.getChildFile(state.sha256.toLowerCase()+".bin");
            if(!path.replaceWithData(state.bytes.getData(),state.bytes.getSize())||
               path.getSize()!=static_cast<juce::int64>(state.bytes.getSize())||
               !juce::SHA256(path).toHexString().equalsIgnoreCase(state.sha256))
                return "Cannot durably stage verified vendor state";
        }
        MixerSessionDocument reread;
        std::vector<armix::shows::BinaryState> copies;
        if(readGeneration(stage,reread,copies).isNotEmpty()||
           Codec::encode(reread)!=json)return "Staged session readback changed";
        const auto committed=versions.getChildFile(id);
        if(committed.exists()||!stage.moveFileTo(committed))
            return "Cannot publish immutable mixer generation";
        const auto head=root_.getChildFile("HEAD");
        if(head.existsAsFile()) {
            const auto previous=readPointer(head);
            if(previous.isEmpty())return "Current mixer HEAD invalid; recover explicitly";
            MixerSessionDocument older;
            std::vector<armix::shows::BinaryState> oldStates;
            const auto error=readGeneration(versions.getChildFile(previous),older,oldStates);
            if(error.isNotEmpty())
                return "Current mixer generation invalid; preserve it for recovery: "+error;
            const auto backupError=writePointer(root_.getChildFile("PREVIOUS"),previous);
            if(backupError.isNotEmpty())return backupError;
        }
        return writePointer(head,id);
    }

    // Read normal HEAD only: no silent fallback to an older performance.
    juce::String load(MixerSessionDocument& out,
                      std::vector<armix::shows::BinaryState>& states) const {
        const auto pointer=readPointer(root_.getChildFile("HEAD"));
        if(pointer.isEmpty())return "Mixer session HEAD missing or invalid";
        MixerSessionDocument doc;
        std::vector<armix::shows::BinaryState> binary;
        const auto error=readGeneration(root_.getChildFile("versions").getChildFile(pointer),
                                        doc,binary);
        if(error.isNotEmpty())return error;
        if(readPointer(root_.getChildFile("HEAD"))!=pointer)
            return "Mixer HEAD changed during read";
        out=std::move(doc);states=std::move(binary);
        return {};
    }
    bool exists() const noexcept {return root_.getChildFile("HEAD").existsAsFile();}
    // Recovery does NOT arm output or publish a graph. Valid HEAD wins.
    juce::String recoverPrevious() const {
        MixerSessionDocument current;
        std::vector<armix::shows::BinaryState> currentStates;
        if(load(current,currentStates).isEmpty())
            return "Current mixer is verified; refusing rollback";
        const auto pointer=readPointer(root_.getChildFile("PREVIOUS"));
        if(pointer.isEmpty())return "Last-good mixer pointer missing";
        MixerSessionDocument verified;
        std::vector<armix::shows::BinaryState> binary;
        const auto error=readGeneration(root_.getChildFile("versions").getChildFile(pointer),
                                        verified,binary);
        if(error.isNotEmpty())return "Previous mixer generation invalid: "+error;
        return writePointer(root_.getChildFile("HEAD"),pointer);
    }
private:
    juce::File root_;
    static bool safeHash(const juce::String& hash) {
        return hash.length()==64 && hash.containsOnly("0123456789abcdef");
    }
    static juce::String readPointer(const juce::File& file) {
        if(!file.existsAsFile()||file.getSize()<1||file.getSize()>128)return {};
        const auto name=file.loadFileAsString().trim();
        return armix::shows::detail::uuid(name)?name:juce::String{};
    }
    static juce::String writePointer(const juce::File& file,const juce::String& id) {
        if(!armix::shows::detail::uuid(id))return "Invalid mixer generation pointer";
        juce::TemporaryFile staging(file);
        auto output=staging.getFile().createOutputStream();
        if(!output||!output->writeText(id+"\n",false,false,"\n"))
            return "Cannot stage mixer HEAD";
        output->flush();
        if(output->getStatus().failed())return "Cannot flush mixer HEAD";
        output.reset();
        return staging.overwriteTargetFileWithTemporary()
            ?juce::String{}:juce::String("Cannot atomically publish mixer HEAD");
    }
    static juce::String readGeneration(const juce::File& generation,
                       MixerSessionDocument& out,
                       std::vector<armix::shows::BinaryState>& states) {
        const auto jsonFile=generation.getChildFile("mixer.json");
        const auto sumFile=generation.getChildFile("mixer.sha256");
        if(!generation.isDirectory()||!jsonFile.existsAsFile()||
           jsonFile.getSize()<1||jsonFile.getSize()>maxJson||
           !sumFile.existsAsFile()||sumFile.getSize()!=64)
            return "Mixer JSON generation missing/oversized";
        const auto text=jsonFile.loadFileAsString();
        const auto expected=sumFile.loadFileAsString().trim();
        if(!safeHash(expected)||
           juce::SHA256(text.toRawUTF8(),text.getNumBytesAsUTF8()).toHexString()!=expected)
            return "Mixer document SHA mismatch";
        MixerSessionDocument doc;
        if(!Codec::decode(text,doc))
            return "Mixer session semantic validation rejected";
        std::set<juce::String> hashes;
        for(const auto& insert:doc.inserts)hashes.insert(insert.sha256);
        if(hashes.size()>maxStates)return "Mixer vendor state reference count exceeded";
        std::vector<armix::shows::BinaryState> binaries;
        juce::int64 total=0;
        for(const auto& hash:hashes) {
            if(!safeHash(hash))return "Unsafe mixer state hash";
            const auto file=generation.getChildFile("blobs").getChildFile(hash+".bin");
            if(!file.existsAsFile()||file.getSize()<0||file.getSize()>maxState)
                return "Mixer VST3 state missing or oversized";
            total+=file.getSize();
            if(total>maxTotal)return "Mixer vendor state budget exceeded";
            armix::shows::BinaryState blob;
            blob.sha256=hash;
            if(!file.loadFileAsData(blob.bytes)||
               juce::SHA256(blob.bytes).toHexString()!=hash)
                return "Mixer vendor state changed on disk";
            binaries.push_back(std::move(blob));
        }
        out=std::move(doc);states=std::move(binaries);
        return {};
    }
};
} // namespace armix::session
