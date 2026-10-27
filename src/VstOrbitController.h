// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "../engine/core/OrbitPhase.h"
#include "../engine/core/OrbitControlPolicy.h"
#include "../engine/core/OrbitMotion.h"
#include <cmath>
#include <utility>

// Only the JUCE message thread owns the vendor parameter gestures and timer.
// No WebView frame or DSP/audio callback owns VST3 parameter automation.
class VstOrbitController final : private juce::Timer
{
public:
    ~VstOrbitController() override { stop(); }

    juce::String start(juce::AudioPluginInstance* plugin,int slot,int azimuthIndex,
                       double seconds,bool reverse,int shape=0,
                       int distanceIndex=-1,int elevationIndex=-1,
                       double nearDistance=0.2,double farDistance=0.7)
    {
        if(!plugin) return "VST3 processor is no longer available";
        if(!std::isfinite(seconds)||seconds<2.0||seconds>180.0)
            return "Revolution time must be 2–180 seconds";
        if(!armix::core::validOrbitShape(shape)) return "Unknown motion path";
        if(!std::isfinite(nearDistance)||!std::isfinite(farDistance)||
           nearDistance<0.0||farDistance>1.0||nearDistance>farDistance)
            return "Invalid Near/Far distance setpoints";
        auto& params=plugin->getParameters();
        auto findParam=[&](int index)->juce::AudioProcessorParameter* {
            return index>=0&&index<params.size()?params[index]:nullptr;
        };
        auto* az=findParam(azimuthIndex);
        auto* distance=distanceIndex>=0?findParam(distanceIndex):nullptr;
        auto* elevation=armix::core::needsOrbitElevation(shape)?findParam(elevationIndex):nullptr;
        auto validParam=[](juce::AudioProcessorParameter* p) {
            return p&&p->isAutomatable()&&p->getNumSteps()>=72 &&
                   std::isfinite(p->getValue())&&p->getValue()>=0.f&&p->getValue()<=1.f;
        };
        if(!validParam(az)) return "Choose a high-resolution automatable Azimuth parameter";
        if((armix::core::needsOrbitDistance(shape)||distanceIndex>=0) &&
           (!validParam(distance)||distance==az))
            return "Ellipse needs a distinct automatable Distance parameter";
        if(armix::core::needsOrbitElevation(shape) &&
           (!validParam(elevation)||elevation==az||elevation==distance))
            return "Rise/fall needs a distinct automatable Elevation parameter";
        const auto first=az->getValue();
        const auto startPhase=armix::core::orbitShapePhaseForAzimuth(first,shape);
        const float startDist=distance?distance->getValue():0.5f;
        const float startElev=elevation?elevation->getValue():0.5f;
        stop(); // only after every replacement parameter is checked
        processor_=plugin;
        azimuth_=az;distance_=distance;elevation_=elevation;
        slot_=slot;azimuthIndex_=azimuthIndex;distanceIndex_=distanceIndex;
        elevationIndex_=elevationIndex;shape_=shape;period_=seconds;reverse_=reverse;
        nearDistance_=nearDistance;farDistance_=farDistance;
        phaseStart_=static_cast<float>(startPhase);
        lastAz_=first;lastDist_=startDist;lastElev_=startElev;
        initialDistance_=startDist;initialElevation_=startElev;
        azimuthSteps_=az->getNumSteps();
        dispatch_={};
        startMs_=juce::Time::getMillisecondCounterHiRes();
        previousTickMs_=startMs_;
        previousAbsolutePhase_=startPhase;
        azimuth_->beginChangeGesture();
        if(distance_)distance_->beginChangeGesture();
        if(elevation_)elevation_->beginChangeGesture();
        startTimerHz(60);
        return {};
    }

    void stop()
    {
        stopTimer();
        if(azimuth_)std::exchange(azimuth_,nullptr)->endChangeGesture();
        if(distance_)std::exchange(distance_,nullptr)->endChangeGesture();
        if(elevation_)std::exchange(elevation_,nullptr)->endChangeGesture();
        processor_=nullptr;slot_=-1;azimuthIndex_=-1;distanceIndex_=-1;
        elevationIndex_=-1;
    }
    void pauseForGraphEdit() { if(active())stopTimer(); }
    void resumeAfterGraphEdit() { if(active())startTimerHz(60); }
    // Existing processor stays stable when another FX is appended, moved, or removed.
    void rebindSlot(int slot) noexcept { if(active())slot_=slot; }

    bool active() const noexcept { return azimuth_!=nullptr; }
    int slot() const noexcept { return slot_; }
    juce::AudioPluginInstance* processor() const noexcept { return processor_; }
    int parameterIndex() const noexcept { return azimuthIndex_; }
    int distanceParameterIndex() const noexcept { return distanceIndex_; }
    int elevationParameterIndex() const noexcept { return elevationIndex_; }
    int shape() const noexcept { return shape_; }
    double nearDistance() const noexcept { return nearDistance_; }
    double farDistance() const noexcept { return farDistance_; }
    double secondsPerRevolution() const noexcept { return period_; }
    bool reverse() const noexcept { return reverse_; }
    const armix::core::OrbitDispatchStats& stats() const noexcept { return dispatch_; }

    double phase() const noexcept
    {
        return active()?armix::core::orbitRamp(
            phaseStart_,(juce::Time::getMillisecondCounterHiRes()-startMs_)/1000.0,
            period_,reverse_):phaseStart_;
    }
private:
    void timerCallback() override
    {
        if(!active())return;
        const auto now=juce::Time::getMillisecondCounterHiRes();
        dispatch_.observe(now-previousTickMs_);
        previousTickMs_=now;
        ++dispatch_.ticks;
        const double elapsed=(now-startMs_)/1000.0;
        const double absolutePhase=static_cast<double>(phaseStart_) +
            (reverse_?-1.0:1.0)*elapsed/period_;
        const auto pos=armix::core::orbitMotionPoint(
            armix::core::orbitWrap(absolutePhase),shape_,nearDistance_,farDistance_);
        float appliedDistance=pos.distance;
        bool forceDistanceEndpoint=false;
        if(distance_ && elapsed>=0.3) {
            if(armix::core::crossesOrbitDistanceExtreme(previousAbsolutePhase_,
                absolutePhase,shape_,true)){
                appliedDistance=static_cast<float>(farDistance_);
                forceDistanceEndpoint=true;
            }else if(armix::core::crossesOrbitDistanceExtreme(previousAbsolutePhase_,
                absolutePhase,shape_,false)){
                appliedDistance=static_cast<float>(nearDistance_);
                forceDistanceEndpoint=true;
            }
        }
        previousAbsolutePhase_=absolutePhase;
        const auto publish=[&](juce::AudioProcessorParameter* p,float value,
                               float& previous,bool cyclic,int steps,bool force=false){
            if(!p)return;
            const bool needed=force||(cyclic ?
                armix::core::shouldPublishOrbit(value,previous,steps):
                std::abs(value-previous)>0.0001f);
            if(needed && std::isfinite(value) && value>=0.f && value<=1.f){
                p->setValueNotifyingHost(value);previous=value;++dispatch_.writes;
            } else ++dispatch_.coalesced;
        };
        publish(azimuth_,pos.azimuth,lastAz_,true,azimuthSteps_);
        // 300 ms easing prevents jumps when enabling additional axes.
        const float mix=static_cast<float>(std::clamp(elapsed/0.3,0.0,1.0));
        if(distance_)publish(distance_,initialDistance_+
            mix*(appliedDistance-initialDistance_),lastDist_,false,0,
            forceDistanceEndpoint);
        if(elevation_)publish(elevation_,initialElevation_+
            mix*(pos.elevation-initialElevation_),lastElev_,false,0);
    }

    juce::AudioPluginInstance* processor_=nullptr; // borrowed, stable while its FX exists
    juce::AudioProcessorParameter* azimuth_=nullptr;
    juce::AudioProcessorParameter* distance_=nullptr;
    juce::AudioProcessorParameter* elevation_=nullptr;
    int slot_=-1,azimuthIndex_=-1,distanceIndex_=-1,elevationIndex_=-1;
    int shape_=0,azimuthSteps_=0;
    double period_=20.0,startMs_=0.0,previousTickMs_=0.0;
    double nearDistance_=0.2,farDistance_=0.7,previousAbsolutePhase_=0.0;
    float phaseStart_=0.f,lastAz_=0.5f,lastDist_=0.5f,lastElev_=0.5f;
    float initialDistance_=0.5f,initialElevation_=0.5f;
    bool reverse_=false;
    armix::core::OrbitDispatchStats dispatch_;
};
