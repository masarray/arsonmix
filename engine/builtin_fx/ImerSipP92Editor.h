// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Compact ImmerSIB concert scene + native JUCE control panel.
// Scene photography is image-backed; all control surfaces and icons are JUCE.
// Audio uses the existing SonKu PCM, host bypass, ACK, and IMRS state paths.
#include <BinaryData.h>
#include <array>
#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <utility>
#include "ImerSipP92VisualSkin.h"

namespace armix::builtin_fx {

class ImerSipP92Editor final : public juce::AudioProcessorEditor,
                              private juce::Timer {
public:
    // Snapshot of actual host PCM control gates, never a GUI-local power flag.
    // Slot preference is independent of the additive rack/global gates.
    struct HostBypassState final {
        bool valid=false;
        bool slotBypassed=false;
        bool rackBypassed=false;
        bool globalBypassed=false;
        bool effectiveBypassed() const noexcept {
            return valid && (slotBypassed || rackBypassed || globalBypassed);
        }
    };
    explicit ImerSipP92Editor(ImerSipP92Processor& processor)
        :juce::AudioProcessorEditor(processor),fx_(processor) {
        // Compact 85% presentation of the audited 1100x680 master canvas.
        // The host sees an actual 935x578 editor rather than an oversized FX.
        setSize(935,578);
        setResizable(false,false);
        preset_.setComponentID("imersip.preset");
        preset_.addItem("Custom",1);
        for(const auto& scene:imersipP92Presets)
            preset_.addItem(juce::String(scene.label),scene.id);
        preset_.setLookAndFeel(&skin_);
        preset_.onChange=[this]{
            const int id=preset_.getSelectedId();
            if(!imersipP92PresetById(id))return;
            // A named preset queues its exact factory scene, never a
            // rounded slider snapshot (e.g. 1.8 linear -> displayed +5.1dB).
            const auto factory=imersipP92Factory(id);
            isCustomIntent_=false;
            visualSceneId_=id;
            sync(factory);
            preset_.setSelectedId(id,juce::dontSendNotification);
            submit(factory);
        };
        addAndMakeVisible(preset_);
        room_.setComponentID("imersip.room");
        room_.addItem("ROOM OFF",1);
        room_.addItem("STUDIO 7.2 M",2);
        room_.addItem("CHAMBER 14.4 M",3);
        room_.addItem("LARGE 28.8 M",4);
        room_.setLookAndFeel(&skin_);
        room_.onChange=[this]{
            uiScene_.spacePreset=std::clamp(room_.getSelectedId()-1,0,3);
            isCustomIntent_=true;
            preset_.setSelectedId(1,juce::dontSendNotification);
            submit(uiScene_);
        };
        addAndMakeVisible(room_);

        const char* ids[]={"stage","distance","early","late",
                          "mix","gain","align","yaw"};
        const char* names[]={"STAGE WIDTH","DISTANCE","EARLY ROOM",
                             "LATE ROOM","SPATIAL MIX","MASTER GAIN",
                             "EAR ALIGN","HEAD YAW"};
        const double lower[]={30.,imersipP92MinDistance(),0.,0.,0.,-12.,-30.,-90.};
        #if defined(SONKU_P98_SCENE_VOICING_RESEARCH) && SONKU_P98_SCENE_VOICING_RESEARCH
        const double upper[]={180.,2.5,180.,100.,100.,6.,20.,90.};
#else
        const double upper[]={180.,7.4,100.,100.,100.,6.,20.,90.};
#endif
        const double step[]={1.,.1,1.,1.,1.,.1,1.,1.};
        for(int i=0;i<8;++i){
            auto& slider=knobs_[static_cast<std::size_t>(i)];
            slider.setComponentID(juce::String("imersip.")+ids[i]);
            slider.setSliderStyle(i==5?
                juce::Slider::RotaryHorizontalVerticalDrag:
                juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle(juce::Slider::NoTextBox,false,0,0);
            slider.setRange(lower[i],upper[i],step[i]);
            slider.setLookAndFeel(&skin_);
            slider.onValueChange=[this,i]{
                applySliderEdit(i);
                isCustomIntent_=true;
                preset_.setSelectedId(1,juce::dontSendNotification);
                submit(uiScene_);
                repaint();
            };
            addAndMakeVisible(slider);
            auto& label=labels_[static_cast<std::size_t>(i)];
            label.setComponentID(juce::String("imersip.label.")+ids[i]);
            label.setText(names[i],juce::dontSendNotification);
            label.setJustificationType(juce::Justification::centredLeft);
            label.setColour(juce::Label::textColourId,juce::Colour(0xffcbd7dc));
            label.setFont(juce::FontOptions(14.0f));
            addAndMakeVisible(label);
        }
        power_.setComponentID("imersip.power");
        power_.setButtonText({}); // Icon is drawn by the JUCE LookAndFeel.
        power_.setClickingTogglesState(false); // Host readback is authoritative.
        power_.setTooltip("FX state unavailable");
        power_.setLookAndFeel(&skin_);
        power_.setEnabled(false); // no fake bypass authority
        power_.onClick=[this] {
            if(!requestBypass_ || !readHostPower_)return;
            const auto before=readHostPower_();
            if(!before.valid || before.globalBypassed ||
               before.rackBypassed) {
                status_=before.globalBypassed?"GLOBAL FX BYPASSED - USE MIXER POWER":
                        before.rackBypassed?"FX RACK BYPASSED - USE RACK POWER":
                                            "FX INSTANCE NO LONGER ACTIVE";
                refreshHostPower();
                repaint();
                return;
            }
            const bool wanted=!before.slotBypassed;
            const bool accepted=requestBypass_(wanted);
            const auto after=readHostPower_();
            status_=!accepted?"HOST FX POWER REJECTED - REFRESH FX RACK":
                   !after.valid?"FX INSTANCE CHANGED - REOPEN EDITOR":
                   after.slotBypassed!=wanted?"HOST FX POWER NOT CONFIRMED":
                   wanted?"FX BYPASS ACCEPTED":"FX ENABLE ACCEPTED";
            refreshHostPower();
            repaint();
        };
        addAndMakeVisible(power_);
        sync(fx_.acceptedSettings());
        startTimerHz(25);
    }
    ~ImerSipP92Editor() override {
        stopTimer();
        for(auto& slider:knobs_)slider.setLookAndFeel(nullptr);
        preset_.setLookAndFeel(nullptr);
        room_.setLookAndFeel(nullptr);
        power_.setLookAndFeel(nullptr);
    }
    bool areVisualAssetsReady() const {return skin_.ready();}
    // Message-thread diagnostic for optional low-spec asset profiling.
    std::size_t residentVenueCount() const noexcept {
        return skin_.residentVenueCount();
    }
    void bindHostPower(std::function<bool(bool)> request,
                       std::function<HostBypassState()> readback) {
        requestBypass_=std::move(request);
        readHostPower_=std::move(readback);
        refreshHostPower();
    }
    // Called by mixer dispatch after accepted Rack/Global FX ACK on JUCE's
    // message thread. Timer polling remains a fallback for external changes.
    void synchronizeHostPower() { refreshHostPower(); }
    static constexpr float compactScale=0.85f;
    // Master canvas and all native hitboxes share exactly one conversion.
    void paint(juce::Graphics& g) override {
        g.fillAll(juce::Colour(0xff11181d));
        juce::Graphics::ScopedSaveState save(g);
        g.addTransform(juce::AffineTransform::scale(compactScale));
        // One authentic photo venue per scene, never the abandoned
        // holographic room or stacked artificial speaker sprites.
        // Only approved concert photography; rejected procedural art is removed.
        skin_.drawScene(g,visualSceneId_);
        if(hostBypassed_.value_or(false))
            skin_.drawBypassedVenueOverlay(g);
        else if(hostLeaseWasValid_ && !hostBypassed_.has_value())
            skin_.drawUnavailableVenueOverlay(g);

        // No second header: the native plugin window already owns its title.
        // Preserve the venue art's full 1100x461 scene viewport.

        // Flat JUCE-painted panel: no bitmap, bevels, screws, or partitions.
        skin_.drawPanel(g);
        g.setColour(juce::Colour(0xff33424b));
        g.fillRect(0,461,1100,1);
        g.setColour(juce::Colour(0xffbacbd0));
        g.setFont(juce::FontOptions(13.8f));
        g.drawText("PRESET",24,485,208,19,juce::Justification::centredLeft);
        g.drawText("ROOM",24,564,208,19,juce::Justification::centredLeft);

        // Seven precise native controls in two relaxed 308px lanes.
        // One row owns one label, one drag rail, and one right-aligned
        // readback. Avoid four 106px rails squeezed under long labels.
        constexpr int ids[]={0,1,2,3,4,6,7};
        for(int j=0;j<7;++j){
            const int idx=ids[j],col=j%2,row=j/2;
            const int x=252+317*col,y=485+43*row;
            const double v=knobs_[static_cast<std::size_t>(idx)].getValue();
            const juce::String value=idx==0?juce::String(v,0)+"°":
                idx==1?juce::String(v,1)+" m":
                idx==6||idx==7?juce::String(v,0)+"°":
                juce::String(v,0)+"%";
            g.setColour(juce::Colour(0xffd6efea));
            g.setFont(juce::FontOptions(13.2f));
            g.drawText(value,x+254,y+3,49,22,
                       juce::Justification::centredRight);
        }
        const auto gain=knobs_[5].getValue();
        g.setColour(juce::Colour(0xfff0eafa));
        g.setFont(juce::FontOptions(16.f));
        g.drawText((gain>=0?"+":"")+juce::String(gain,1)+" dB",
                   886,611,138,23,juce::Justification::centred);
        g.setColour(juce::Colour(0xffb4c3c9));
        g.setFont(juce::FontOptions(12.2f));
        g.drawText("AUDIO  /  "+status_,24,648,838,23,
                   juce::Justification::centredLeft);
    }
    void paintOverChildren(juce::Graphics& g) override {
        // Parent paint() cannot dim child Components: JUCE draws their
        // labels, sliders and popup controls AFTER parent paint().
        // A distinct ~44% muted charcoal pass makes all inactive
        // controls visibly recede, without losing label legibility.
        // Exclude FX Power so the host-authoritative toggle remains
        // crisp, keyboard/mouse accessible and visually actionable.
        if(!hostBypassed_.value_or(false) &&
           !(hostLeaseWasValid_ && !hostBypassed_.has_value()))return;
        juce::Graphics::ScopedSaveState saved(g);
        const auto panelTop=juce::roundToInt(461.f*compactScale);
        const auto panel=juce::Rectangle<int>(
            0,panelTop,getWidth(),getHeight()-panelTop);
        g.reduceClipRegion(panel);
        g.excludeClipRegion(power_.getBounds());
        g.setColour(juce::Colour(0x7603060a)); // 46% muted neutral charcoal
        g.fillRect(panel);
    }
    void resized() override {
        const auto place=[](juce::Component& child,int x,int y,int w,int h){
            child.setBounds(juce::roundToInt(x*compactScale),
                            juce::roundToInt(y*compactScale),
                            juce::roundToInt(w*compactScale),
                            juce::roundToInt(h*compactScale));
        };
        place(preset_,24,507,206,36);
        place(room_,24,586,206,36);
        constexpr int ids[]={0,1,2,3,4,6,7};
        for(int j=0;j<7;++j){
            const int col=j%2,row=j/2;
            const int x=252+317*col,y=485+43*row;
            auto& slider=knobs_[static_cast<std::size_t>(ids[j])];
            auto& label=labels_[static_cast<std::size_t>(ids[j])];
            place(label,x,y,132,24);
            place(slider,x+134,y,114,25);
        }
        // Control order: two slider lanes -> master gain -> host FX Power.
        // Right-side power cell is independent of the master rotary hitbox.
        place(labels_[5],884,484,139,22);
        place(knobs_[5],908,510,98,94);
        place(power_,1032,483,44,44);
    }
private:
    void applySliderEdit(int axis) {
        const float v=static_cast<float>(
            knobs_[static_cast<std::size_t>(axis)].getValue());
        switch(axis) {
            case 0: uiScene_.stereoSpreadDegrees=v;break;
            case 1: uiScene_.sourceDistanceMeters=std::clamp(v,
                imersipP92MinDistance(),
                imersipP92MaxDistance(uiScene_.spacePreset));break;
            case 2: uiScene_.roomAmount=v*.01f;break;
            case 3: uiScene_.ambience=v*.01f;break;
            case 4: uiScene_.spatialMix=v*.01f;break;
            case 5: uiScene_.outputTrim=std::pow(10.f,v/20.f);break;
            case 6: uiScene_.hrtfElevationTrimDegrees=v;break;
            case 7: uiScene_.headYawDegrees=v;break;
            default: jassertfalse;break;
        }
    }
    void sync(const armix::sonku::Settings& s){
        uiScene_=s;
        room_.setSelectedId(s.spacePreset+1,juce::dontSendNotification);
        const double vals[]={s.stereoSpreadDegrees,s.sourceDistanceMeters,
            100.*s.roomAmount,100.*s.ambience,100.*s.spatialMix,
            20.*std::log10(s.outputTrim),s.hrtfElevationTrimDegrees,
            s.headYawDegrees};
        for(int i=0;i<8;++i)knobs_[static_cast<std::size_t>(i)].setValue(
            vals[i],juce::dontSendNotification);
        int preset=1;
        if(!isCustomIntent_)for(const auto& factory:imersipP92Presets){
            const int id=factory.id;
            const auto candidate=imersipP92Factory(id);
            if(s.spacePreset==candidate.spacePreset &&
               std::abs(s.stereoSpreadDegrees-candidate.stereoSpreadDegrees)<.01f &&
               std::abs(s.sourceDistanceMeters-candidate.sourceDistanceMeters)<.01f &&
               std::abs(s.roomAmount-candidate.roomAmount)<.001f &&
               std::abs(s.ambience-candidate.ambience)<.001f &&
               std::abs(s.spatialMix-candidate.spatialMix)<.001f &&
               std::abs(s.outputTrim-candidate.outputTrim)<.001f &&
#if defined(SONKU_P98_SCENE_VOICING_RESEARCH) && SONKU_P98_SCENE_VOICING_RESEARCH
               s.sceneVoicing==candidate.sceneVoicing &&
#endif
               std::abs(s.headYawDegrees)<.001f &&
               std::abs(s.hrtfElevationTrimDegrees)<.001f) {preset=id;break;}
        }
        if(preset!=1)visualSceneId_=preset;
        // Adjusting a slider marks "Custom" but deliberately keeps the
        // chosen venue image until the operator picks another scene.
        preset_.setSelectedId(preset,juce::dontSendNotification);
        repaint();
    }
    void submit(const armix::sonku::Settings& wanted) {
        if(!validImerSipP92Scene(wanted)){
            status_="INVALID ROOM / SOURCE GEOMETRY";repaint();return;
        }
        if(pending_!=0){
            deferred_=wanted;hasDeferred_=true;
            status_="LATEST EDIT QUEUED";repaint();return;
        }
        const auto id=fx_.nextEditorRequestId();
        if(!fx_.queueSettings(wanted,id)){
            deferred_=wanted;hasDeferred_=true;
            status_="WAITING FOR AUDIO CALLBACK";repaint();return;
        }
        pending_=id;
        status_="PENDING NATIVE AUDIO ACK";
        repaint();
    }
    void refreshHostPower() {
        // One effective state = individual slot OR rack gate OR global A/B.
        // Never flatten these independent remembered bypass preferences.
        const auto state=readHostPower_?readHostPower_():HostBypassState{};
        const bool valid=state.valid;
        const bool bypassed=state.effectiveBypassed();
        const bool enabled=valid && static_cast<bool>(requestBypass_) &&
                           !state.rackBypassed && !state.globalBypassed;
        // Host gate/readback can stay unchanged across thousands of timer
        // ticks. Skip redundant JUCE setEnabled, setToggleState and tooltip
        // String updates; never cache the authoritative host callback itself.
        const bool hasRequest=static_cast<bool>(requestBypass_);
        if(hostReadbackCached_ && cachedHasRequest_==hasRequest &&
           lastHostReadback_.valid==state.valid &&
           lastHostReadback_.slotBypassed==state.slotBypassed &&
           lastHostReadback_.rackBypassed==state.rackBypassed &&
           lastHostReadback_.globalBypassed==state.globalBypassed)
            return;
        lastHostReadback_=state;
        cachedHasRequest_=hasRequest;
        hostReadbackCached_=true;
        // Full-scene dimming needs repaint only on actual host state change.
        const bool changed=(valid!=hostBypassed_.has_value()) ||
                           (valid && *hostBypassed_!=bypassed);
        if(valid) {
            hostLeaseWasValid_=true;
            hostBypassed_=bypassed;
        } else hostBypassed_.reset();
        power_.setEnabled(enabled);
        power_.setToggleState(valid && !bypassed,juce::dontSendNotification);
        power_.setTooltip(!valid?"FX instance unavailable - reopen from rack":
            state.globalBypassed?"GLOBAL FX BYPASSED - enable mixer FX":
            state.rackBypassed?"FX RACK BYPASSED - enable rack power":
            state.slotBypassed?"FX BYPASSED - click to enable":
                               "FX ON - click to bypass");
        // Host changes can arrive from another window while this editor is
        // open. 25 Hz control-thread polling updates visual status on change.
        if(changed)repaint();
    }
    void timerCallback() override {
        const auto previousStatus=status_;
        SonKuSpatialProcessor::Completion done{};
        std::optional<bool> orphanApplied;
        // An edit queued before the prior editor was closed may ACK after
        // this editor is constructed. That request is not pending_ here;
        // reconcile actual accepted PCM, never invent a second callback.
        while(fx_.pollCompletion(done)){
            if(pending_==0 || done.requestId!=pending_)
                orphanApplied=done.applied;
        }
        if(pending_==0 && !hasDeferred_ && orphanApplied.has_value()){
            sync(fx_.acceptedSettings());
            status_=*orphanApplied?"RECONCILED NATIVE AUDIO ACK":
                                   "NATIVE AUDIO EDIT REJECTED";
        }
        if(pending_!=0 && fx_.lastCompletedRequestId()==pending_){
            const bool ok=fx_.lastCompletionApplied();
            pending_=0;
            status_=ok?"APPLIED TO P9 PCM":"REJECTED BY P9 PCM";
            if(!ok || !hasDeferred_)sync(fx_.acceptedSettings());
        }
        if(pending_==0 && hasDeferred_){
            const auto next=deferred_;
            hasDeferred_=false;
            if(validImerSipP92Scene(next)){
                const auto id=fx_.nextEditorRequestId();
                if(fx_.queueSettings(next,id)){
                    pending_=id;status_="APPLYING LATEST EDIT";
                }else{
                    deferred_=next;hasDeferred_=true;
                }
            }
        }
        refreshHostPower();
        // No animated status text: repainting the footer unconditionally at
        // 25 Hz wastes GUI cycles on idle low-spec PCs. submit()/sync() and
        // host state transitions repaint themselves; timer needs only changes.
        if(status_!=previousStatus)
            repaint(juce::Rectangle<int>(0,juce::roundToInt(646*compactScale),
                                         getWidth(),juce::roundToInt(34*compactScale)));
    }
    int visualSceneId_=2; // UI-only scene selection; not IMRS / PCM state.
    bool isCustomIntent_=false;
    armix::sonku::Settings uiScene_{}; // Unquantized intent owned by GUI thread.
    ImerSipP92Processor& fx_;
    ImerSipP92VisualSkin skin_;
    juce::ComboBox preset_{},room_{};
    juce::TextButton power_{};
    std::array<juce::Slider,8> knobs_{};
    std::array<juce::Label,8> labels_{};
    std::function<bool(bool)> requestBypass_;
    std::function<HostBypassState()> readHostPower_;
    std::optional<bool> hostBypassed_{}; // UI cache of actual host readback only
    bool hostLeaseWasValid_=false; // stale-lease UI only; never audio authority
    HostBypassState lastHostReadback_{};
    bool hostReadbackCached_=false,cachedHasRequest_=false;

    armix::sonku::Settings deferred_{};
    std::uint64_t pending_=0;
    bool hasDeferred_=false;
    juce::String status_="P9.2 CONCERT ENGINE READY";
};
inline juce::AudioProcessorEditor* ImerSipP92Processor::createEditor() {
    return new ImerSipP92Editor(*this);
}
} // namespace armix::builtin_fx
