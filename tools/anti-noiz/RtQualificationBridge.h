// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>
// Standalone benchmark ABI only. The identical driver object is linked
// against each independently compiled DSP revision. No production dependency.
namespace armix::anti_noiz_qualification {
struct Core {
    enum class Mode { AutoProtect, Voice, Singing, Music };
    struct Settings {
        Mode mode=Mode::AutoProtect;
        float cleanDb=14.f,silenceDb=32.f,musicProtect=1.f;
        bool enabled=true;
    };
};
class Control final {
public:
    enum class Kind {settings,learn};
    struct Command {std::uint64_t id=0;Kind kind=Kind::settings;Core::Settings settings{};};
    struct Snapshot {bool profile=false,learning=false;int latency=0;};
    struct Completion {std::uint64_t id=0;bool applied=false;};
    Control();
    ~Control();
    Control(const Control&)=delete;
    Control& operator=(const Control&)=delete;
    bool prepare(double rate,int channels) noexcept;
    bool submit(Command c) noexcept;
    bool poll(Completion& c) noexcept;
    bool pollTelemetry(Snapshot& s) noexcept;
    Snapshot snapshot() const noexcept;
    void process(float* l,float* r,int n) noexcept;
private:
    struct Impl;
    Impl* impl_;
};
}
