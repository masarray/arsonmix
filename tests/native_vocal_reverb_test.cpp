// SPDX-License-Identifier: AGPL-3.0-only
#if defined(NDEBUG)
#undef NDEBUG
#endif
#include "../engine/builtin_fx/VocalReverbProcessor.h"
#include "../src/NativeBuiltInFx.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <vector>
#include <thread>

int main() {
    juce::ScopedJuceInitialiser_GUI gui;
    using namespace armix::builtin_fx;
    static_assert(VocalReverbProcessor::stateBytes==68);
    for(int i=0;i<reverbSisPresetCount;++i)assert(validVocalReverbSettings(vocalReverbFactoryPreset(i)));
    static_assert(reverbSisPresetCount==13 && reverbSisCustomItem==14);
    const auto mc=vocalReverbFactoryPreset(12);
    assert(juce::String(vocalReverbPresetNames[12])=="Master Ceremony");
    assert(mc.preset==12&&mc.algorithm==static_cast<int>(arverb::Algorithm::Chamber));
    assert(mc.values[static_cast<unsigned>(arverb::Parameter::Mix)]==.14f);
    assert(mc.values[static_cast<unsigned>(arverb::Parameter::DecaySeconds)]==.78f);
    const auto* entry=findNativeBuiltInFx(VocalReverbProcessor::catalogIdentity);
    assert(entry&&entry->showInPicker);
    assert(juce::String(entry->category)=="Reverb");
    assert(!entry->prepare(384000.,128,nullptr,0)); // pinned DSP max=192k
    assert(!entry->prepare(48000.,4,nullptr,0));
    auto created=entry->prepare(48000.,128,nullptr,0);
    assert(created && created->getName()=="Reverb-Sis" && created->hasEditor());
    // Detached logical editor has NO callback. Save the newest knob/preset
    // packet on CONTROL, prove exact state restore, reject a published lease.
    {
        auto detached=entry->prepare(48000.,256,nullptr,0);
        auto* owner=dynamic_cast<VocalReverbProcessor*>(detached.get());
        assert(owner);
        auto edited=vocalReverbFactoryPreset(11);
        edited.preset=-1;edited.values[0]=.73f;
        assert(owner->queueSettings(vocalReverbFactoryPreset(8))>0);
        const auto final=owner->queueSettings(edited);
        assert(final>0&&owner->settingsPending());
        juce::MemoryBlock bytes;
        detached->getStateInformation(bytes);
        assert(bytes.isEmpty()); // ordinary published capture refuses pending packet
        assert(owner->captureDetachedState(bytes));
        assert(owner->processedBlocks()==0&&owner->appliedRequestId()==final);
        assert(bytes.getSize()==VocalReverbProcessor::stateBytes);
        auto restored=entry->prepare(48000.,256,bytes.getData(),
                                     static_cast<int>(bytes.getSize()));
        auto* accepted=dynamic_cast<VocalReverbProcessor*>(restored.get());
        VocalReverbSettings saved{};
        assert(accepted&&accepted->acceptedSettings(saved)&&
               saved.values==edited.values&&saved.algorithm==edited.algorithm&&saved.preset==-1);
        owner->sealForRealtime();
        assert(owner->queueSettings(vocalReverbFactoryPreset(9))>0);
        assert(!owner->captureDetachedState(bytes)&&bytes.isEmpty());
        assert(owner->settingsPending()&&owner->appliedRequestId()==final);
    }
    // Test the real native JUCE faceplate, not a WebView mock or a fake
    // static rendered image. Dimensions and all param IDs are contract.
    // This host builds a private AudioProcessor::createEditor() facade.
    // Reverb's concrete override is public: test the actual native editor,
    // not the inaccessible base-class method.
    // Use an independent display-only native DSP instance. Exercising
    // authentic PCM meter input may not perturb the main test's exact
    // processedBlocks() or session-recovery assertions.
    auto displayInstance=entry->prepare(48000.,128,nullptr,0);
    auto* uiOwner=dynamic_cast<VocalReverbProcessor*>(displayInstance.get());
    assert(uiOwner);
    std::unique_ptr<juce::AudioProcessorEditor> ui(uiOwner->createEditor());
    assert(ui && ui->getWidth()==1120 && ui->getHeight()==430);
    auto* mainDecay=dynamic_cast<juce::Slider*>(
        ui->findChildWithID("armix.reverb.param.2"));
    auto* mainMix=dynamic_cast<juce::Slider*>(
        ui->findChildWithID("armix.reverb.param.0"));
    auto* presetSelect=dynamic_cast<juce::ComboBox*>(
        ui->findChildWithID("armix.reverb.preset"));
    auto* algorithmSelect=dynamic_cast<juce::ComboBox*>(
        ui->findChildWithID("armix.reverb.algorithm"));
    assert(mainDecay&&mainMix&&presetSelect&&algorithmSelect);
    auto* productTitle=dynamic_cast<juce::Label*>(
        ui->findChildWithID("armix.reverb.title"));
    assert(productTitle && productTitle->getText()=="REVERB-SIS");
    // The same embedded Inter typeface must render Windows screenshots and
    // release builds; a host-installed substitute is not sufficient.
    assert(productTitle->getFont().getTypefaceName().containsIgnoreCase("Inter"));
    // Visual status is intentionally hidden. PCM-applied ACK remains on the
    // processor and contextual tooltip, never a permanently printed stamp.
    assert(ui->findChildWithID("armix.reverb.status")==nullptr);
    assert(ui->getLocalBounds().contains(productTitle->getBounds()));
    assert(!productTitle->getBounds().intersects(presetSelect->getBounds()));
    assert(productTitle->getBottom()<mainDecay->getY());
    // Live LCDs show native-realistic physical units and round-trip text
    // edits to the SAME float parameter. No 8-decimal debug readouts.
    assert(mainDecay->getTextFromValue(2.05)=="2.05 s");
    assert(mainMix->getTextFromValue(.18)=="18 %");
    assert(std::fabs(mainMix->getValueFromText("27 %")-.27)<1.e-6);
    assert(std::fabs(mainDecay->getValueFromText("3.25 s")-3.25)<1.e-6);
    assert(mainDecay->isVisible()&&mainMix->isVisible());
    assert(!mainDecay->getBounds().intersects(mainMix->getBounds()));
    assert(presetSelect->getNumItems()==reverbSisCustomItem && algorithmSelect->getNumItems()==4);
    assert(presetSelect->getItemText(12)=="Master Ceremony");
    assert(presetSelect->getItemId(12)==13);
    assert(presetSelect->getItemText(13)=="Custom / saved");
    // Test actual selection and detached state round-trip independently
    // of the main faceplate/meter fixture.
    {
        auto speech=entry->prepare(48000.,128,nullptr,0);
        auto* owner=dynamic_cast<VocalReverbProcessor*>(speech.get());
        assert(owner);
        std::unique_ptr<juce::AudioProcessorEditor> editor(owner->createEditor());
        auto* select=dynamic_cast<juce::ComboBox*>(editor->findChildWithID("armix.reverb.preset"));
        assert(select);
        select->setSelectedId(13,juce::sendNotificationSync);
        juce::MemoryBlock saved;
        assert(owner->captureDetachedState(saved));
        auto restored=entry->prepare(48000.,128,saved.getData(),static_cast<int>(saved.getSize()));
        auto* decoded=dynamic_cast<VocalReverbProcessor*>(restored.get());
        VocalReverbSettings accepted{};
        assert(decoded&&decoded->acceptedSettings(accepted)&&accepted.preset==12&&
               accepted.values==mc.values&&accepted.algorithm==mc.algorithm);
    }
    // Combo label and down-chevron share EXACT row centre. Test both
    // native ComboBoxes rather than relying on developer screenshot eye.
    for(auto* combo : {presetSelect,algorithmSelect}) {
        assert(combo->getNumChildComponents()>0);
        const auto* text=dynamic_cast<juce::Label*>(combo->getChildComponent(0));
        assert(text);
        assert(std::abs(text->getBounds().getCentreY()-
                        combo->getHeight()/2)<=1);
        assert(text->getRight()<=combo->getWidth()-24);
        assert(text->getFont().getTypefaceName().containsIgnoreCase("Inter"));
    }
    auto* leftChevron=dynamic_cast<juce::TextButton*>(
        ui->findChildWithID("armix.reverb.preset.previous"));
    auto* rightChevron=dynamic_cast<juce::TextButton*>(
        ui->findChildWithID("armix.reverb.preset.next"));
    assert(leftChevron&&rightChevron&&
           leftChevron->getButtonText().isEmpty()&&
           rightChevron->getButtonText().isEmpty());
    assert(leftChevron->getBounds().getCentreY()==
           rightChevron->getBounds().getCentreY());
    assert(leftChevron->getBounds().getCentreY()==
           presetSelect->getBounds().getCentreY());
    assert(presetSelect->getBounds().getCentreX()==ui->getWidth()/2);
    assert(presetSelect->getChildComponent(0)->getBounds().getCentreX()==presetSelect->getWidth()/2);
    assert(leftChevron->getX()+rightChevron->getRight()==ui->getWidth());
    assert(mainDecay->getX()+mainMix->getRight()==ui->getWidth());
    const auto* wetLabel=ui->findChildWithID("armix.reverb.label.0");
    const auto* decayLabel=ui->findChildWithID("armix.reverb.label.2");
    assert(wetLabel&&decayLabel);
    assert(algorithmSelect->getBottom()<=wetLabel->getY());
    assert(!algorithmSelect->getBounds().intersects(wetLabel->getBounds()));
    assert(!productTitle->getBounds().intersects(decayLabel->getBounds()));
    assert(algorithmSelect->getBounds().getBottom()<mainMix->getY()+40);
    assert(mainMix->getY()==124 && mainDecay->getY()==124);
    auto* power=dynamic_cast<juce::TextButton*>(ui->findChildWithID("armix.reverb.power"));
    auto* close=dynamic_cast<juce::TextButton*>(ui->findChildWithID("armix.reverb.close"));
    assert(power&&close&&power->isVisible()&&close->isVisible()&&!power->isEnabled());
    assert(power->getWidth()==26&&power->getHeight()==24);
    assert(close->getWidth()==24&&close->getHeight()==24);
    assert(power->getRight()+8==close->getX());
    assert(power->getY()==close->getY()&&close->getRight()==ui->getWidth()-25);
    assert(power->getButtonText().isEmpty()&&close->getButtonText().isEmpty());
    assert(power->getProperties().getWithDefault("armixFxState","").toString()=="unknown");
    assert(power->getBounds().getCentreY()==presetSelect->getBounds().getCentreY()-5);
    assert(close->getBounds().getCentreY()==power->getBounds().getCentreY());
    assert(std::abs(algorithmSelect->getBounds().getCentreX()-mainMix->getBounds().getCentreX())<=1);
    assert(close->getBottom()<reverb_ui::FaceplateLayout::shoulderY);
    assert(!power->getBounds().intersects(presetSelect->getBounds()));
    assert(!close->getBounds().intersects(presetSelect->getBounds()));
    for(int i=0;i<6;++i) {
        auto* tab=dynamic_cast<juce::TextButton*>(
            ui->findChildWithID("armix.reverb.tab."+juce::String(i)));
        assert(tab&&tab->isVisible()&&!tab->getBounds().isEmpty());
        assert(ui->getLocalBounds().contains(tab->getBounds()));
    }
    assert(ui->findChildWithID("armix.reverb.tab.0")->getX()+
           ui->findChildWithID("armix.reverb.tab.5")->getRight()==ui->getWidth());
    // Layout-contract QA across ALL six actual JUCE pages, not just the
    // factory MIX screenshot. Text bounds have 7px gutters in each 115px
    // column; previous 120px labels overlapped their neighbours by 5px.
    for(int page=0;page<6;++page) {
        auto* button=dynamic_cast<juce::TextButton*>(
            ui->findChildWithID("armix.reverb.tab."+juce::String(page)));
        assert(button && button->onClick);
        button->onClick();
        std::array<juce::Rectangle<int>,7> occupied{};
        int visibleLabels=0, visibleKnobs=0;
        for(int i=0;i<13;++i) {
            auto* label=ui->findChildWithID(
                "armix.reverb.label."+juce::String(i));
            auto* slider=dynamic_cast<juce::Slider*>(ui->findChildWithID(
                "armix.reverb.param."+juce::String(i)));
            assert(label && slider);
            if(label->isVisible()) {
                assert(visibleLabels<7);
                assert(ui->getLocalBounds().contains(label->getBounds()));
                for(int j=0;j<visibleLabels;++j)
                    assert(!occupied[static_cast<std::size_t>(j)].intersects(
                        label->getBounds()));
                occupied[static_cast<std::size_t>(visibleLabels++)]=label->getBounds();
                if(i!=0&&i!=2) {
                    assert(label->getWidth()<=110);
                    assert(label->getY()==reverb_ui::FaceplateLayout::smallCaptionY && slider->getY()==221);
                }
            }
            if(slider->isVisible()) {
                ++visibleKnobs;
                using F=reverb_ui::FaceplateLayout;
                const bool hero=i==0||i==2;
                const auto layout=slider->getLookAndFeel().getSliderLayout(*slider);
                const auto dial=layout.sliderBounds.translated(slider->getX(),slider->getY());
                const auto lcd=layout.textBoxBounds.translated(slider->getX(),slider->getY());
                assert(dial.getCentreX()==label->getBounds().getCentreX());
                assert(dial.getCentreY()==(hero?F::heroCentreY:F::smallCentreY));
                assert(dial.getWidth()==(hero?F::heroDial:F::smallDial));
                assert(lcd.getCentreX()==dial.getCentreX()&&lcd.getCentreY()==F::lcdCentreY);
                assert(lcd.getY()>=F::lcdTop&&lcd.getBottom()<=F::lcdBottom);
                // Include LED glow/stroke beyond nominal rotary draw rectangle.
                const float radius=dial.getWidth()*(hero?.366f:.338f)+
                    (hero?8.f+13.f+3.8f+2.f+3.25f:5.f+9.f+2.5f+2.f+2.f);
                assert(radius<slider->getWidth()*.5f);
                assert(dial.getCentreY()-radius>label->getBottom());
                assert(dial.getCentreY()+radius<lcd.getY());
                bool labelClicks=true,childClicks=true;
                label->getInterceptsMouseClicks(labelClicks,childClicks);
                assert(!labelClicks&&!childClicks);
            }
        }
        assert(visibleLabels==7 && visibleKnobs==7);
        assert(mainDecay->isVisible()&&mainMix->isVisible());
        // Tabs cannot sit on top of the small-control captions.
        for(int i=0;i<6;++i) {
            auto* tab=ui->findChildWithID(
                "armix.reverb.tab."+juce::String(i));
            for(const auto& rect:occupied)
                assert(!tab->getBounds().intersects(rect));
        }
    }
    auto* restoreMix=dynamic_cast<juce::TextButton*>(
        ui->findChildWithID("armix.reverb.tab.0"));
    assert(restoreMix && restoreMix->onClick);
    restoreMix->onClick();
    auto* firstPage=dynamic_cast<juce::Slider*>(
        ui->findChildWithID("armix.reverb.param.1"));
    auto* motionPage=dynamic_cast<juce::Slider*>(
        ui->findChildWithID("armix.reverb.param.8"));
    assert(firstPage&&firstPage->isVisible()&&motionPage&&!motionPage->isVisible());
    for(int id=0;id<13;++id) {
        auto* knob=dynamic_cast<juce::Slider*>(
            ui->findChildWithID("armix.reverb.param."+juce::String(id)));
        assert(knob);
        assert(knob->getSliderStyle()==juce::Slider::RotaryHorizontalVerticalDrag);
        if(knob->isVisible())assert(ui->getLocalBounds().contains(knob->getBounds()));
    }
    // Real signal, never synthetic animated LED graphics. The separate
    // native JUCE DSP measures four actual L/R in/out block peaks, and the
    // 30 Hz editor timer consumes ONLY those atomics.
    juce::AudioBuffer<float> liveMeterPcm(2,128);
    juce::MidiBuffer liveMeterMidi;
    for(int block=0;block<16;++block) {
        for(int sample=0;sample<128;++sample) {
            const double phase=(block*128+sample)*0.039f;
            liveMeterPcm.setSample(0,sample,
                                  static_cast<float>(.42*std::sin(phase)));
            liveMeterPcm.setSample(1,sample,
                                  static_cast<float>(.31*std::sin(phase*.83)));
        }
        displayInstance->processBlock(liveMeterPcm,liveMeterMidi);
    }
    juce::Thread::sleep(42);
    juce::Timer::callPendingTimersSynchronously();
    auto snapshot=ui->createComponentSnapshot(ui->getLocalBounds());
    assert(snapshot.isValid()&&snapshot.getWidth()==1120&&snapshot.getHeight()==430);
    assert(snapshot.getPixelAt(16,16)!=snapshot.getPixelAt(568,353));
    // Native HDR-ish midnight-blue recessed header AND distinct navy
    // digital-footer band; this fails if a future regression flattens
    // the HD-CART-inspired silhouette into a generic blank JUCE panel.
    assert(snapshot.getPixelAt(540,23).getBlue()>
           snapshot.getPixelAt(540,89).getBlue());
    assert(snapshot.getPixelAt(540,378).getBlue()>
           snapshot.getPixelAt(540,320).getBlue());
    // Compact LCD begins at y374 instead of old y355; no oversized
    // cobalt block can regress unnoticed.
    assert(snapshot.getPixelAt(30,359).getBlue()<
           snapshot.getPixelAt(30,382).getBlue());
    // Native rendered pixels, including the previously black bottom 10px.
    // Keep only the same thin cabinet inset used at the top.
    for(float scale:{1.f,1.5f,2.f}) {
        const auto face=ui->createComponentSnapshot(ui->getLocalBounds(),true,scale);
        for(int x:{30,560,1090})for(int y:{409,414,416}) {
            const auto c=face.getPixelAt(static_cast<int>(x*scale),static_cast<int>(y*scale));
            assert(c.getBlue()>=65 && c.getBlue()>c.getRed()+35 &&
                   c.getGreen()>c.getRed()+15);
        }
    }
    // Group centred at x560; independent left/right 12px gutters.
    using M=reverb_ui::MeterLayout;
    static_assert(M::firstDotY==97 && M::secondDotY==132 && M::tickY==112);
    static_assert(M::groupLeft+M::groupRight==1120);
    static_assert(M::dotsLeftX-M::leftLabelX-M::leftLabelWidth==12);
    static_assert(M::rightLabelX-M::dotsRightX-19*M::pitch-M::dotSize==12);
    static_assert(M::dotsLeftX+M::dotsRightX+M::bankWidth==1120);
    static_assert(M::leftLabelWidth==M::rightLabelWidth);
    static_assert(M::tickOffset(-60)==3 && M::tickOffset(-24)==113 &&
                  M::tickOffset(-6)==173 && M::tickOffset(0)==193);
    assert(M::litCount(-80.f)==0 && M::litCount(-60.f)==0);
    assert(M::litCount(-24.f)==12 && M::litCount(-6.f)==18);
    assert(M::litCount(0.f)==20 && M::litCount(6.f)==20);
    assert(juce::String(M::leftLabels[0])=="INPUT L");
    assert(juce::String(M::rightLabels[0])=="INPUT R");
    assert(juce::String(M::leftLabels[1])=="OUTPUT L");
    assert(juce::String(M::rightLabels[1])=="OUTPUT R");
    // Guard incremental live paint: every segment and its glow, not only
    // the pixels that happen to be visible in a full component snapshot.
    for(int x : {M::dotsLeftX,M::dotsRightX})
        for(int y : {M::firstDotY,M::secondDotY})
            for(int segment=0;segment<M::segments;++segment) {
                const juce::Rectangle<int> led(x+segment*M::pitch,y,
                                                M::dotSize,M::dotSize);
                assert(M::dirtyBounds().contains(led.expanded(M::glowMargin)));
            }
    assert(ui->getLocalBounds().contains(M::dirtyBounds()));
    // At least the left input meter has a true bright PCM segment; the
    // far-right dot stays dark because input is safely below 0 dBFS.
    assert(snapshot.getPixelAt(M::dotsLeftX+2,M::firstDotY+3)!=
           snapshot.getPixelAt(M::dotsLeftX+19*M::pitch+2,
                               M::firstDotY+3));
    // These are snapshots of the ACTUAL JUCE AudioProcessorEditor canvas.
    // No HTML mockup, screenshot reference, or image synthesis involved.
    const auto saveImage=[](const juce::Image& picture,const char* name) {
        const auto file=juce::File::getCurrentWorkingDirectory().getChildFile(name);
        juce::FileOutputStream out(file);
        assert(out.openedOk());
        juce::PNGImageFormat png;
        assert(png.writeImageToStream(picture,out));
        out.flush();
        assert(file.existsAsFile() && file.getSize()>10000);
    };
    saveImage(snapshot,"ArSonMix_Vocal_Reverb_Native_MIX_100.png");
    // The actual editor must show all three accepted host gates, not a
    // local toggle. A missing lease is never reported as BYPASSED.
    auto* reverbEditor=dynamic_cast<VocalReverbEditor*>(ui.get());
    assert(reverbEditor);
    using Overlay=VocalReverbEditor::HostOverlay;
    assert(reverbEditor->hostOverlay()==Overlay::none);
    VocalReverbEditor::HostBypassState host{true,false,false,false};
    int powerRequests=0;
    bool rejectPower=false;
    reverbEditor->bindHostPower([&](bool bypass){
        ++powerRequests;
        if(rejectPower)return false;
        host.slotBypassed=bypass;return true;
    },[&]{return host;});
    assert(power->isEnabled()&&power->getToggleState());
    power->onClick();
    assert(powerRequests==1 && host.slotBypassed && !power->getToggleState());
    assert(power->getProperties().getWithDefault("armixFxState","").toString()=="off");
    power->onClick();
    assert(powerRequests==2 && !host.slotBypassed && power->getToggleState());
    assert(power->getProperties().getWithDefault("armixFxState","").toString()=="on");
    rejectPower=true;
    power->onClick();
    assert(powerRequests==3 && !host.slotBypassed && power->getToggleState());
    rejectPower=false;
    const auto amberPixels=[](const juce::Image& image) {
        int count=0;
        for(int y=164;y<286;++y)for(int x=300;x<820;++x) {
            const auto c=image.getPixelAt(x,y);
            if(c.getRed()>150&&c.getGreen()>60&&
               c.getRed()>c.getGreen()+30&&c.getGreen()>c.getBlue()+35)++count;
        }
        return count;
    };
    for(int gate=0;gate<3;++gate) {
        host={true,gate==0,gate==1,gate==2};
        reverbEditor->synchronizeHostBypass();
        assert(reverbEditor->hostOverlay()==Overlay::bypassed);
        assert(mainMix->isEnabled()); // valid bypassed FX remains editable
        assert(close->isEnabled()&&!power->getToggleState());
        assert(power->isEnabled()==(gate==0));
        assert(power->getProperties().getWithDefault("armixFxState","").toString()==
               (gate==0?"off":"rack-off"));
        if(gate!=0) {
            const int before=powerRequests;
            power->onClick();
            assert(powerRequests==before);
        }
        const auto picture=ui->createComponentSnapshot(ui->getLocalBounds());
        assert(amberPixels(picture)>amberPixels(snapshot)+200);
        if(gate==0)saveImage(picture,"ArSonMix_Vocal_Reverb_Native_BYPASSED_100.png");
    }
    host={false,true,true,true};
    reverbEditor->synchronizeHostBypass();
    assert(reverbEditor->hostOverlay()==Overlay::unavailable&&!mainMix->isEnabled());
    assert(!power->isEnabled()&&close->isEnabled());
    assert(power->getProperties().getWithDefault("armixFxState","").toString()=="unknown");
    host={true,false,false,false};
    reverbEditor->synchronizeHostBypass();
    assert(reverbEditor->hostOverlay()==Overlay::none&&mainMix->isEnabled());
    auto* motion=dynamic_cast<juce::TextButton*>(
        ui->findChildWithID("armix.reverb.tab.4"));
    assert(motion&&motion->onClick);
    motion->onClick(); // same page handler as a real click, no audio change
    assert(motionPage->isVisible()&&!firstPage->isVisible());
    assert(mainDecay->isVisible()&&mainMix->isVisible());
    auto motionImage=ui->createComponentSnapshot(ui->getLocalBounds(),true,1.5f);
    assert(motionImage.isValid()&&motionImage.getWidth()==1680&&
           motionImage.getHeight()==645);
    saveImage(motionImage,"ArSonMix_Vocal_Reverb_Native_MOTION_150.png");
    auto* duck=dynamic_cast<juce::Slider*>(
        ui->findChildWithID("armix.reverb.param.12"));
    assert(duck && !duck->isVisible()); // MOTION has no duplicate DUCKER
    auto* duckTab=dynamic_cast<juce::TextButton*>(
        ui->findChildWithID("armix.reverb.tab.5"));
    assert(duckTab && duckTab->onClick);
    duckTab->onClick();
    assert(duck->isVisible() && !motionPage->isVisible());
    assert(mainDecay->isVisible() && mainMix->isVisible());
    auto* nextPreset=dynamic_cast<juce::TextButton*>(
        ui->findChildWithID("armix.reverb.preset.next"));
    assert(nextPreset&&nextPreset->onClick);
    nextPreset->onClick(); // control-side draft update, not an ASIO ACK
    assert(presetSelect->getSelectedId()==2);
    // Close immediately, without a timer tick or PCM ACK. Reopen must show
    // pending intent and the next PCM callback must apply it even when closed.
    assert(uiOwner->settingsPending());
    const auto beforeCloseAck=uiOwner->appliedRequestId();
    ui.reset();
    ui.reset(uiOwner->createEditor());
    auto* reopenedPreset=dynamic_cast<juce::ComboBox*>(
        ui->findChildWithID("armix.reverb.preset"));
    assert(reopenedPreset&&reopenedPreset->getSelectedId()==2);
    ui.reset();
    assert(uiOwner->appliedRequestId()==beforeCloseAck);
    liveMeterPcm.clear();
    displayInstance->processBlock(liveMeterPcm,liveMeterMidi);
    assert(!uiOwner->settingsPending());
    VocalReverbSettings reopenedAccepted{};
    assert(uiOwner->acceptedSettings(reopenedAccepted)&&reopenedAccepted.preset==1);
    // Offline editing and 1000 fast intents: no backlog, no premature ACK,
    // no lost final setting; prepare/restart does not discard the mailbox.
    uiOwner->releaseResources();
    auto offline=vocalReverbFactoryPreset(0);
    offline.preset=-1;
    const auto beforeOffline=uiOwner->appliedRequestId();
    std::uint64_t last=0;
    for(int i=0;i<1000;++i) {
        offline.values[0]=static_cast<float>(i%100)/100.f;
        last=uiOwner->queueSettings(offline);
        assert(last>beforeOffline);
    }
    assert(uiOwner->settingsPending()&&!uiOwner->stateSnapshotQuiescent());
    juce::MemoryBlock blocked;
    uiOwner->getStateInformation(blocked);
    assert(blocked.getSize()==0);
    assert(uiOwner->appliedRequestId()==beforeOffline);
    uiOwner->prepareToPlay(48000.,128);
    assert(uiOwner->isReady()&&uiOwner->settingsPending());
    liveMeterPcm.clear();
    displayInstance->processBlock(liveMeterPcm,liveMeterMidi);
    assert(uiOwner->appliedRequestId()==last&&uiOwner->stateSnapshotQuiescent());
    assert(uiOwner->acceptedSettings(reopenedAccepted)&&
           reopenedAccepted.values[0]==offline.values[0]);

    // Real JUCE Slider pointer down/drag/up handlers, all 13 controls.
    // Each intermediate value must reach the live PCM before mouse-up.
    // Test the shipping editor/adapter at typical low ASIO block sizes.
    for(const auto rate : {44100.,48000.,96000.}) {
        const int frames=rate==44100.?32:rate==48000.?64:128;
        auto gestureInstance=entry->prepare(rate,frames,nullptr,0);
        auto* gestureProcessor=dynamic_cast<VocalReverbProcessor*>(gestureInstance.get());
        assert(gestureProcessor);
        std::unique_ptr<juce::AudioProcessorEditor> gestureUi(gestureProcessor->createEditor());
        gestureUi->setVisible(true); // real component hit-testing needs visible root
        auto reference=entry->prepare(rate,frames,nullptr,0);
        assert(reference);
        juce::AudioBuffer<float> audio(2,frames),referenceAudio(2,frames);
        bool compareMix=true;
        double mixDifference=0.;
        juce::MidiBuffer gestureMidi;
        std::uint64_t sampleIndex=0;
        std::array<float,2> previous{};
        float worstStep=0.f;
        const auto render=[&](int blocks) {
            for(int b=0;b<blocks;++b) {
                for(int j=0;j<frames;++j,++sampleIndex) {
                    const double phase=6.283185307179586*static_cast<double>(sampleIndex)/rate;
                    audio.setSample(0,j,static_cast<float>(.12*std::sin(220.*phase)+.035*std::sin(997.*phase)));
                    audio.setSample(1,j,static_cast<float>(.11*std::sin(224.*phase)+.03*std::sin(883.*phase)));
                }
                if(compareMix)
                    for(int ch=0;ch<2;++ch)
                        referenceAudio.copyFrom(ch,0,audio,ch,0,frames);
                gestureInstance->processBlock(audio,gestureMidi);
                if(compareMix) {
                    reference->processBlock(referenceAudio,gestureMidi);
                    for(int ch=0;ch<2;++ch)for(int j=0;j<frames;++j) {
                        const double delta=audio.getSample(ch,j)-referenceAudio.getSample(ch,j);
                        mixDifference+=delta*delta;
                    }
                }
                for(int ch=0;ch<2;++ch)for(int j=0;j<frames;++j) {
                    const auto y=audio.getSample(ch,j);
                    assert(std::isfinite(y)&&std::abs(y)<1.f);
                    worstStep=std::max(worstStep,std::abs(y-previous[static_cast<std::size_t>(ch)]));
                    previous[static_cast<std::size_t>(ch)]=y;
                }
            }
        };
        render(static_cast<int>(rate*.5/frames)); // warm audible wet history
        assert(mixDifference<1.e-12); // identical control and initial PCM
        constexpr std::array<int,13> pageFor{{0,0,0,1,1,3,0,0,4,4,0,0,5}};
        for(int index=0;index<13;++index) {
            auto* tab=dynamic_cast<juce::TextButton*>(gestureUi->findChildWithID(
                "armix.reverb.tab."+juce::String(pageFor[static_cast<std::size_t>(index)])));
            assert(tab&&tab->onClick);tab->onClick();
            auto* knob=dynamic_cast<juce::Slider*>(gestureUi->findChildWithID(
                "armix.reverb.param."+juce::String(index)));
            assert(knob&&knob->isVisible()&&knob->isEnabled());
            assert(knob->getMouseDragSensitivity()==280);
            const auto origin=juce::Point<float>(knob->getWidth()*.5f,knob->getHeight()*.45f);
            const auto local=origin.roundToInt()+knob->getPosition();
            const auto* hit=gestureUi->getComponentAt(local);
            assert(hit&&(hit==knob||knob->isParentOf(hit)));
            const auto downTime=juce::Time::getCurrentTime();
            const auto source=juce::Desktop::getInstance().getMainMouseSource();
            const auto event=[&](juce::Point<float> pos,bool held,bool moved) {
                return juce::MouseEvent(source,pos,
                    juce::ModifierKeys(held?juce::ModifierKeys::leftButtonModifier:0),
                    1.f,0.f,0.f,0.f,0.f,knob,knob,juce::Time::getCurrentTime(),
                    origin,downTime,1,moved);
            };
            const auto before=knob->getValue();
            const auto normalized=knob->valueToProportionOfLength(before);
            const float direction=normalized<.5?1.f:-1.f;
            const auto beforeId=gestureProcessor->appliedRequestId();
            knob->mouseDown(event(origin,true,false));
            assert(std::abs(knob->getValue()-before)<1.e-5);
            std::uint64_t ack=beforeId;
            for(int step=1;step<=8;++step) {
                const auto pos=origin.translated(0.f,-direction*step*5.f);
                knob->mouseDrag(event(pos,true,true));
                assert(gestureProcessor->settingsPending());
                render(1);
                assert(gestureProcessor->appliedRequestId()>ack);
                ack=gestureProcessor->appliedRequestId();
                VocalReverbSettings readback{};
                assert(gestureProcessor->acceptedSettings(readback));
                assert(std::abs(readback.values[static_cast<std::size_t>(index)]-
                                static_cast<float>(knob->getValue()))<1.e-5f);
                render(3);
            }
            if(index==0) {
                // Real rendered audio must diverge from a fixed-control
                // reference while the mouse is STILL down, not just ACK.
                assert(mixDifference>1.e-8);
                compareMix=false;
            }
            knob->mouseUp(event(origin.translated(0.f,-direction*40.f),false,true));
            assert(gestureProcessor->appliedRequestId()==ack);
            assert(!gestureProcessor->settingsPending());
            assert(std::abs(knob->getValue()-before)>1.e-5);
        }
        // A bounded low-level band-limited source plus wet history should
        // not yield gross discontinuities. This is objective regression,
        // not a subjective "inaudible on all hardware" certification.
        assert(worstStep<.15f);
    }
    // Concurrent single-producer/PCM-consumer stress: every accepted
    // packet must be a coherent complete factory snapshot, never torn.
    auto concurrent=entry->prepare(48000.,32,nullptr,0);
    auto* concurrentReverb=dynamic_cast<VocalReverbProcessor*>(concurrent.get());
    assert(concurrentReverb);
    std::atomic<bool> producerDone{false};
    std::thread pcmConsumer([&] {
        juce::AudioBuffer<float> pcm(2,32);
        juce::MidiBuffer midi;
        std::uint64_t previous=0;
        for(int block=0;block<200000;++block) {
            pcm.clear();
            concurrent->processBlock(pcm,midi);
            const auto id=concurrentReverb->appliedRequestId();
            assert(id>=previous);
            previous=id;
            VocalReverbSettings received{};
            if(concurrentReverb->acceptedSettings(received)) {
                assert(received.preset>=0 && received.preset<reverbSisPresetCount);
                const auto expected=vocalReverbFactoryPreset(received.preset);
                assert(received.values==expected.values &&
                       received.algorithm==expected.algorithm);
            }
            if(producerDone.load(std::memory_order_acquire)&&
               !concurrentReverb->settingsPending())return;
            if((block&31)==0)std::this_thread::yield();
        }
        assert(false && "latest intent failed to converge");
    });
    std::uint64_t concurrentLast=0;
    for(int i=0;i<5000;++i) {
        concurrentLast=concurrentReverb->queueSettings(vocalReverbFactoryPreset(i%reverbSisPresetCount));
        assert(concurrentLast>0);
        if((i&31)==0)std::this_thread::yield();
    }
    producerDone.store(true,std::memory_order_release);
    pcmConsumer.join();
    assert(concurrentReverb->appliedRequestId()==concurrentLast);
    VocalReverbSettings concurrentAccepted{};
    assert(concurrentReverb->acceptedSettings(concurrentAccepted)&&
           concurrentAccepted.preset==7);

    assert(created->getLatencySamples()==0);
    auto* typed=dynamic_cast<VocalReverbProcessor*>(created.get());
    assert(typed&&typed->isReady());
    juce::MemoryBlock state;
    created->getStateInformation(state);
    assert(static_cast<int>(state.getSize())==VocalReverbProcessor::stateBytes);
    auto restored=entry->prepare(44100.,64,state.getData(),static_cast<int>(state.getSize()));
    assert(restored);
    auto invalid=state;
    invalid[0]=0;
    assert(!entry->prepare(48000.,128,invalid.getData(),static_cast<int>(invalid.getSize())));
    assert(!entry->prepare(48000.,128,state.getData(),static_cast<int>(state.getSize())-1));
    invalid=state;
    // Reject algorithm outside 4-way native catalog.
    invalid[12]=99;invalid[13]=0;invalid[14]=0;invalid[15]=0;
    assert(!entry->prepare(48000.,128,invalid.getData(),static_cast<int>(invalid.getSize())));
    auto edited=vocalReverbFactoryPreset(5);
    edited.values[static_cast<std::size_t>(arverb::Parameter::Mix)]=0.5f;
    edited.preset=-1;
    const auto request=typed->queueSettings(edited);
    assert(request>0 && !typed->stateSnapshotQuiescent());
    // Shared rack bypass still consumes one edit on audio callback.
    typed->consumeRealtimeEdits();
    assert(typed->appliedRequestId()==request);
    assert(typed->stateSnapshotQuiescent());
    VocalReverbSettings acknowledged{};
    assert(typed->acceptedSettings(acknowledged));
    assert(acknowledged.preset==-1);
    assert(acknowledged.values[0]==.5f);
    created->getStateInformation(state);
    auto copy=entry->prepare(48000.,128,state.getData(),static_cast<int>(state.getSize()));
    assert(copy);
    auto* copyTyped=dynamic_cast<VocalReverbProcessor*>(copy.get());
    assert(copyTyped&&copyTyped->acceptedSettings(acknowledged));
    assert(acknowledged.values[0]==.5f && acknowledged.algorithm==3);
    auto bad=edited;
    bad.values[1]=std::numeric_limits<float>::quiet_NaN();
    assert(typed->queueSettings(bad)==0);
    assert(typed->appliedRequestId()==request);
    juce::MidiBuffer midi;
    juce::AudioBuffer<float> buffer(2,128);
    double tail=0.;
    for(int block=0;block<440;++block) {
        buffer.clear();
        if(block==0) {buffer.setSample(0,0,.65f);buffer.setSample(1,0,.65f);}
        created->processBlock(buffer,midi);
        for(int ch=0;ch<2;++ch)for(int i=0;i<128;++i) {
            const float y=buffer.getSample(ch,i);
            assert(std::isfinite(y) && std::abs(y)<12.f);
            if(block>15)tail+=static_cast<double>(y)*y;
        }
    }
    assert(tail>1.e-8 && typed->processedBlocks()==440);
    // Fail-closed state after seal: an audio-owned FX cannot be mutated by
    // old session bytes while published.
    typed->sealForRealtime();
    assert(!typed->restoreUnpublishedState(state.getData(),static_cast<int>(state.getSize())));
    // Zero added sample latency is distinct from intentionally delayed WET
    // reflections: 0% Mix must pass the very first dry sample unchanged.
    auto zeroMix=vocalReverbFactoryPreset(0);
    zeroMix.values[static_cast<std::size_t>(arverb::Parameter::Mix)]=0.f;
    zeroMix.preset=-1;
    auto drySeed=entry->prepare(48000.,64,nullptr,0);
    auto* drySeedTyped=dynamic_cast<VocalReverbProcessor*>(drySeed.get());
    assert(drySeedTyped);
    assert(drySeedTyped->queueSettings(zeroMix)>0);
    drySeedTyped->consumeRealtimeEdits();
    juce::MemoryBlock dryState;
    drySeed->getStateInformation(dryState);
    auto coldDry=entry->prepare(48000.,64,dryState.getData(),
                                static_cast<int>(dryState.getSize()));
    assert(coldDry && coldDry->getLatencySamples()==0);
    juce::AudioBuffer<float> dryPcm(2,64);
    dryPcm.clear();
    dryPcm.setSample(0,0,.7f);
    dryPcm.setSample(1,0,-.5f);
    coldDry->processBlock(dryPcm,midi);
    assert(std::fabs(dryPcm.getSample(0,0)-.7f)<2.e-6f);
    assert(std::fabs(dryPcm.getSample(1,0)+.5f)<2.e-6f);
    for(int i=1;i<64;++i) {
        assert(std::fabs(dryPcm.getSample(0,i))<1.e-6f);
        assert(std::fabs(dryPcm.getSample(1,i))<1.e-6f);
    }

    // Each singing preset must be an ACTUAL native C++ stereo wet tail.
    // Stage the preset BEFORE prepare() so its first ASIO callback does not
    // need to regenerate expensive reverb coefficients.
    for(int program=0;program<reverbSisPresetCount;++program) {
        auto seed=entry->prepare(48000.,64,nullptr,0);
        auto* seedTyped=dynamic_cast<VocalReverbProcessor*>(seed.get());
        assert(seedTyped);
        auto preset=vocalReverbFactoryPreset(program);
        preset.values[static_cast<std::size_t>(arverb::Parameter::Mix)]=1.f;
        preset.preset=-1;
        assert(seedTyped->queueSettings(preset)>0);
        seedTyped->consumeRealtimeEdits();
        juce::MemoryBlock wetState;
        seed->getStateInformation(wetState);
        auto wet=entry->prepare(48000.,64,wetState.getData(),
                                static_cast<int>(wetState.getSize()));
        assert(wet && wet->getLatencySamples()==0);
        juce::AudioBuffer<float> p(2,128);
        double eLeft=0.,eRight=0.,eFold=0.;
        for(int block=0;block<256;++block) {
            p.clear();
            if(block==0) {p.setSample(0,0,.8f);p.setSample(1,0,.8f);}
            wet->processBlock(p,midi);
            if(block==0) {
                // 100% wet with nonzero pre-delay: no dry first-sample leak.
                assert(std::fabs(p.getSample(0,0))<1.e-6f);
                assert(std::fabs(p.getSample(1,0))<1.e-6f);
            }
            for(int j=0;j<128;++j) {
                const double a=p.getSample(0,j),b=p.getSample(1,j);
                assert(std::isfinite(a)&&std::isfinite(b));
                assert(std::fabs(a)<12. && std::fabs(b)<12.);
                if(block>32) {eLeft+=a*a;eRight+=b*b;eFold+=(a+b)*(a+b)*.25;}
            }
        }
        assert(eLeft>1.e-8 && eRight>1.e-8 && eFold>1.e-9);
        assert(eFold>.08*.5*(eLeft+eRight)); // no predominantly antiphase tail
    }

    // Native rack bypass must NOT freeze an audible room tail. Drain only
    // after the fade finishes, feeding silence into the same FDN, then idle
    // with zero DSP work. No ring-buffer reset spike in the PCM callback.
    const auto beforeDrain=typed->processedBlocks();
    typed->onRealtimeBypass(true,true);
    for(int i=0;i<4000;++i) {
        buffer.clear();
        created->processBlock(buffer,midi);
    }
    const auto parked=typed->processedBlocks();
    assert(parked>beforeDrain);
    for(int i=0;i<20;++i) {
        buffer.clear();
        created->processBlock(buffer,midi);
    }
    assert(typed->processedBlocks()==parked);
    // Simulate the SAME FxInstance full-bypass contract: exactly one
    // mailbox edit is ACKed by the host hook per callback, not a hidden
    // second pop from the continuous tail-rendering processBlock().
    auto updateA=vocalReverbFactoryPreset(1),updateB=vocalReverbFactoryPreset(2);
    const auto idA=typed->queueSettings(updateA);
    const auto idB=typed->queueSettings(updateB);
    assert(idA && idB>idA && !typed->stateSnapshotQuiescent());
    typed->consumeRealtimeEdits();
    buffer.clear();
    created->processBlock(buffer,midi);
    // Latest complete intent wins. No stale preset A is rendered first.
    assert(typed->appliedRequestId()==idB);
    assert(typed->stateSnapshotQuiescent());
    typed->consumeRealtimeEdits();
    buffer.clear();
    created->processBlock(buffer,midi);
    assert(typed->appliedRequestId()==idB);
    assert(typed->stateSnapshotQuiescent());
    typed->onRealtimeBypass(false,false);
    float worstGhost=0.f;
    for(int i=0;i<40;++i) {
        buffer.clear();
        created->processBlock(buffer,midi);
        for(int sample=0;sample<128;++sample)
            worstGhost=std::max(worstGhost,
                std::max(std::fabs(buffer.getSample(0,sample)),
                         std::fabs(buffer.getSample(1,sample))));
    }
    assert(worstGhost<1.e-3f);

    // Hidden tank regression: dry-only output while bypassed is silent,
    // but the wet network may still contain the last phrase. Do not park
    // just because the post-mix measurement is zero.
    auto* hidden=dynamic_cast<VocalReverbProcessor*>(coldDry.get());
    assert(hidden);
    for(int i=0;i<64;++i) {
        dryPcm.clear();
        dryPcm.setSample(0,0,.6f);
        dryPcm.setSample(1,0,.6f);
        coldDry->processBlock(dryPcm,midi);
    }
    hidden->onRealtimeBypass(true,true);
    for(int i=0;i<800;++i) {
        dryPcm.clear();
        coldDry->processBlock(dryPcm,midi);
    }
    const auto hiddenDraining=hidden->processedBlocks();
    dryPcm.clear();
    coldDry->processBlock(dryPcm,midi);
    assert(hidden->processedBlocks()==hiddenDraining+1); // >1s, still draining
    for(int i=0;i<5000;++i) {
        dryPcm.clear();
        coldDry->processBlock(dryPcm,midi);
    }
    const auto hiddenParked=hidden->processedBlocks();
    dryPcm.clear();
    coldDry->processBlock(dryPcm,midi);
    assert(hidden->processedBlocks()==hiddenParked);

    // True zero-cost bypass before first audio render: initial BYPASS does
    // not start a needless empty 8-line delay network on a live mixer.
    auto coldBypass=entry->prepare(48000.,64,nullptr,0);
    auto* coldTyped=dynamic_cast<VocalReverbProcessor*>(coldBypass.get());
    assert(coldTyped);
    coldTyped->onRealtimeBypass(true,true);
    dryPcm.clear();
    coldBypass->processBlock(dryPcm,midi);
    assert(coldTyped->processedBlocks()==0);
    coldTyped->onRealtimeBypass(false,false);

    // Prepare/automation smoke across common monitoring sample rates;
    // the native FX cannot secretly add a plugin delay or create a second
    // PCM processing pipeline.
    for(const double rate : {44100.,48000.,96000.,192000.}) {
        auto instance=entry->prepare(rate,32,nullptr,0);
        assert(instance && instance->getLatencySamples()==0);
        juce::AudioBuffer<float> small(2,32);
        for(int block=0;block<32;++block) {
            small.clear();
            if(block==0)small.setSample(0,0,.5f);
            instance->processBlock(small,midi);
            for(int ch=0;ch<2;++ch)for(int j=0;j<32;++j)
                assert(std::isfinite(small.getSample(ch,j)));
        }
    }
    return 0;
}
