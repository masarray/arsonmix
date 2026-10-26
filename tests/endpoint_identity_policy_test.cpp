// SPDX-License-Identifier: AGPL-3.0-only
#include "../engine/device/EndpointIdentityPolicy.h"
#include <cstdlib>
#include <vector>
using P=armix::device::EndpointIdentityPolicy;
static void check(bool ok) {if(!ok)std::abort();}
int main()
{
    using R=P::Role; using E=P::Error;
    const std::vector<P::Endpoint> endpoints {
        {R::capture,L"Virtual Cable",L"capture-001"},
        {R::render,L"Wired Monitor",L"render-001"},
        {R::render,L"BT Monitor",L"render-bt"},
        // Same friendly name in another flow is valid and not ambiguous.
        {R::capture,L"Wired Monitor",L"capture-002"}
    };
    auto a=P::resolve(endpoints,L"Virtual Cable",L"Wired Monitor");
    check(a.ok() && a.selection.captureId==L"capture-001"
          && a.selection.renderId==L"render-001");
    check(P::sameIdentity(a.selection,a.selection));
    check(P::resolve(endpoints,L"",L"Wired Monitor").error==E::emptyName);
    check(P::resolve(endpoints,L"Gone",L"Wired Monitor").error==E::captureMissing);
    check(P::resolve(endpoints,L"Virtual Cable",L"Gone").error==E::renderMissing);
    auto different=endpoints;
    different.push_back({R::capture,L"Virtual Cable",L"capture-shadow"});
    check(P::resolve(different,L"Virtual Cable",L"Wired Monitor").error==E::captureAmbiguous);
    different=endpoints;
    different.push_back({R::render,L"Wired Monitor",L"render-shadow"});
    check(P::resolve(different,L"Virtual Cable",L"Wired Monitor").error==E::renderAmbiguous);
    // Same friendly names but a different Windows device must reject.
    different=endpoints;
    different[1].id=L"render-replaced";
    auto after=P::resolve(different,L"Virtual Cable",L"Wired Monitor");
    check(after.ok() && !P::sameIdentity(a.selection,after.selection));
    check(!P::sameIdentity({},after.selection));
    different=endpoints;
    different[0].id.clear();
    check(P::resolve(different,L"Virtual Cable",L"Wired Monitor").error==E::captureMissing);
    return 0;
}
