// SPDX-License-Identifier: AGPL-3.0-only
// IO-0 physical dual-vendor ASIO compatibility probe, NOT the ArSonMix mixer.
// This executable never renders DSP/VST3, never changes the product session,
// and never routes secondary capture to a speaker. Its explicit primary
// callback writes only zeroes. Run only with owner-approved disconnected or
// muted speakers and a deliberate test input signal on ASIO B Input 1.
//
// Normal CTest --self-test performs NO hardware enumeration or driver opens.
// Explicit manual: ARMixDualAsioCaptureProbe --probe "Vendor A ASIO" "Vendor B ASIO" 48000 256 5000
#include "../engine/device/DualAsioCaptureProbePolicy.h"
#include "../engine/device/AsioRegistryIdentityProbe.h"
#include "../engine/device/AsioPriorityDeviceManager.h"
#include <juce_audio_devices/juce_audio_devices.h>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

namespace
{
using Policy=armix::device::DualAsioCaptureProbePolicy;
struct CaptureCounters final : juce::AudioIODeviceCallback
{
    bool primary=false;
    int expectedInputs=0,expectedOutputs=0;
    std::atomic<std::uint64_t> calls{0},frames{0},nonzero{0},nonfinite{0};
    std::atomic<bool> violated{false};
    explicit CaptureCounters(bool isPrimary) : primary(isPrimary) {}
    void audioDeviceAboutToStart(juce::AudioIODevice*) override {}
    void audioDeviceStopped() override {}
    void audioDeviceError(const juce::String&) override
    {
        violated.store(true,std::memory_order_release);
    }
    void audioDeviceIOCallbackWithContext(
        const float* const* input,int inputs,float* const* output,int outputs,
        int samples,const juce::AudioIODeviceCallbackContext&) override
    {
        // Never drive hardware, even on the explicitly opened primary.
        for(int c=0;c<outputs;++c)
            if(output && output[c]) juce::FloatVectorOperations::clear(output[c],samples);
        if(samples<=0 || inputs!=expectedInputs || outputs!=expectedOutputs)
        {
            violated.store(true,std::memory_order_release);
            return;
        }
        calls.fetch_add(1,std::memory_order_relaxed);
        frames.fetch_add(static_cast<std::uint64_t>(samples),std::memory_order_relaxed);
        if(primary) return; // zero-output capture producer, NO graph or DSP
        std::uint64_t live=0,bad=0;
        for(int c=0;c<inputs;++c)
        {
            const auto* data=input?input[c]:nullptr;
            if(!data){violated.store(true,std::memory_order_release);return;}
            for(int i=0;i<samples;++i)
            {
                const auto sample=data[i];
                if(!std::isfinite(sample)) ++bad;
                else if(std::abs(sample)>0.0001f) ++live;
            }
        }
        if(live)nonzero.fetch_add(live,std::memory_order_relaxed);
        if(bad)nonfinite.fetch_add(bad,std::memory_order_relaxed);
    }
};

struct Endpoint final
{
    armix::device::AsioPriorityDeviceManager discoveryOnly; // never initialise / open defaults
    juce::OwnedArray<juce::AudioIODeviceType> factories;
    std::unique_ptr<juce::AudioIODevice> device;
    CaptureCounters counters;
    explicit Endpoint(bool primary) : counters(primary) {}
    ~Endpoint()
    {
        if(device){device->stop();device->close();device.reset();}
        // Factories outlive their created devices.
    }
    juce::String openExact(const juce::String& selected, bool primary,
                           double rate,int frames)
    {
        discoveryOnly.createAudioDeviceTypes(factories);
        juce::AudioIODeviceType* asio=nullptr;
        for(auto* t:factories) if(t->getTypeName()=="ASIO"){asio=t;break;}
        if(!asio)return "Pinned JUCE build has no ASIO device type";
        asio->scanForDevices(); // CONTROL, before opening; never on callback
        if(!asio->getDeviceNames(true).contains(selected) ||
           (primary&&!asio->getDeviceNames(false).contains(selected)))
            return "Selected exact ASIO driver is absent from input/output inventory";
        // For capture B, the OUTPUT name is deliberately EMPTY, never an
        // implicit default speaker. Some ASIO vendors reject this outright:
        // return REJECT, do NOT invent a dummy output or reopen another driver.
        // JUCE createDevice signature is (OUTPUT name, INPUT name).
        // B MUST have no output device even during construction.
        device.reset(asio->createDevice(primary?selected:juce::String(),selected));
        if(!device)return "JUCE ASIO refused capture-only device construction";
        juce::BigInteger in,out;
        in.setRange(0,primary?2:1,true);
        if(primary)out.setRange(0,2,true);
        const auto error=device->open(in,out,rate,frames);
        if(error.isNotEmpty())return error;
        if(!device->isOpen()||device->getName()!=selected)
            return "ASIO device opened a different physical driver name";
        if(device->getActiveInputChannels()!=in||
           device->getActiveOutputChannels()!=out)
            return "ASIO driver renegotiated physical input or output pin mask";
        if(device->getCurrentSampleRate()<8000. ||
           device->getCurrentBufferSizeSamples()<=0)
            return "ASIO negotiated an invalid rate or period";
        counters.expectedInputs=primary?2:1;
        counters.expectedOutputs=primary?2:0;
        return {};
    }
    void start() { if(device)device->start(&counters); }
    bool playing() const {return device&&device->isOpen()&&device->isPlaying();}
};

std::uint64_t maskOf(const juce::BigInteger& mask) noexcept
{
    // IO-0 admits only the first 32 ASIO pins; no unsafe truncation of a
    // higher sparse physical mask. The fixture checks exact masks below.
    if(mask.getHighestBit()>=32)return UINT64_MAX;
    return static_cast<std::uint32_t>(mask.getBitRangeAsInt(0,32));
}

int probe(const char* primaryLabel,const char* captureLabel,double rate,int frames,
          int durationMs)
{
#if !defined(_WIN32) || !JUCE_ASIO
    (void)primaryLabel;(void)captureLabel;(void)rate;(void)frames;(void)durationMs;
    std::fputs("IO0_FAIL: pinned Windows JUCE ASIO backend unavailable\n",stderr);
    return 3;
#else
    // All registry, device enumeration and JUCE open/close calls are CONTROL.
    if(!std::isfinite(rate)||rate<8000.||rate>192000.||frames<32||
       frames>8192||durationMs<1000||durationMs>30000)
        return 2;
    const juce::String primaryName=juce::String::fromUTF8(primaryLabel);
    const juce::String captureName=juce::String::fromUTF8(captureLabel);
    const auto primaryReg=armix::device::AsioRegistryIdentityProbe::resolve(
        std::wstring(primaryName.toWideCharPointer())).registration;
    const auto captureReg=armix::device::AsioRegistryIdentityProbe::resolve(
        std::wstring(captureName.toWideCharPointer())).registration;
    const std::wstring primaryWide(primaryName.toWideCharPointer());
    const std::wstring captureWide(captureName.toWideCharPointer());
    Policy::Request request{primaryWide,captureWide,
        primaryReg.identity,captureReg.identity,3,3,1};
    const auto initial=Policy::preflight(request);
    if(!primaryReg.ok()||!captureReg.ok()||initial!=Policy::Verdict::ok)
    {
        std::fprintf(stderr,"IO0_REJECT preflight=%s primary=%s capture=%s\n",
            Policy::reason(initial),
            armix::device::AsioRegistrationPolicy::reason(primaryReg.error),
            armix::device::AsioRegistrationPolicy::reason(captureReg.error));
        return 3;
    }
    Endpoint primary(true),capture(false);
    auto err=primary.openExact(primaryName,true,rate,frames);
    if(err.isNotEmpty()){std::fprintf(stderr,"IO0_PRIMARY_REJECT %s\n",err.toRawUTF8());return 3;}
    // Open primary FIRST, then B while A is still actually open. The primary
    // callback is silence-only, not a second mixer render implementation.
    primary.start();
    if(!primary.playing())
    {std::fputs("IO0_PRIMARY_START_REJECT\n",stderr);return 3;}
    err=capture.openExact(captureName,false,rate,frames);
    if(err.isNotEmpty()){std::fprintf(stderr,"IO0_CAPTURE_REJECT %s\n",err.toRawUTF8());return 3;}
    const auto primaryRegAfter=armix::device::AsioRegistryIdentityProbe::resolve(
        std::wstring(primaryName.toWideCharPointer())).registration;
    const auto captureRegAfter=armix::device::AsioRegistryIdentityProbe::resolve(
        std::wstring(captureName.toWideCharPointer())).registration;
    if(!primaryRegAfter.ok()||!captureRegAfter.ok()||
       !primaryReg.identity.sameDriverClass(primaryRegAfter.identity)||
       !captureReg.identity.sameDriverClass(captureRegAfter.identity))
    {std::fputs("IO0_REJECT ASIO registry class changed while opening\n",stderr);return 3;}
    capture.start();
    if(!capture.playing())
    {std::fputs("IO0_CAPTURE_START_REJECT\n",stderr);return 3;}
    juce::Thread::sleep(durationMs); // CONTROL only, never the hardware callback
    auto* a=primary.device.get();
    auto* b=capture.device.get();
    const std::wstring openedPrimary(a->getName().toWideCharPointer());
    const std::wstring openedCapture(b->getName().toWideCharPointer());
    const Policy::Readback readback{
        openedPrimary,openedCapture,
        a->getCurrentSampleRate(),b->getCurrentSampleRate(),
        a->getCurrentBufferSizeSamples(),b->getCurrentBufferSizeSamples(),
        maskOf(a->getActiveInputChannels()),maskOf(a->getActiveOutputChannels()),
        maskOf(b->getActiveInputChannels()),maskOf(b->getActiveOutputChannels())};
    const auto verified=Policy::verify(request,readback);
    const auto aCalls=primary.counters.calls.load();
    const auto bCalls=capture.counters.calls.load();
    const auto bSignal=capture.counters.nonzero.load();
    const bool ok=verified==Policy::Verdict::ok&&
        primary.playing()&&capture.playing()&&aCalls>0&&bCalls>0&&
        bSignal>0&&capture.counters.nonfinite.load()==0&&
        !primary.counters.violated.load()&&!capture.counters.violated.load();
    std::printf("IO0_RESULT=%s READBACK=%s PRIMARY_RATE=%.1f CAPTURE_RATE=%.1f "
        "PRIMARY_PERIOD=%d CAPTURE_PERIOD=%d PRIMARY_CALLBACKS=%llu "
        "CAPTURE_CALLBACKS=%llu CAPTURE_NONZERO_SAMPLES=%llu "
        "CAPTURE_OUTPUT_MASK=%llu DSP_PROCESSED=0\n",
        ok?"PHYSICAL_PROBE_PASS":"PHYSICAL_PROBE_FAIL",Policy::reason(verified),
        readback.primaryRate,readback.captureRate,
        readback.primaryPeriod,readback.capturePeriod,
        static_cast<unsigned long long>(aCalls),
        static_cast<unsigned long long>(bCalls),
        static_cast<unsigned long long>(bSignal),
        static_cast<unsigned long long>(readback.captureOutputMask));
    return ok?0:4;
#endif
}
} // namespace

int main(int argc,char** argv)
{
    // Self-test is deliberately hardware-free; CTest NEVER attempts an
    // unknown ASIO device or opens real speakers on an unattended runner.
    if(argc==2&&std::string(argv[1])=="--self-test")
    {
        const Policy::Identity a{L"A",L"{11111111-1111-1111-1111-111111111111}"};
        const Policy::Identity b{L"B",L"{22222222-2222-2222-2222-222222222222}"};
        const Policy::Request r{L"A ASIO",L"B ASIO",a,b,3,3,1};
        if(Policy::preflight(r)!=Policy::Verdict::ok||
           Policy::preflight({L"A ASIO",L"B ASIO",a,a})!=
               Policy::Verdict::sameDriverClass)
            return 5;
        std::puts("IO0_SELF_TEST_PASS: no physical device opened");
        return 0;
    }
    if(argc!=7||std::string(argv[1])!="--probe")
    {
        std::fputs("Usage: ARMixDualAsioCaptureProbe --self-test OR\n"
            "  --probe \"Primary ASIO name\" \"Secondary ASIO name\" "
            "48000 256 5000\n"
            "WARNING: owner-authorized physical rig only; outputs sent zeros.\n",stderr);
        return 2;
    }
    const double rate=std::strtod(argv[4],nullptr);
    const int frames=std::atoi(argv[5]);
    const int ms=std::atoi(argv[6]);
    // ASIO uses JUCE message-window/COM lifecycle for reset notifications.
    // The attended physical probe initialises it on CONTROL only; --self-test
    // deliberately never creates a MessageManager or opens a sound device.
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    return probe(argv[2],argv[3],rate,frames,ms);
}
