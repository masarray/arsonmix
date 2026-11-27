// SPDX-License-Identifier: AGPL-3.0-only
#include "RtQualificationBridge.h"
#include "AntiNoizControl.h"
namespace armix::anti_noiz_qualification {
namespace real=armix::builtin_fx::anti_noiz;
struct Control::Impl {real::Control fx;};
Control::Control():impl_(new Impl{}){}
Control::~Control(){delete impl_;}
bool Control::prepare(double rate,int channels) noexcept {return impl_->fx.prepare(rate,channels);}
bool Control::submit(Command c) noexcept {
    real::Core::Settings s{static_cast<real::Core::Mode>(c.settings.mode),c.settings.cleanDb,c.settings.silenceDb,c.settings.musicProtect,c.settings.enabled};
    return impl_->fx.submit({c.id,c.kind==Kind::settings?real::Control::Kind::settings:real::Control::Kind::learn,s});
}
bool Control::poll(Completion& c) noexcept {
    real::Control::Completion a;
    if(!impl_->fx.poll(a))return false;
    c={a.id,a.applied};return true;
}
bool Control::pollTelemetry(Snapshot& s) noexcept {
    real::Control::Snapshot m;
    if(!impl_->fx.pollTelemetry(m))return false;
    s={m.profile,m.learning,m.latency};return true;
}
Control::Snapshot Control::snapshot() const noexcept {
    const auto m=impl_->fx.snapshot();return {m.profile,m.learning,m.latency};
}
void Control::process(float* l,float* r,int n) noexcept {impl_->fx.process(l,r,n);}
}
