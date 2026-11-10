// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_core/juce_core.h>
#include <algorithm>
#include <functional>
#include <vector>
#if defined(_WIN32)
#include <windows.h>
#include <commctrl.h>
#if defined(_MSC_VER)
#pragma comment(lib, "Comctl32.lib")
#endif
#endif

// Control-thread only. Win32 registers chords; the existing JUCE mixer
// dispatcher performs the action. This class NEVER touches audio callbacks.
namespace armix::hotkey {
struct Mapping final {
    juce::String id, scope, sessionId, channelId, operation;
    int vk=0, modifiers=0;
};
class Registry final {
public:
    explicit Registry(juce::File home) : file_(home.getChildFile("hotkey-registry.v1.json")) {
        load();
    }
    ~Registry() { detach(); }
    Registry(const Registry&)=delete;
    Registry& operator=(const Registry&)=delete;

    static bool valid(const Mapping& m) {
        const bool key=(m.vk>=0x30&&m.vk<=0x39) ||
                       (m.vk>=0x41&&m.vk<=0x5a) ||
                       (m.vk>=0x70&&m.vk<=0x7a);
        const bool chord=(m.modifiers&~7)==0 &&
                         (m.modifiers&3)!=0; // global chords need Ctrl or Alt
        const bool command=m.operation=="PANIC_MUTE" ||
                           m.operation=="SET_MUTE"||m.operation=="SET_SOLO";
        const bool app=m.scope=="APP" && m.operation=="PANIC_MUTE" &&
                       m.channelId.isEmpty() && m.sessionId.isEmpty();
        const bool session=m.scope=="SESSION" && m.operation!="PANIC_MUTE" &&
                           m.channelId.containsOnly("0123456789") &&
                           m.channelId.isNotEmpty() && m.channelId!="0" &&
                           m.sessionId.isNotEmpty() && m.sessionId.length()<=48;
        return m.id.isNotEmpty()&&m.id.length()<=64 && key && chord &&
               command && (app||session);
    }
    static juce::var encode(const Mapping& m) {
        auto out=juce::var(new juce::DynamicObject());
        auto* o=out.getDynamicObject();
        o->setProperty("id",m.id);o->setProperty("scope",m.scope);
        o->setProperty("sessionId",m.sessionId);
        o->setProperty("channelId",m.channelId);
        o->setProperty("operation",m.operation);
        o->setProperty("vk",m.vk);o->setProperty("modifiers",m.modifiers);
        return out;
    }
    static bool decode(const juce::var& v,Mapping& m) {
        if(!v.isObject())return false;
        auto field=[&](const char* name,juce::String& target) {
            const auto value=v.getProperty(name,{});
            if(!value.isString()||value.toString().length()>64)return false;
            target=value.toString();return true;
        };
        const auto vk=v.getProperty("vk",{}),mods=v.getProperty("modifiers",{});
        if(!field("id",m.id)||!field("scope",m.scope)||
           !field("sessionId",m.sessionId)||!field("channelId",m.channelId)||
           !field("operation",m.operation)||!vk.isInt()||!mods.isInt())return false;
        m.vk=static_cast<int>(vk);m.modifiers=static_cast<int>(mods);
        return valid(m);
    }
    void setHandler(std::function<void(const Mapping&)> handler) {
        handler_=std::move(handler);
    }
    juce::String activeSession() const {return activeSession_;}
    void setCaptureMode(bool enabled) {
        if(capturing_==enabled)return;
        capturing_=enabled;
        if(enabled)clearRegistrations();
        else registerActive();
    }
    // Import is explicit only for opening .smix. Implicit restart recovery
    // keeps the newer crash-safe local journal instead of overwriting it with
    // an older checkpoint. An empty imported list intentionally clears keys.
    juce::String activateSession(juce::String id,
                                 const std::vector<Mapping>* imported=nullptr) {
        if(id==activeSession_ && !imported)return {};
        if(imported) {
            auto next=entries_;
            next.erase(std::remove_if(next.begin(),next.end(),
                [&id](const Mapping& m){return m.scope=="SESSION"&&m.sessionId==id;}),
                next.end());
            for(const auto& m:*imported) {
                if(!valid(m)||m.scope!="SESSION"||m.sessionId!=id)
                    return "INVALID_IMPORTED_HOTKEY";
                if(std::any_of(next.begin(),next.end(),
                    [&m](const Mapping& other){return other.id==m.id ||
                        (other.scope=="SESSION"&&other.sessionId==m.sessionId &&
                         other.vk==m.vk&&other.modifiers==m.modifiers);}))
                    return "DUPLICATE_IMPORTED_HOTKEY";
                next.push_back(m);
            }
            if(next.size()>256)return "HOTKEY_LIMIT_REACHED";
            if(!save(next))return "HOTKEY_IMPORT_PERSISTENCE_FAILED";
            entries_=std::move(next);
        }
        clearRegistrations();
        activeSession_=std::move(id);
        registerActive();
        return {};
    }
    std::vector<Mapping> sessionEntries() const {
        std::vector<Mapping> result;
        for(const auto& m:entries_)if(m.scope=="SESSION"&&m.sessionId==activeSession_)
            result.push_back(m);
        return result;
    }
    juce::var snapshot() const {
        auto o=juce::var(new juce::DynamicObject());
        o.getDynamicObject()->setProperty("schema","arsonmix.hotkey-registry.v1");
        o.getDynamicObject()->setProperty("sessionId",activeSession_);
        juce::Array<juce::var> rows;
        for(const auto& m:entries_)if(active(m)) {
            auto row=encode(m);
            row.getDynamicObject()->setProperty("status",
                registered(m.id)?"REGISTERED":
#if defined(_WIN32)
                "UNAVAILABLE");
#else
                "UNSUPPORTED_PLATFORM");
#endif
            rows.add(row);
        }
        o.getDynamicObject()->setProperty("entries",juce::var(rows));
        return o;
    }
    // A refused registration changes neither the OS registration nor disk.
    juce::String upsert(Mapping m) {
        if(!valid(m)|| (m.scope=="SESSION" && m.sessionId!=activeSession_))
            return "INVALID_HOTKEY_TARGET";
        const auto it=std::find_if(entries_.begin(),entries_.end(),
            [&m](const Mapping& item){return item.id==m.id;});
        if(it==entries_.end()&&entries_.size()>=256)return "HOTKEY_LIMIT_REACHED";
        for(const auto& old:entries_) {
            if(old.id!=m.id && active(old) && old.vk==m.vk &&
               old.modifiers==m.modifiers)return "HOTKEY_ALREADY_MAPPED";
        }
        const bool sameKey=it!=entries_.end() && active(*it) &&
                           it->vk==m.vk&&it->modifiers==m.modifiers;
        int staged=0;
        if(!sameKey) {
#if defined(_WIN32)
            if(!window_)return "HOTKEY_NATIVE_WINDOW_NOT_READY";
            staged=++nextId_;
            if(!::RegisterHotKey(window_,staged,
                static_cast<UINT>(m.modifiers)|MOD_NOREPEAT,static_cast<UINT>(m.vk)))
                return "HOTKEY_OS_CONFLICT";
#else
            return "HOTKEY_PLATFORM_UNSUPPORTED";
#endif
        }
        auto next=entries_;
        if(it==entries_.end())next.push_back(m);
        else next[static_cast<std::size_t>(it-entries_.begin())]=m;
        if(!save(next)) {
#if defined(_WIN32)
            if(staged)::UnregisterHotKey(window_,staged);
#endif
            return "HOTKEY_PERSISTENCE_FAILED";
        }
        if(!sameKey) {
            unregisterId(m.id);
            registrations_.push_back({staged,m.id});
        }
        entries_=std::move(next);
        return {};
    }
    juce::String remove(const juce::String& id) {
        auto next=entries_;
        auto it=std::find_if(next.begin(),next.end(),
            [&id](const Mapping& m){return m.id==id;});
        if(it==next.end()||!active(*it))return "UNKNOWN_HOTKEY_MAPPING";
        next.erase(it);
        if(!save(next))return "HOTKEY_PERSISTENCE_FAILED";
        unregisterId(id);
        entries_=std::move(next);
        return {};
    }
    bool attached() const noexcept {
#if defined(_WIN32)
        return window_!=nullptr;
#else
        return false;
#endif
    }
    void attach(void* nativeWindow) {
#if defined(_WIN32)
        auto* newWindow=static_cast<HWND>(nativeWindow);
        if(newWindow==window_)return;
        detach();
        if(!newWindow||!::IsWindow(newWindow))return;
        if(!::SetWindowSubclass(newWindow,&Registry::windowProc,0xA551,
                                reinterpret_cast<DWORD_PTR>(this)))return;
        window_=newWindow;
        registerActive();
#else
        (void)nativeWindow;
#endif
    }
    void detach() {
        clearRegistrations();
#if defined(_WIN32)
        if(window_)::RemoveWindowSubclass(window_,&Registry::windowProc,0xA551);
        window_=nullptr;
#endif
    }
private:
    struct Registration {int id;juce::String mapping;};
    bool active(const Mapping& m) const {
        return m.scope=="APP" ||
               (m.scope=="SESSION" && m.sessionId==activeSession_);
    }
    bool registered(const juce::String& id) const {
        return std::any_of(registrations_.begin(),registrations_.end(),
            [&id](const Registration& r){return r.mapping==id;});
    }
    void unregisterId(const juce::String& mapping) {
        auto it=registrations_.begin();
        while(it!=registrations_.end()) {
            if(it->mapping==mapping) {
#if defined(_WIN32)
                if(window_)::UnregisterHotKey(window_,it->id);
#endif
                it=registrations_.erase(it);
            } else ++it;
        }
    }
    void clearRegistrations() {
#if defined(_WIN32)
        if(window_)for(const auto& r:registrations_)::UnregisterHotKey(window_,r.id);
#endif
        registrations_.clear();
    }
    void registerActive() {
#if defined(_WIN32)
        if(!window_ || capturing_)return;
        for(const auto& m:entries_)if(active(m) && !registered(m.id)) {
            const int id=++nextId_;
            if(::RegisterHotKey(window_,id,
                static_cast<UINT>(m.modifiers)|MOD_NOREPEAT,static_cast<UINT>(m.vk)))
                registrations_.push_back({id,m.id});
        }
#endif
    }
    bool save(const std::vector<Mapping>& entries) const {
        auto o=juce::var(new juce::DynamicObject());
        o.getDynamicObject()->setProperty("schema","arsonmix.hotkey-registry.v1");
        juce::Array<juce::var> rows;
        for(const auto& m:entries)rows.add(encode(m));
        o.getDynamicObject()->setProperty("entries",juce::var(rows));
        if(file_.getParentDirectory().createDirectory().failed())return false;
        juce::TemporaryFile tmp(file_);
        return tmp.getFile().replaceWithText(juce::JSON::toString(o,false)) &&
               tmp.overwriteTargetFileWithTemporary();
    }
    void load() {
        if(!file_.existsAsFile())return;
        const auto content=file_.loadFileAsString();
        if(content.getNumBytesAsUTF8()>128*1024)return;
        const auto doc=juce::JSON::parse(content);
        if(doc.getProperty("schema","").toString()!="arsonmix.hotkey-registry.v1")return;
        const auto entriesValue=doc.getProperty("entries",{});
        const auto* rows=entriesValue.getArray();
        if(!rows || rows->size()>256)return;
        std::vector<Mapping> read;
        for(const auto& row:*rows) {
            Mapping m;
            if(!decode(row,m))return;
            if(std::any_of(read.begin(),read.end(),[&m](const Mapping& other) {
                return other.id==m.id ||
                    (other.scope==m.scope && other.sessionId==m.sessionId &&
                     other.vk==m.vk && other.modifiers==m.modifiers);
            }))return;
            read.push_back(std::move(m));
        }
        entries_=std::move(read);
    }
#if defined(_WIN32)
    static LRESULT CALLBACK windowProc(HWND hwnd,UINT msg,WPARAM wp,LPARAM lp,
                                       UINT_PTR subclass,DWORD_PTR data) {
        auto* self=reinterpret_cast<Registry*>(data);
        if(msg==WM_HOTKEY && self) {
            const int keyId=static_cast<int>(wp);
            for(const auto& r:self->registrations_)if(r.id==keyId) {
                const auto it=std::find_if(self->entries_.begin(),self->entries_.end(),
                    [&r](const Mapping& m){return m.id==r.mapping;});
                if(it!=self->entries_.end() && self->active(*it)) {
                    if(self->handler_)self->handler_(*it);
                    return 0;
                }
            }
        }
        return ::DefSubclassProc(hwnd,msg,wp,lp);
    }
    HWND window_=nullptr;
#endif
    juce::File file_;
    juce::String activeSession_;
    std::vector<Mapping> entries_;
    std::vector<Registration> registrations_;
    std::function<void(const Mapping&)> handler_;
    int nextId_=0x7000;
    bool capturing_=false;
};
} // namespace armix::hotkey
