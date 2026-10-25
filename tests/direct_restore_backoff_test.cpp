// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/device/DirectRestoreBackoff.h"
#include <cstdlib>
static void check (bool ok) { if (!ok) std::abort(); }
int main()
{
    using R = armix::device::DirectRestoreBackoff;
    R p;
    check(!p.due(0,0));
    const auto generation = p.begin(1000);
    check(p.due(generation,1000));
    check(!p.due(generation-1,1000));
    check(p.failed(generation,1000));
    auto snap = p.snapshot();
    check(snap.attempts==1 && snap.nextDueMs==1250);
    check(!p.due(generation,1249));
    check(p.remainingMs(1100)==150);
    check(p.due(generation,1250));
    check(p.failed(generation,1250));
    check(p.snapshot().nextDueMs==2250);
    check(p.failed(generation,2250));
    check(p.snapshot().nextDueMs==5250);
    check(p.failed(generation,5250));
    check(p.snapshot().nextDueMs==13250);
    check(p.failed(generation,13250));
    check(p.snapshot().state==R::State::exhausted);
    check(p.snapshot().attempts==R::maxAttempts);
    check(!p.due(generation,1000000));
    check(!p.failed(generation,1000000));
    check(!p.recovered(generation));
    const auto fresh=p.begin(50000);
    check(fresh>generation);
    check(!p.failed(generation,50000));
    check(!p.recovered(generation));
    check(p.due(fresh,50000));
    check(p.recovered(fresh));
    check(p.snapshot().state==R::State::idle);
    check(p.snapshot().attempts==0);
    const auto another=p.begin(70000);
    p.cancel();
    check(!p.due(another,80000));
    check(p.snapshot().generation>another);
    check(p.remainingMs(80000)==0);
}
