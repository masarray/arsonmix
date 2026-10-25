// SPDX-License-Identifier: AGPL-3.0-only
#include "../src/DiagnosticTone.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

static void check (bool condition) { if (!condition) std::abort(); }

int main()
{
    using namespace armix::diagnostics;
    check (std::strcmp (routeName (Route::captureOnly), "CAPTURE_ONLY") == 0);
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        DiagnosticTone tone;
        tone.prepare (rate);
        double sumSquares = 0.0;
        float last = 0.0f;
        float peak = 0.0f;
        const int n = static_cast<int> (rate);
        for (int i = 0; i < n; ++i)
        {
            const auto value = tone.next (true);
            check (std::isfinite (value));
            check (std::abs (value) <= 0.03163f);
            if (i > static_cast<int> (rate * 0.02))
            {
                sumSquares += static_cast<double> (value) * value;
                peak = std::max (peak, std::abs (value));
            }
            last = value;
        }
        check (peak > 0.030f);
        check (sumSquares / n > 0.00040 && sumSquares / n < 0.00052);
        // Muting must fade continuously rather than abruptly outputting a step.
        float previous = last;
        for (int i = 0; i < static_cast<int> (rate * 0.015); ++i)
        {
            const float v = tone.next (false);
            check (std::isfinite (v));
            check (std::abs (v) < 0.03163f);
            check (std::abs (v - previous) < 0.0025f);
            previous = v;
        }
        for (int i = 0; i < 1000; ++i) check (tone.next (false) == 0.f);
    }
}
