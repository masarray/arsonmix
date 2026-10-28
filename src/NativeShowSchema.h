// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <vector>

// P4 stage 1: inert, CONTROL-thread-only wire schema. No file I/O, device
// activation, WebView bridge or audio callback reads. An encoded record is NOT
// evidence that a VST was captured or that native recall is implemented.
namespace armix::shows {

constexpr int channels = 10;
constexpr int maxFxPerStrip = 8;
constexpr int maxSendsPerSnapshot = 32;
constexpr int maxSnapshotsPerShow = 256;
constexpr int maxJsonBytes = 512 * 1024;

struct FxRef final {
    juce::String catalogId;  // trusted catalog identity, resolved at preflight
    int uid = 0;
    juce::String stateSha256; // external, separately verified immutable blob
    bool bypass = false;
};
struct Send final {
    int destination = 0; // stable zero-based strip index; never GUI row order
    bool preFader = false;
    double gain = 0.0, pan = 0.0;
};
struct Strip final {
    int source = 0;       // armix::mixer::SourceMode: disconnected/mono/stereo
    int inputA = -1, inputB = -1; // DEVICE-LOCAL physical indexes; never auto-applied
    double fader = 1.0, pan = 0.0; // DSP linear gain, not GUI dB
    bool mute = false, solo = false, toMaster = true;
    std::vector<FxRef> fx;
    std::vector<Send> sends;
};
struct SnapshotV2 final {
    juce::String id, name, sourceBuild;
    juce::int64 createdMs = 0, modifiedMs = 0;
    std::array<Strip, channels> strips{};
    double masterGain = 1.0, masterPan = 0.0;
    bool masterMute = false, masterMono = false;
};
struct ShowV2 final {
    juce::String id, name, currentSnapshotId;
    juce::int64 createdMs = 0, modifiedMs = 0;
    std::array<bool, channels> channelSafes{};
    std::vector<juce::String> snapshotOrder; // metadata only; snapshots in separate files
};

namespace detail {
inline juce::var object() { return juce::var (new juce::DynamicObject()); }
inline void put(juce::var& obj, const char* key, juce::var v) {
    obj.getDynamicObject()->setProperty(juce::Identifier(key), std::move(v));
}
inline juce::var get(const juce::var& obj, const char* key) {
    return obj.getProperty(juce::Identifier(key), {});
}
inline bool integer(const juce::var& v, int& out, int lo, int hi) {
    if (!v.isInt()) return false;
    const auto n = static_cast<int>(v);
    if(n < lo || n > hi) return false;
    out=n; return true;
}
inline bool timestamp(const juce::var& v, juce::int64& out) {
    if (!v.isInt() && !v.isInt64()) return false;
    const auto n = static_cast<juce::int64>(v);
    if(n <= 0 || n > 9000000000000000LL) return false;
    out=n; return true;
}
inline bool real(const juce::var& v, double& out, double lo, double hi) {
    if (!v.isDouble() && !v.isInt() && !v.isInt64()) return false;
    const auto n=static_cast<double>(v);
    if(!std::isfinite(n) || n<lo || n>hi) return false;
    out=n; return true;
}
inline bool flag(const juce::var& v, bool& out) {
    if(!v.isBool()) return false;
    out=static_cast<bool>(v); return true;
}
inline bool str(const juce::var& v, juce::String& out, int limit, bool required=true) {
    if(!v.isString()) return false;
    auto s=v.toString();
    if(s.length()>limit || (required && s.isEmpty())) return false;
    out=std::move(s); return true;
}
inline bool uuid(const juce::String& s) {
    if(s.length()!=36) return false;
    for(int i=0;i<36;++i) {
        const auto ch=s[i];
        if(i==8||i==13||i==18||i==23) { if(ch!='-') return false; }
        else if(!((ch>='0'&&ch<='9')||(ch>='a'&&ch<='f')||(ch>='A'&&ch<='F'))) return false;
    }
    return true;
}
inline bool name(const juce::String& n) {
    if(n.isEmpty() || n.length()>64 || n.trim()!=n) return false;
    for(auto p=n.getCharPointer(); !p.isEmpty(); ++p) {
        const auto c=*p;
        if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||
             (c>='0'&&c<='9')||c==' '||c=='_'||c=='-')) return false;
    }
    return true;
}
inline bool common(const juce::var& v, juce::String& id, juce::String& label,
                   juce::int64& created, juce::int64& modified) {
    return v.isObject() && str(get(v,"id"),id,36) && uuid(id) &&
        str(get(v,"name"),label,64) && name(label) &&
        timestamp(get(v,"createdMs"),created) &&
        timestamp(get(v,"modifiedMs"),modified) && modified>=created;
}
inline void commonWrite(juce::var& v, const juce::String& id,
                        const juce::String& name,juce::int64 created,juce::int64 modified) {
    put(v,"schemaVersion",2);
    put(v,"id",id);put(v,"name",name);
    put(v,"createdMs",created);put(v,"modifiedMs",modified);
}
inline juce::String encode(const juce::var& v) {
    return juce::JSON::toString(v, true);
}
inline bool header(const juce::var& v,const char* format) {
    int schema=0; juce::String kind;
    return v.isObject() && integer(get(v,"schemaVersion"),schema,2,2) &&
        str(get(v,"format"),kind,64) && kind==format;
}
} // namespace detail

class Codec final {
public:
    static juce::String encode(const ShowV2& s) {
        auto v=detail::object();
        detail::commonWrite(v,s.id,s.name,s.createdMs,s.modifiedMs);
        detail::put(v,"format","arsonmix.native.show");
        detail::put(v,"currentSnapshotId",s.currentSnapshotId);
        juce::Array<juce::var> safes,order;
        for(bool safe:s.channelSafes) safes.add(safe);
        for(const auto& id:s.snapshotOrder) order.add(id);
        detail::put(v,"channelSafes",juce::var(safes));
        detail::put(v,"snapshotOrder",juce::var(order));
        return detail::encode(v);
    }
    static juce::String encode(const SnapshotV2& s) {
        auto v=detail::object();
        detail::commonWrite(v,s.id,s.name,s.createdMs,s.modifiedMs);
        detail::put(v,"format","arsonmix.native.snapshot");
        detail::put(v,"sourceBuild",s.sourceBuild);
        auto master=detail::object();
        detail::put(master,"gain",s.masterGain);detail::put(master,"pan",s.masterPan);
        detail::put(master,"mute",s.masterMute);detail::put(master,"mono",s.masterMono);
        detail::put(v,"master",master);
        juce::Array<juce::var> strips;
        for(int i=0;i<channels;++i) {
            const auto& ch=s.strips[static_cast<size_t>(i)];
            auto c=detail::object();
            detail::put(c,"id","ch-"+juce::String(i+1).paddedLeft('0',3));
            detail::put(c,"source",ch.source);
            detail::put(c,"inputA",ch.inputA);detail::put(c,"inputB",ch.inputB);
            detail::put(c,"fader",ch.fader);detail::put(c,"pan",ch.pan);
            detail::put(c,"mute",ch.mute);detail::put(c,"solo",ch.solo);
            detail::put(c,"toMaster",ch.toMaster);
            juce::Array<juce::var> fx, sends;
            for(const auto& f:ch.fx) {
                auto p=detail::object();
                detail::put(p,"catalogId",f.catalogId);detail::put(p,"uid",f.uid);
                detail::put(p,"stateSha256",f.stateSha256);detail::put(p,"bypass",f.bypass);
                fx.add(p);
            }
            for(const auto& se:ch.sends) {
                auto p=detail::object();
                detail::put(p,"destination",se.destination);
                detail::put(p,"preFader",se.preFader);
                detail::put(p,"gain",se.gain);detail::put(p,"pan",se.pan);
                sends.add(p);
            }
            detail::put(c,"fx",juce::var(fx));detail::put(c,"sends",juce::var(sends));
            strips.add(c);
        }
        detail::put(v,"strips",juce::var(strips));
        return detail::encode(v);
    }
    // Decode is transactional: an invalid record never mutates the caller's
    // previously accepted record. Capability/plugin/device preflight is later.
    static bool decode(const juce::String& json,ShowV2& out) {
        if(json.getNumBytesAsUTF8()>maxJsonBytes) return false;
        const auto v=juce::JSON::parse(json);
        if(!detail::header(v,"arsonmix.native.show")) return false;
        ShowV2 next;
        if(!detail::common(v,next.id,next.name,next.createdMs,next.modifiedMs) ||
           !detail::str(detail::get(v,"currentSnapshotId"),next.currentSnapshotId,36,false) ||
           (!next.currentSnapshotId.isEmpty() && !detail::uuid(next.currentSnapshotId)))
            return false;
        const auto* order=detail::get(v,"snapshotOrder").getArray();
        const auto* safes=detail::get(v,"channelSafes").getArray();
        // getArray pointers above must not outlive their containing var.
        return decodeShowArrays(v,next,out);
    }
    static bool decode(const juce::String& json,SnapshotV2& out) {
        if(json.getNumBytesAsUTF8()>maxJsonBytes) return false;
        const auto v=juce::JSON::parse(json);
        if(!detail::header(v,"arsonmix.native.snapshot")) return false;
        SnapshotV2 next;
        if(!detail::common(v,next.id,next.name,next.createdMs,next.modifiedMs) ||
           !detail::str(detail::get(v,"sourceBuild"),next.sourceBuild,80,false))
            return false;
        const auto master=detail::get(v,"master");
        if(!master.isObject() ||
           !detail::real(detail::get(master,"gain"),next.masterGain,0.0,4.0) ||
           !detail::real(detail::get(master,"pan"),next.masterPan,-1.0,1.0) ||
           !detail::flag(detail::get(master,"mute"),next.masterMute) ||
           !detail::flag(detail::get(master,"mono"),next.masterMono)) return false;
        const auto list=detail::get(v,"strips");
        const auto* channelsArray=list.getArray();
        if(!channelsArray || channelsArray->size()!=channels) return false;
        int totalSends=0;
        for(int i=0;i<channels;++i) {
            auto& ch=next.strips[static_cast<size_t>(i)];
            const auto& rec=channelsArray->getReference(i);
            juce::String id;
            if(!rec.isObject() ||
               !detail::str(detail::get(rec,"id"),id,6) ||
               id!="ch-"+juce::String(i+1).paddedLeft('0',3) ||
               !detail::integer(detail::get(rec,"source"),ch.source,0,2) ||
               !detail::integer(detail::get(rec,"inputA"),ch.inputA,-1,255) ||
               !detail::integer(detail::get(rec,"inputB"),ch.inputB,-1,255) ||
               !detail::real(detail::get(rec,"fader"),ch.fader,0.0,4.0) ||
               !detail::real(detail::get(rec,"pan"),ch.pan,-1.0,1.0) ||
               !detail::flag(detail::get(rec,"mute"),ch.mute) ||
               !detail::flag(detail::get(rec,"solo"),ch.solo) ||
               !detail::flag(detail::get(rec,"toMaster"),ch.toMaster))
                return false;
            if((ch.source==0 && (ch.inputA!=-1||ch.inputB!=-1)) ||
               (ch.source==1 && (ch.inputA<0||ch.inputB!=-1)) ||
               (ch.source==2 && (ch.inputA<0||ch.inputB<0||ch.inputA==ch.inputB)))
                return false;
            const auto fxv=detail::get(rec,"fx"), sv=detail::get(rec,"sends");
            const auto* fx=fxv.getArray();
            const auto* sends=sv.getArray();
            if(!fx||!sends||fx->size()>maxFxPerStrip ||
               (totalSends+=sends->size())>maxSendsPerSnapshot) return false;
            for(const auto& p:*fx) {
                FxRef f;
                if(!p.isObject() ||
                   !detail::str(detail::get(p,"catalogId"),f.catalogId,512) ||
                   !detail::integer(detail::get(p,"uid"),f.uid,-2147483647,2147483647) ||
                   !detail::str(detail::get(p,"stateSha256"),f.stateSha256,64) ||
                   !detail::flag(detail::get(p,"bypass"),f.bypass) ||
                   f.stateSha256.length()!=64) return false;
                for(auto chHex:f.stateSha256.toStdString())
                    if(!((chHex>='a'&&chHex<='f')||(chHex>='A'&&chHex<='F')||
                         (chHex>='0'&&chHex<='9'))) return false;
                ch.fx.push_back(std::move(f));
            }
            for(const auto& p:*sends) {
                Send send;
                if(!p.isObject() ||
                   !detail::integer(detail::get(p,"destination"),send.destination,0,channels-1) ||
                   send.destination==i ||
                   !detail::flag(detail::get(p,"preFader"),send.preFader) ||
                   !detail::real(detail::get(p,"gain"),send.gain,0.0,4.0) ||
                   !detail::real(detail::get(p,"pan"),send.pan,-1.0,1.0))
                    return false;
                ch.sends.push_back(send);
            }
        }
        out=std::move(next); return true;
    }
private:
    static bool decodeShowArrays(const juce::var& v,ShowV2& next,ShowV2& out) {
        const auto orderV=detail::get(v,"snapshotOrder");
        const auto safesV=detail::get(v,"channelSafes");
        const auto* order=orderV.getArray(), *safes=safesV.getArray();
        if(!order||!safes||safes->size()!=channels||
           order->size()>maxSnapshotsPerShow) return false;
        for(int i=0;i<channels;++i)
            if(!detail::flag(safes->getReference(i),next.channelSafes[static_cast<size_t>(i)]))
                return false;
        for(const auto& item:*order) {
            juce::String id;
            if(!detail::str(item,id,36)||!detail::uuid(id)) return false;
            for(const auto& previous:next.snapshotOrder)
                if(previous.equalsIgnoreCase(id)) return false;
            next.snapshotOrder.push_back(id);
        }
        if(!next.currentSnapshotId.isEmpty()) {
            bool found=false;
            for(const auto& id:next.snapshotOrder)
                if(id.equalsIgnoreCase(next.currentSnapshotId)) {found=true;break;}
            if(!found) return false;
        }
        out=std::move(next); return true;
    }
};
} // namespace armix::shows
