// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "PlayerSetlistArchive.h"
#include <juce_core/juce_core.h>
#include <memory>
#include <map>
#include <vector>

// N-MOD-18: one read-only Player projection, CONTROL/UI thread only.
// Not an accepted PCM/deck model, transport, or another persistence owner.
// The one ConsoleWindow emits the returned v1 event to the approved GUI.
namespace armix::player {
class PlayerStateProjection final {
public:
    struct Source {
        const std::vector<juce::File>& files;
        const std::vector<PlayerTrackSettings>& controls;
        const std::vector<PlayerSavedSet>& sets;
        const std::vector<juce::File>& queue;
        juce::var transport;
        juce::String error;
        int selected=-1;
        int current=-1;
        int active=0;
        int pending=-1;
        int queueRevision=0;
        int crossfadeMs=180;
        int undoSet=-1;
        int undoIndex=-1;
        bool autoNext=false;
        bool loopAB=false;
        bool libraryBusy=false;
        const std::map<juce::File,int>* catalogueIds=nullptr;
        bool mediaRefreshQueued=false;
    };
    static juce::var build(const Source& s,bool includeLibrary,
                           bool includeTracks) {
        // Refuse to project a corrupt song/control pair as an accepted
        // snapshot. The durable repository rejects it for the same reason.
        if(s.files.size()!=s.controls.size())return {};
        auto obj=std::make_unique<juce::DynamicObject>();
        obj->setProperty("schema","arsonmix.player.v1");
        obj->setProperty("transport",s.transport);
        obj->setProperty("selected",s.selected);
        obj->setProperty("current",s.current);
        obj->setProperty("autoNext",s.autoNext);
        obj->setProperty("loopAB",s.loopAB);
        obj->setProperty("activeSetlist",s.active);
        obj->setProperty("pending",s.pending);
        obj->setProperty("queueRevision",s.queueRevision);
        obj->setProperty("crossfadeMs",s.crossfadeMs);
        obj->setProperty("canUndoRemove",
            PlayerSetlistArchive::canUndoRemove(
                s.active,s.undoSet,s.undoIndex,s.files.size(),s.controls.size()));
        obj->setProperty("libraryBusy",s.libraryBusy);
        obj->setProperty("mediaRefreshQueued",s.mediaRefreshQueued);
        obj->setProperty("error",s.error);
        obj->setProperty("chooseOutput",
            s.error.containsIgnoreCase("Choose a verified MAIN output") ||
            s.error.containsIgnoreCase("native mixer is preparing"));
        if(includeLibrary) {
            juce::Array<juce::var> sets;
            for(std::size_t i=0;i<s.sets.size();++i) {
                auto item=std::make_unique<juce::DynamicObject>();
                item->setProperty("id",static_cast<int>(i));
                item->setProperty("name",s.sets[i].name);
                item->setProperty("count",static_cast<int>(
                    static_cast<int>(i)==s.active?s.files.size():
                        s.sets[i].files.size()));
                sets.add(juce::var(item.release()));
            }
            obj->setProperty("setlists",juce::var(sets));
            juce::Array<juce::var> queue;
            for(std::size_t i=0;i<s.queue.size();++i) {
                auto item=std::make_unique<juce::DynamicObject>();
                item->setProperty("id",static_cast<int>(i));
                item->setProperty("title",s.queue[i].getFileNameWithoutExtension());
                queue.add(juce::var(item.release()));
            }
            obj->setProperty("queue",juce::var(queue));
            if(includeTracks) {
                juce::Array<juce::var> rows;
                for(std::size_t i=0;i<s.files.size();++i) {
                    auto item=std::make_unique<juce::DynamicObject>();
                    item->setProperty("id",static_cast<int>(i));
                    if(s.catalogueIds){const auto it=s.catalogueIds->find(s.files[i]);if(it!=s.catalogueIds->end())item->setProperty("catalogId",it->second);}
                    item->setProperty("title",
                        s.files[i].getFileNameWithoutExtension());
                    item->setProperty("missing",!s.files[i].existsAsFile());
                    item->setProperty("keySemitones",s.controls[i].key);
                    item->setProperty("tempoPercent",s.controls[i].tempo);
                    item->setProperty("autoAfter",s.controls[i].autoAfter);
                    item->setProperty("cueIn",s.controls[i].cueIn);
                    item->setProperty("cueOut",s.controls[i].cueOut);
                    rows.add(juce::var(item.release()));
                }
                obj->setProperty("tracks",juce::var(rows));
            }
        }
        return juce::var(obj.release());
    }
};
} // namespace armix::player
