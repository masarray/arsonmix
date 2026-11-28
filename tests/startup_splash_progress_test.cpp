// SPDX-License-Identifier: AGPL-3.0-only
#include "../src/StartupSplashProgress.h"
#include <cstdlib>
#include <iostream>

static void check (bool ok)
{
    if (!ok) std::abort();
}

int main()
{
    using armix::startup::SplashProgress;
    for (int current = 0; current <= 2; ++current)
        for (int incoming = -2; incoming <= 5; ++incoming)
        {
            SplashProgress p;
            if (current >= 1) check (p.advance (1, 20.0));
            if (current == 2) check (p.advance (2, 50.0));
            const bool accepted = incoming > current && incoming <= 2;
            check (p.advance (incoming, 80.0) == accepted);
            check (p.stage() == (accepted ? incoming : current));
            check (p.fraction() == static_cast<float> (p.stage() + 1) / 3.0f);
            check (p.guiHandshakeMs() == (current >= 1 ? 20.0 : incoming == 1 ? 80.0 : -1.0));
            check (p.handoffMs() == (current == 2 ? 50.0 : incoming == 2 ? 80.0 : -1.0));
        }

    SplashProgress p;
    check (p.assetsDecodedMs() == -1.0 && p.firstPaintMs() == -1.0);
    p.markAssetsDecoded (2.5);
    p.markAssetsDecoded (7.0);
    p.markFirstPaint (9.0);
    p.markFirstPaint (11.0);
    check (p.assetsDecodedMs() == 2.5 && p.firstPaintMs() == 9.0);
    check (p.advance (1, 100.0));
    check (!p.advance (1, 300.0));
    check (!p.advance (3, 400.0));
    check (p.handoffMs() == -1.0 && p.guiHandshakeMs() == 100.0);
    check (p.advance (2, 500.0));
    check (p.handoffMs() == 500.0);
    std::cout << "PASS: 24 transition combinations, timing retention, missing and failure events\n";
}
