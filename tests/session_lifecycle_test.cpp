// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/device/SessionLifecycle.h"
#include <cstdlib>
static void check (bool ok) { if (!ok) std::abort(); }
int main()
{
    using L = armix::device::SessionLifecycle;
    L lifecycle;
    auto s = lifecycle.snapshot();
    check(s.state == L::State::offline && !s.outputAllowed);
    auto first = lifecycle.begin();
    check(!lifecycle.ready(first));
    check(!lifecycle.prepared(first - 1));
    check(lifecycle.prepared(first));
    check(lifecycle.ready(first));
    check(lifecycle.snapshot().outputAllowed);
    check(lifecycle.degrade(first,L::Failure::starvation));
    check(!lifecycle.snapshot().outputAllowed);
    check(!lifecycle.ready(first));
    for(unsigned i=0;i<L::maxRetries;++i)
    {
        const auto token=lifecycle.snapshot().generation;
        check(lifecycle.recover(token));
        check(!lifecycle.retryPrepare(token-1));
        const auto generation=lifecycle.retryPrepare(token);
        check(generation>token && !lifecycle.ready(token));
        check(lifecycle.fail(generation,L::Failure::startup));
        check(!lifecycle.snapshot().outputAllowed);
    }
    check(!lifecycle.recover(lifecycle.snapshot().generation));
    lifecycle.stop();
    check(!lifecycle.prepared(first));
    check(lifecycle.snapshot().state==L::State::offline);
    const auto latest=lifecycle.begin();
    check(lifecycle.snapshot().retries==0);
    check(!lifecycle.ready(first));
    check(lifecycle.prepared(latest));
    check(lifecycle.ready(latest));
}
