// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "BuiltInFxCatalog.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>

// Channel strip EQ for the main native mixer ONLY. Deliberately no JUCE,
// VST3, ArSonEQ, FFT, WebView2, heap allocation or audio callback ownership.
// prepare()/setBand() operate on UNPUBLISHED instances (or in offline tests).
// For live edits, prepareBandEdit() performs all trig on CONTROL; applyPreparedBand()
// performs only bounded copies on the exclusive audio owner at a block boundary.
// No state mutation may race process(), and no UI thread reads live histories.
namespace armix::builtin_fx {
struct Band {
    bool enabled = false;
    float frequencyHz = 1000.0f;
    float gainDb = 0.0f;
    float q = 0.70710678f;
    Slope slope = Slope::db12; // HPF/LPF only; other types ignore it.
};

class ChannelEq final {
public:
    static constexpr std::size_t bandCount = channelEqSlots.size();
    static constexpr float maxGainDb = 18.0f;
    static constexpr float minQ = 0.2f;
    static constexpr float maxQ = 12.0f;

    ChannelEq() noexcept {
        for (std::size_t i = 0; i < bandCount; ++i) {
            current_.settings[i].frequencyHz = channelEqSlots[i].defaultHz;
            current_.settings[i].q = channelEqSlots[i].kind == FilterKind::bell ? 1.0f : 0.70710678f;
        }
    }

    // CONTROL creates a precomputed change without accessing a live EQ.
    // A bounded queue/immutable handoff outside this module transports it to
    // the audio owner. sampleRate acts as a device-epoch-independent fence;
    // the mixer adapter must ALSO validate ChannelId, epoch, and revision.
    struct PreparedBandEdit final {
        Band band{};
        std::size_t index=bandCount;
        double sampleRate=0.0;
        std::array<std::array<double,5>,2> coefficients{}; // b0,b1,b2,a1,a2
        std::uint8_t sections=0;
        bool valid=false;
    };

    static bool prepareBandEdit(std::size_t index, const Band& band,
                                double sampleRate, PreparedBandEdit& output) noexcept {
        if (!std::isfinite(sampleRate) || sampleRate < 8000.0 ||
            sampleRate > 384000.0 ||
            !validBand(index,band,static_cast<float>(std::min(20000.0,sampleRate*0.45))))
            return false;
        Slot compiled{};
        if (band.enabled && !makeSlot(channelEqSlots[index].kind,band,sampleRate,compiled))
            return false;
        PreparedBandEdit candidate{};
        candidate.band=band;
        candidate.index=index;
        candidate.sampleRate=sampleRate;
        candidate.sections=compiled.count;
        for (std::uint8_t i=0;i<compiled.count;++i) {
            const auto& s=compiled.section[i];
            candidate.coefficients[i]={s.b0,s.b1,s.b2,s.a1,s.a2};
        }
        candidate.valid=true;
        output=candidate;
        return true;
    }

    // AUDIO OWNER: no trig, allocations, locks, plugin work or OS calls.
    // Apply only one prevalidated control packet at a real block boundary.
    bool applyPreparedBand(const PreparedBandEdit& edit) noexcept {
        if (!prepared_ || !edit.valid || edit.index>=bandCount ||
            edit.sampleRate!=sampleRate_ ||
            !validBand(edit.index,edit.band,maxFrequencyHz())) return false;
        const auto kind=channelEqSlots[edit.index].kind;
        const bool cut=kind==FilterKind::highPass||kind==FilterKind::lowPass;
        const std::uint8_t expected=edit.band.enabled ?
            static_cast<std::uint8_t>(cut && edit.band.slope==Slope::db24 ? 2 : 1) : 0;
        if (edit.sections!=expected) return false;
        Slot newSlot{};
        newSlot.count=expected;
        for(std::uint8_t i=0;i<expected;++i) {
            const auto& values=edit.coefficients[i];
            for(double v:values) if(!std::isfinite(v))return false;
            auto& c=newSlot.section[i];
            c.b0=values[0];c.b1=values[1];c.b2=values[2];
            c.a1=values[3];c.a2=values[4];
        }
        const auto& old=current_.settings[edit.index];
        if (old.enabled==edit.band.enabled && old.frequencyHz==edit.band.frequencyHz &&
            old.gainDb==edit.band.gainDb && old.q==edit.band.q &&
            old.slope==edit.band.slope) return true;
        Bank next=current_;
        next.settings[edit.index]=edit.band;
        next.slots[edit.index]=newSlot;
        next.activeCount=0;
        for (std::size_t i=0;i<bandCount;++i)
            if(next.settings[i].enabled)
                next.active[next.activeCount++]=static_cast<std::uint8_t>(i);
        previous_=current_;
        current_=next;
        fadeRemaining_=fadeLength_;
        return true;
    }

    // Prepares without allocating. Enabled filters above the new Nyquist guard
    // cause rejection (caller must explicitly choose a safe setting).
    bool prepare(double sampleRate) noexcept {
        if (!std::isfinite(sampleRate) || sampleRate < 8000.0 || sampleRate > 384000.0)
            return false;
        const float maxHz = static_cast<float>(std::min(20000.0, sampleRate * 0.45));
        Bank next = current_;
        for (std::size_t i = 0; i < bandCount; ++i) {
            auto& band = next.settings[i];
            if (!band.enabled) band.frequencyHz = std::min(band.frequencyHz, maxHz);
            if (!validBand(i, band, maxHz)) return false;
        }
        next.activeCount = 0;
        for (std::size_t i = 0; i < bandCount; ++i) {
            next.slots[i] = Slot{};
            if (next.settings[i].enabled) {
                if (!makeSlot(channelEqSlots[i].kind, next.settings[i], sampleRate, next.slots[i]))
                    return false;
                next.active[next.activeCount++] = static_cast<std::uint8_t>(i);
            }
        }
        sampleRate_ = sampleRate;
        current_ = next;
        previous_ = Bank{};
        fadeLength_ = std::max(1, static_cast<int>(std::round(sampleRate * 0.005)));
        fadeRemaining_ = 0;
        prepared_ = true;
        return true;
    }

    // Offline/unpublished convenience, NOT for the live audio callback:
    // this computes trigonometric coefficients. Use prepareBandEdit() on the
    // CONTROL thread and applyPreparedBand() on AUDIO for live automation.
    bool setBand(std::size_t index, const Band& band) noexcept {
        if(!prepared_)return false;
        PreparedBandEdit edit{};
        return prepareBandEdit(index,band,sampleRate_,edit) && applyPreparedBand(edit);
    }

    bool resetToFlat() noexcept {
        if (!prepared_) return false;
        if (current_.activeCount == 0) return true;
        previous_ = current_;
        for (std::size_t i = 0; i < bandCount; ++i) {
            current_.settings[i].enabled = false;
            current_.slots[i] = Slot{};
        }
        current_.activeCount = 0;
        fadeRemaining_ = fadeLength_;
        return true;
    }

    [[nodiscard]] Band band(std::size_t index) const noexcept {
        return index < bandCount ? current_.settings[index] : Band{};
    }
    [[nodiscard]] bool isPrepared() const noexcept { return prepared_; }
    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] float maxFrequencyHz() const noexcept {
        return static_cast<float>(std::min(20000.0, sampleRate_ * 0.45));
    }
    [[nodiscard]] std::size_t activeBands() const noexcept { return current_.activeCount; }
    [[nodiscard]] int addedLatencySamples() const noexcept { return 0; }
    [[nodiscard]] bool transitioning() const noexcept {
        return fadeRemaining_>0 || externalFadeRemaining_>0;
    }
    // AUDIO-owner-only smooth gate, preserving individual band settings.
    void setExternalBypass(bool bypass) noexcept {
        const float target=bypass?0.0f:1.0f;
        if(target==externalTarget_)return;
        externalTarget_=target;
        if(current_.activeCount==0 && fadeRemaining_==0) {
            externalWet_=target;externalFadeRemaining_=0;
        } else externalFadeRemaining_=fadeLength_;
    }

    // Processing and all biquad histories are audio-owner exclusive.
    // left==right is explicitly one mono buffer, never double processed.
    void processStereo(float* left, float* right, std::size_t frames) noexcept {
        if (!prepared_ || !left || !right || frames == 0) return;
        if (left == right) { processMono(left, frames); return; }
        if (current_.activeCount==0 && fadeRemaining_==0) {
            externalWet_=externalTarget_;externalFadeRemaining_=0;
            return; // exact bitwise bypass
        }

        for (std::size_t i = 0; i < frames; ++i) {
            const float dryL = left[i], dryR = right[i];
            const float wetL = render(current_, 0, dryL), wetR = render(current_, 1, dryR);
            if (fadeRemaining_ > 0) {
                const float oldL = render(previous_, 0, dryL);
                const float oldR = render(previous_, 1, dryR);
                const float mix = 1.0f - static_cast<float>(fadeRemaining_ - 1) /
                                               static_cast<float>(fadeLength_);
                const float processedL=oldL+(wetL-oldL)*mix;
                const float processedR=oldR+(wetR-oldR)*mix;
                const float gate=nextExternalMix();
                left[i]=dryL+(processedL-dryL)*gate;
                right[i]=dryR+(processedR-dryR)*gate;
                --fadeRemaining_;
            } else {
                const float gate=nextExternalMix();
                left[i]=dryL+(wetL-dryL)*gate;
                right[i]=dryR+(wetR-dryR)*gate;
            }
        }
        clearTiny(current_);
        if (fadeRemaining_ != 0) clearTiny(previous_);
    }

    void processMono(float* samples, std::size_t frames) noexcept {
        if(!prepared_||!samples||frames==0)return;
        if(current_.activeCount==0 && fadeRemaining_==0) {
            externalWet_=externalTarget_;externalFadeRemaining_=0;return;
        }

        for (std::size_t i = 0; i < frames; ++i) {
            const float dry = samples[i];
            const float wet = render(current_, 0, dry);
            if (fadeRemaining_ > 0) {
                const float old = render(previous_, 0, dry);
                const float mix = 1.0f - static_cast<float>(fadeRemaining_ - 1) /
                                               static_cast<float>(fadeLength_);
                const float processed=old+(wet-old)*mix;
                const float gate=nextExternalMix();
                samples[i]=dry+(processed-dry)*gate;
                --fadeRemaining_;
            } else {
                const float gate=nextExternalMix();
                samples[i]=dry+(wet-dry)*gate;
            }
        }
        clearTiny(current_);
        if (fadeRemaining_ != 0) clearTiny(previous_);
    }

    // Control-side visualization only. Never call while an audio callback is
    // mutating this instance. Host should snapshot coefficients off callback.
    [[nodiscard]] double responseDb(double frequencyHz) const noexcept {
        if (!prepared_ || !std::isfinite(frequencyHz) || frequencyHz <= 0.0 ||
            frequencyHz >= sampleRate_ * 0.5) return 0.0;
        const double w = twoPi * frequencyHz / sampleRate_;
        const std::complex<double> z1 = std::polar(1.0, -w);
        const auto z2 = z1 * z1;
        double result = 0.0;
        for (std::uint8_t k = 0; k < current_.activeCount; ++k) {
            const auto& s = current_.slots[current_.active[k]];
            for (std::uint8_t j = 0; j < s.count; ++j) {
                const auto& c = s.section[j];
                const auto numerator = c.b0 + c.b1 * z1 + c.b2 * z2;
                const auto denominator = 1.0 + c.a1 * z1 + c.a2 * z2;
                const double mag = std::abs(numerator / denominator);
                result += 20.0 * std::log10(std::max(1.0e-12, mag));
            }
        }
        return std::clamp(result, -160.0, 80.0);
    }

private:
    static constexpr double twoPi = 6.283185307179586476925286766559;
    struct Section {
        double b0=1, b1=0, b2=0, a1=0, a2=0;
        double z1[2]{}, z2[2]{};
        float process(int channel, float input) noexcept {
            const double x = static_cast<double>(input);
            const double out = b0 * x + z1[channel];
            z1[channel] = b1 * x - a1 * out + z2[channel];
            z2[channel] = b2 * x - a2 * out;
            return static_cast<float>(out);
        }
        void denormalGuard() noexcept {
            for (int i = 0; i < 2; ++i) {
                if (std::abs(z1[i]) < 1.0e-24) z1[i] = 0;
                if (std::abs(z2[i]) < 1.0e-24) z2[i] = 0;
            }
        }
    };
    struct Slot { std::array<Section,2> section{}; std::uint8_t count=0; };
    struct Bank {
        std::array<Band,bandCount> settings{};
        std::array<Slot,bandCount> slots{};
        std::array<std::uint8_t,bandCount> active{};
        std::uint8_t activeCount=0;
    };
    Bank current_{}, previous_{};
    double sampleRate_ = 0.0;
    int fadeLength_ = 1, fadeRemaining_ = 0;
    float externalWet_=1.0f,externalTarget_=1.0f;
    int externalFadeRemaining_=0;
    bool prepared_ = false;
    float nextExternalMix() noexcept {
        if(externalFadeRemaining_>0) {
            externalWet_+=(externalTarget_-externalWet_)/
                          static_cast<float>(externalFadeRemaining_);
            if(--externalFadeRemaining_==0)externalWet_=externalTarget_;
        }
        return externalWet_;
    }

    static bool validBand(std::size_t i, const Band& b, float maxHz) noexcept {
        if (i >= bandCount || !std::isfinite(b.frequencyHz) ||
            !std::isfinite(b.gainDb) || !std::isfinite(b.q) ||
            b.frequencyHz < 20.0f || b.frequencyHz > maxHz ||
            b.gainDb < -maxGainDb || b.gainDb > maxGainDb ||
            b.q < minQ || b.q > maxQ) return false;
        return b.slope == Slope::db12 || b.slope == Slope::db24;
    }
    static bool normalized(Section& s, double b0, double b1, double b2,
                           double a0, double a1, double a2) noexcept {
        if (!std::isfinite(a0) || std::abs(a0) < 1.0e-12) return false;
        s.b0 = b0 / a0; s.b1 = b1 / a0; s.b2 = b2 / a0;
        s.a1 = a1 / a0; s.a2 = a2 / a0;
        return std::isfinite(s.b0) && std::isfinite(s.b1) && std::isfinite(s.b2) &&
               std::isfinite(s.a1) && std::isfinite(s.a2);
    }
    static bool makeBiquad(FilterKind kind, const Band& b, double sampleRate,
                           double q, Section& out) noexcept {
        const double omega = twoPi * b.frequencyHz / sampleRate;
        const double c = std::cos(omega), s = std::sin(omega);
        const double a = std::pow(10.0, static_cast<double>(b.gainDb) / 40.0);
        const double alpha = s / (2.0 * q);
        switch (kind) {
            case FilterKind::bell:
                return normalized(out, 1.0+alpha*a, -2.0*c, 1.0-alpha*a,
                                  1.0+alpha/a, -2.0*c, 1.0-alpha/a);
            case FilterKind::highPass:
                return normalized(out, (1+c)*0.5, -(1+c), (1+c)*0.5,
                                  1+alpha, -2*c, 1-alpha);
            case FilterKind::lowPass:
                return normalized(out, (1-c)*0.5, 1-c, (1-c)*0.5,
                                  1+alpha, -2*c, 1-alpha);
            case FilterKind::lowShelf: {
                const double edge = s * std::sqrt(a) * std::sqrt(2.0);
                return normalized(out,
                    a*((a+1)-(a-1)*c+edge), 2*a*((a-1)-(a+1)*c),
                    a*((a+1)-(a-1)*c-edge),
                    (a+1)+(a-1)*c+edge, -2*((a-1)+(a+1)*c),
                    (a+1)+(a-1)*c-edge);
            }
            case FilterKind::highShelf: {
                const double edge = s * std::sqrt(a) * std::sqrt(2.0);
                return normalized(out,
                    a*((a+1)+(a-1)*c+edge), -2*a*((a-1)+(a+1)*c),
                    a*((a+1)+(a-1)*c-edge),
                    (a+1)-(a-1)*c+edge, 2*((a-1)-(a+1)*c),
                    (a+1)-(a-1)*c-edge);
            }
        }
        return false;
    }
    static bool makeSlot(FilterKind kind, const Band& b, double rate, Slot& result) noexcept {
        const bool cut = kind == FilterKind::highPass || kind == FilterKind::lowPass;
        result.count = static_cast<std::uint8_t>(cut && b.slope == Slope::db24 ? 2 : 1);
        if (result.count == 2) {
            return makeBiquad(kind, b, rate, 0.541196100146197, result.section[0]) &&
                   makeBiquad(kind, b, rate, 1.306562964876377, result.section[1]);
        }
        return makeBiquad(kind, b, rate, cut ? 0.7071067811865476 : b.q,
                          result.section[0]);
    }
    static float render(Bank& bank, int channel, float sample) noexcept {
        for (std::uint8_t i = 0; i < bank.activeCount; ++i) {
            auto& slot = bank.slots[bank.active[i]];
            for (std::uint8_t j = 0; j < slot.count; ++j)
                sample = slot.section[j].process(channel, sample);
        }
        return sample;
    }
    static void clearTiny(Bank& bank) noexcept {
        for (std::uint8_t i = 0; i < bank.activeCount; ++i) {
            auto& slot = bank.slots[bank.active[i]];
            for (std::uint8_t j = 0; j < slot.count; ++j)
                slot.section[j].denormalGuard();
        }
    }
};
} // namespace armix::builtin_fx
