// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "NativeShowSchema.h"
#include <juce_cryptography/juce_cryptography.h>
#include <vector>

// P4 stage 2: CONTROL / I/O thread only. One immutable generation is prepared
// and verified before a single small HEAD file is atomically replaced. The
// audio callback NEVER sees this store. No device or VST3 is instantiated here.
namespace armix::shows {
struct BinaryState final {
    juce::String sha256;
    juce::MemoryBlock bytes; // caller captures vendor state on its valid thread
};
class NativeShowStore final {
public:
    static constexpr int maxSnapshotsPerCommit = 64;
    static constexpr int maxBlobsPerCommit = 128;
    static constexpr juce::int64 maxBlobBytes = 8 * 1024 * 1024;
    static constexpr juce::int64 maxGenerationBytes = 128 * 1024 * 1024;
    explicit NativeShowStore(juce::File studio) : root_(studio.getChildFile("Shows")) {}

    // Success is an empty string. Never change existing HEAD on any rejection.
    juce::String commit(const ShowV2& show,
                        const std::vector<SnapshotV2>& snapshots,
                        const std::vector<BinaryState>& blobs) const {
        if (!detail::uuid(show.id) ||
            snapshots.size()>maxSnapshotsPerCommit ||
            snapshots.size()!=show.snapshotOrder.size() ||
            blobs.size()>maxBlobsPerCommit)
            return "Invalid Show identity or oversized generation";
        ShowV2 checkedShow;
        const auto showJson=Codec::encode(show);
        if(!Codec::decode(showJson,checkedShow)) return "Invalid ShowV2 metadata";
        std::vector<juce::String> payloads;
        payloads.reserve(snapshots.size());
        for(size_t i=0;i<snapshots.size();++i) {
            SnapshotV2 checked;
            const auto& item=snapshots[i];
            const auto json=Codec::encode(item);
            if(!Codec::decode(json,checked) ||
               !checked.id.equalsIgnoreCase(show.snapshotOrder[i]))
                return "Snapshot IDs/order or schema invalid";
            payloads.push_back(json);
        }
        juce::int64 total=showJson.getNumBytesAsUTF8();
        for(const auto& json:payloads)total+=json.getNumBytesAsUTF8();
        for(const auto& blob:blobs) {
            if(!validHash(blob.sha256) ||
               blob.bytes.getSize()>static_cast<size_t>(maxBlobBytes) ||
               juce::SHA256(blob.bytes).toHexString()!=blob.sha256.toLowerCase())
                return "Vendor binary state hash or size invalid";
            total+=static_cast<juce::int64>(blob.bytes.getSize());
            if(total>maxGenerationBytes) return "Generation exceeds 128 MB limit";
        }
        // Every referenced VST3 state must be provided, no phantom vendor state.
        for(const auto& snap:snapshots)
            for(const auto& strip:snap.strips)
                for(const auto& fx:strip.fx) {
                    bool found=false;
                    for(const auto& blob:blobs)
                        if(fx.stateSha256.equalsIgnoreCase(blob.sha256)) {found=true;break;}
                    if(!found) return "Missing vendor state for "+fx.catalogId;
                }
        if(!root_.isDirectory() && root_.createDirectory().failed())
            return "Studio Shows directory is not writable";
        const auto showRoot=root_.getChildFile(show.id.toLowerCase());
        const auto versions=showRoot.getChildFile("versions");
        if(!versions.isDirectory()&&versions.createDirectory().failed())
            return "Show version directory is not writable";
        const auto generation=juce::Uuid().toString().toLowerCase();
        if(!detail::uuid(generation)) return "Could not create generation ID";
        const auto stage=showRoot.getChildFile(".staging-"+generation);
        if(stage.exists()) return "Staging generation already exists";
        if(stage.createDirectory().failed()) return "Cannot stage Show generation";
        struct Cleanup final {
            juce::File dir;
            ~Cleanup(){if(dir.exists())dir.deleteRecursively();}
        } cleanup{stage};
        const auto snaps=stage.getChildFile("snapshots");
        const auto binary=stage.getChildFile("blobs");
        if(snaps.createDirectory().failed()||binary.createDirectory().failed())
            return "Cannot stage Show resources";
        if(!stage.getChildFile("show.json").replaceWithText(showJson))
            return "Cannot stage Show header";
        for(size_t i=0;i<snapshots.size();++i)
            if(!snaps.getChildFile(snapshots[i].id.toLowerCase()+".json")
                    .replaceWithText(payloads[i]))
                return "Cannot stage snapshot JSON";
        for(const auto& blob:blobs) {
            const auto path=binary.getChildFile(blob.sha256.toLowerCase()+".bin");
            if(!path.replaceWithData(blob.bytes.getData(),blob.bytes.getSize()) ||
               path.getSize()!=static_cast<juce::int64>(blob.bytes.getSize()) ||
               !juce::SHA256(path).toHexString().equalsIgnoreCase(blob.sha256))
                return "Failed to stage and verify vendor binary";
        }
        std::vector<SnapshotV2> verified;
        ShowV2 verifiedShow;
        const auto verifyError=readFolder(stage,verifiedShow,verified);
        if(verifyError.isNotEmpty() || verifiedShow.id!=show.id ||
           verified.size()!=snapshots.size()) return "Staged generation verification failed";
        // Directory rename does not replace an older version; the current
        // generation changes ONLY when HEAD pointer commit succeeds.
        const auto committed=versions.getChildFile(generation);
        if(committed.exists()||!stage.moveFileTo(committed))
            return "Unable to publish immutable generation directory";
        // A failed HEAD write leaves previous generation intact; a new version
        // may remain orphaned but will never be loaded without its HEAD.
        juce::TemporaryFile temp(showRoot.getChildFile("HEAD"));
        auto stream=temp.getFile().createOutputStream();
        if(!stream) return "Cannot open temporary Show pointer";
        if(!stream->writeText(generation+"\n",false,false,"\n"))
            return "Cannot write temporary Show pointer";
        stream->flush();
        if(stream->getStatus().failed())return "Failed to flush Show pointer";
        stream.reset();
        if(!temp.overwriteTargetFileWithTemporary())
            return "Cannot atomically commit Show pointer";
        return {};
    }

    // Read-and-verify entire generation before changing either output value.
    juce::String load(const juce::String& requestedId,ShowV2& show,
                      std::vector<SnapshotV2>& snapshots) const {
        if(!detail::uuid(requestedId))return "Invalid Show ID";
        const auto dir=root_.getChildFile(requestedId.toLowerCase());
        const auto head=dir.getChildFile("HEAD");
        if(!head.existsAsFile() || head.getSize()>128) return "Show pointer missing";
        const auto generation=head.loadFileAsString().trim();
        if(!detail::uuid(generation))return "Invalid Show pointer";
        ShowV2 candidate;
        std::vector<SnapshotV2> items;
        const auto error=readFolder(dir.getChildFile("versions").getChildFile(generation),
                                    candidate,items);
        if(error.isNotEmpty())return error;
        if(!candidate.id.equalsIgnoreCase(requestedId))return "Show identity mismatch";
        show=std::move(candidate);
        snapshots=std::move(items);
        return {};
    }

    juce::File currentGeneration(const juce::String& showId) const {
        if(!detail::uuid(showId))return {};
        const auto base=root_.getChildFile(showId.toLowerCase());
        const auto head=base.getChildFile("HEAD");
        if(!head.existsAsFile()||head.getSize()>128)return {};
        const auto id=head.loadFileAsString().trim();
        return detail::uuid(id)?base.getChildFile("versions").getChildFile(id):juce::File{};
    }
private:
    juce::File root_;
    static bool validHash(const juce::String& s) {
        if(s.length()!=64) return false;
        for(const auto ch:s.toStdString())
            if(!((ch>='0'&&ch<='9')||(ch>='a'&&ch<='f')||
                 (ch>='A'&&ch<='F')))return false;
        return true;
    }
    static juce::String readFolder(const juce::File& dir,ShowV2& show,
                                  std::vector<SnapshotV2>& out) {
        const auto header=dir.getChildFile("show.json");
        if(!header.existsAsFile()||header.getSize()>maxJsonBytes)return "Show JSON missing/oversized";
        ShowV2 tmp;
        if(!Codec::decode(header.loadFileAsString(),tmp) ||
           tmp.snapshotOrder.size()>maxSnapshotsPerCommit)return "Invalid Show JSON";
        std::vector<SnapshotV2> candidate;
        juce::int64 total=header.getSize();
        for(const auto& id:tmp.snapshotOrder) {
            const auto file=dir.getChildFile("snapshots").getChildFile(id.toLowerCase()+".json");
            if(!file.existsAsFile()||file.getSize()>maxJsonBytes)return "Snapshot JSON missing/oversized";
            SnapshotV2 snap;
            if(!Codec::decode(file.loadFileAsString(),snap) ||
               !snap.id.equalsIgnoreCase(id))return "Invalid snapshot metadata";
            total+=file.getSize();
            if(total>maxGenerationBytes)return "Generation too large";
            for(const auto& strip:snap.strips)
                for(const auto& fx:strip.fx) {
                    if(!validHash(fx.stateSha256))return "Unsafe state hash";
                    const auto file=dir.getChildFile("blobs").getChildFile(fx.stateSha256.toLowerCase()+".bin");
                    if(!file.existsAsFile()||file.getSize()>maxBlobBytes)
                        return "Missing/oversized vendor state";
                    if(!juce::SHA256(file).toHexString().equalsIgnoreCase(fx.stateSha256))
                        return "Vendor binary hash mismatch";
                }
            candidate.push_back(std::move(snap));
        }
        show=std::move(tmp);out=std::move(candidate);return {};
    }
};
} // namespace armix::shows
