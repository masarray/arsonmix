// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "NativeMixerSessionDocument.h"
#include "BoundedDataInput.h"
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
    using Input=armix::storage::BoundedDataInput;
    // Cold-start emergency path only, before any session worker or driver exists.
    // Preserve even corrupt journals byte-for-byte; do not read or delete them.
    juce::String quarantineForStartupRecovery(juce::File& archived) const {
        archived=juce::File{};
        if(!root_.exists())return {};
        const auto id=juce::Uuid().toDashedString().toLowerCase();
        if(!armix::shows::detail::uuid(id))return "Cannot allocate recovery identity";
        const auto target=root_.getSiblingFile("MixerSession-Recovery-"+id);
        if(target.exists()||!root_.moveFileTo(target))
            return "Cannot archive MixerSession; close ArSonMix and verify folder access";
        archived=target;
        return {};
    }
    static constexpr juce::int64 maxJson=4*1024*1024;
    static constexpr juce::int64 maxState=8*1024*1024;
    static constexpr juce::int64 maxTotal=128*1024*1024;
    static constexpr std::size_t maxStates=16384;

    juce::String commit(const MixerSessionDocument& doc,
                        const std::vector<armix::shows::BinaryState>& states) const {
        const auto head=root_.getChildFile("HEAD");
        // A missing HEAD with PREVIOUS is recovery evidence, not a new
        // workspace. A directory or invalid pointer is likewise not fresh.
        if(!head.existsAsFile() && (head.exists()||root_.getChildFile("PREVIOUS").exists()))
            return "Mixer checkpoint requires recovery before saving";
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
        if(!Input::managedPath(root_,root_.getChildFile("versions"))||
           !Input::managedPath(root_,root_.getChildFile("HEAD"))||
           !Input::managedPath(root_,root_.getChildFile("PREVIOUS")))return "Unsafe mixer session storage path";
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

    // Explicit NEW replaces HEAD only after archiving the independently
    // verified previous checkpoint. PREVIOUS alone is not enough: the next
    // normal edit rotates it. All IO stays on the existing session worker.
    enum class ArchiveMode { none, newSession, resetIo, reopenArchive };
    juce::File archiveDirectory() const {return root_.getSiblingFile("SessionArchive");}
    juce::String commitNewSession(const MixerSessionDocument& doc,
                        const std::vector<armix::shows::BinaryState>& states,
                        ArchiveMode mode=ArchiveMode::newSession) const {
        const bool reset=mode==ArchiveMode::resetIo;
        const bool reopen=mode==ArchiveMode::reopenArchive;
        if(mode==ArchiveMode::none || doc.resumeMain.value_or(false) ||
           (!reopen && (!doc.logicalOnly ||
             !doc.logicalInputs.references().empty() || !doc.logicalOutputs.references().empty() ||
             (!reset && (!states.empty() || !doc.inserts.empty() || !doc.hotkeys.empty() ||
                         !doc.request.sources.empty())))))
            return "New/reset session must be an unbound workspace";
        const auto head=root_.getChildFile("HEAD");
        if(head.exists()) {
            MixerSessionDocument old,checked;
            std::vector<armix::shows::BinaryState> oldStates,checkedStates;
            auto error=load(old,oldStates);
            if(error.isNotEmpty())return error;
            if(reset){
                if(old.id!=doc.id || !old.logicalOnly ||
                   !old.logicalInputs.references().empty() || !old.logicalOutputs.references().empty())
                    return "Reset requires the same unassigned offline session";
                auto before=old,after=doc;
                before.logicalInputs={};before.logicalOutputs={};
                after.logicalInputs={};after.logicalOutputs={};
                if(Codec::encode(before)!=Codec::encode(after))
                    return "Reset would change mixer state outside I/O definitions";
            }else if(!reopen&&old.id==doc.id)return "New session identity must differ";
            const auto archive=archiveDirectory();
            if(archive.createDirectory().failed())return "Cannot create session archive";
            const auto file=archive.getNonexistentChildFile(old.id+"-"+juce::Uuid().toDashedString(),".smix",false);
            error=writeFile(file,old,oldStates);
            if(error.isNotEmpty())return error;
            old.resumeMain.reset(); // portable archive cannot auto-arm
            error=readFile(file,checked,checkedStates);
            if(error.isNotEmpty() || Codec::encode(old)!=Codec::encode(checked))
                return "Previous session archive readback failed";
        }else if(root_.getChildFile("PREVIOUS").exists())
            return "Previous checkpoint requires explicit recovery";
        else if(reset)return "Save an offline session before resetting its I/O";
        return commit(doc,states);
    }

    // A single portable JSON file, with content-addressed plug-in state.
    static juce::String writeFile(const juce::File& file,MixerSessionDocument doc,
                                  const std::vector<armix::shows::BinaryState>& states) {
        doc.resumeMain.reset();
        const auto json=Codec::encode(doc);
        MixerSessionDocument verified;
        if(!Codec::decode(json,verified))return "Cannot encode mixer file";
        auto root=juce::JSON::parse(json);
        auto blobs=juce::var(new juce::DynamicObject());
        std::set<juce::String> hashes,referenced;
        for(const auto& insert:doc.inserts)referenced.insert(insert.sha256);
        juce::int64 total=0;
        for(const auto& state:states) {
            const auto size=static_cast<juce::int64>(state.bytes.getSize());
            total+=size;
            if(!safeHash(state.sha256)||!hashes.insert(state.sha256).second||
               size>maxState||total>maxTotal||
               juce::SHA256(state.bytes).toHexString()!=state.sha256)
                return "Invalid mixer plug-in state";
            blobs.getDynamicObject()->setProperty(state.sha256,
                juce::Base64::toBase64(state.bytes.getData(),state.bytes.getSize()));
        }
        if(hashes!=referenced||hashes.size()>maxStates)
            return "Incomplete mixer plug-in state";
        root.getDynamicObject()->setProperty("pluginState",blobs);
        juce::TemporaryFile staging(file);
        if(!staging.getFile().replaceWithText(juce::JSON::toString(root,false)) ||
           !staging.overwriteTargetFileWithTemporary())
            return "Could not write mixer file";
        return {};
    }

    static juce::String readFile(const juce::File& file,MixerSessionDocument& out,
                                 std::vector<armix::shows::BinaryState>& states) {
        constexpr auto maxFile=maxJson+(maxTotal*4/3)+maxStates*80;
        const auto size=file.getSize();
        if(!file.hasFileExtension(".smix")||!file.existsAsFile()||size<1||size>maxFile)
            return "Mixer file is missing, invalid or too large";
        juce::MemoryBlock contents;
        if(!Input::read(file,maxFile,contents))
            return "Could not read mixer file";
        juce::String text;
        if(!Input::decodeUtf8(contents,text)||!Input::jsonStructure(text))return "Mixer JSON exceeds structural bounds";
        auto root=juce::JSON::parse(text);
        if(!root.isObject())return "Invalid mixer JSON";
        const auto blobs=root.getProperty("pluginState",{});
        if(!blobs.isObject())return "Mixer plug-in state is missing";
        root.getDynamicObject()->removeProperty("pluginState");
        root.getDynamicObject()->removeProperty("resumeMain");
        MixerSessionDocument doc;
        if(!Codec::decode(juce::JSON::toString(root,false),doc))
            return "Invalid mixer session";
        std::set<juce::String> referenced;
        for(const auto& insert:doc.inserts)referenced.insert(insert.sha256);
        const auto& properties=blobs.getDynamicObject()->getProperties();
        if(referenced.size()>maxStates||static_cast<int>(referenced.size())!=properties.size())
            return "Incomplete mixer plug-in state";
        std::vector<armix::shows::BinaryState> verified;
        juce::int64 total=0;
        for(const auto& hash:referenced) {
            const auto state=blobs.getProperty(hash,{});
            if(!safeHash(hash)||!state.isString()||state.toString().length()>((maxState+2)/3)*4)
                return "Invalid mixer plug-in state";
            juce::MemoryOutputStream decoded;
            if(!juce::Base64::convertFromBase64(decoded,state.toString()))
                return "Invalid mixer plug-in state encoding";
            const auto bytes=static_cast<juce::int64>(decoded.getDataSize());
            total+=bytes;
            if(bytes>maxState||total>maxTotal||juce::SHA256(decoded.getMemoryBlock()).toHexString()!=hash)
                return "Mixer plug-in state failed verification";
            verified.push_back({hash,decoded.getMemoryBlock()});
        }
        out=std::move(doc);
        states=std::move(verified);
        return {};
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
    // Presence is distinct from validity; callers must load/recover.
    bool exists() const noexcept {
        return root_.getChildFile("HEAD").exists()||root_.getChildFile("PREVIOUS").exists();
    }
    // Recovery does NOT arm output or publish a graph. Valid HEAD wins.
    juce::String recoverPrevious() const {
        MixerSessionDocument recovered;
        std::vector<armix::shows::BinaryState> states;
        return recoverPrevious(recovered,states);
    }
    juce::String recoverPrevious(MixerSessionDocument& out,
                     std::vector<armix::shows::BinaryState>& states) const {
        const auto head=root_.getChildFile("HEAD");
        const auto before=readPointer(head);
        MixerSessionDocument verified;
        std::vector<armix::shows::BinaryState> binary;
        if(load(verified,binary).isEmpty())
            return "Saved mixer generation is already verified";
        const auto pointer=readPointer(root_.getChildFile("PREVIOUS"));
        if(pointer.isEmpty())return "Last-good mixer pointer missing";
        auto error=readGeneration(root_.getChildFile("versions").getChildFile(pointer),
                                  verified,binary);
        if(error.isNotEmpty())return "Previous mixer generation invalid: "+error;
        if(readPointer(head)!=before)
            return "Mixer HEAD changed during recovery";
        error=writePointer(head,pointer);
        if(error.isNotEmpty())return error;
        if(readPointer(head)!=pointer)
            return "Mixer HEAD changed after recovery";
        // Return the same verified immutable generation that was published.
        // No second blob read or hash pass is needed on the control thread.
        out=std::move(verified);
        states=std::move(binary);
        return {};
    }
private:
    juce::File root_;
    static bool safeHash(const juce::String& hash) {
        return hash.length()==64 && hash.containsOnly("0123456789abcdef");
    }
    static juce::String readPointer(const juce::File& file) {
        if(!file.existsAsFile()||file.getSize()<1||file.getSize()>128)return {};
        juce::String value;
        if(!Input::text(file,128,value))return {};
        const auto name=value.trim();
        return armix::shows::detail::uuid(name)?name:juce::String{};
    }
    static juce::String writePointer(const juce::File& file,const juce::String& id) {
        if(!armix::shows::detail::uuid(id)||file.isSymbolicLink())return "Invalid mixer generation pointer";
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
        const auto versions=generation.getParentDirectory();
        const auto root=versions.getFileName()=="versions"?versions.getParentDirectory():versions;
        if(!Input::managedPath(root,jsonFile)||!Input::managedPath(root,sumFile)||
           !Input::managedPath(root,generation.getChildFile("blobs")))return "Unsafe mixer generation storage path";
        juce::String text,sum;
        if(!Input::text(jsonFile,maxJson,text)||!Input::text(sumFile,64,sum))return "Mixer generation read exceeded bounds";
        const auto expected=sum.trim();
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
            if(!Input::managedPath(root,file))return "Unsafe mixer blob path";
            armix::shows::BinaryState blob;
            blob.sha256=hash;
            if(!Input::read(file,std::min(maxState,maxTotal-total),blob.bytes,true)||
               juce::SHA256(blob.bytes).toHexString()!=hash)
                return "Mixer vendor state changed on disk";
            total+=static_cast<juce::int64>(blob.bytes.getSize());
            binaries.push_back(std::move(blob));
        }
        out=std::move(doc);states=std::move(binaries);
        return {};
    }
};
} // namespace armix::session
