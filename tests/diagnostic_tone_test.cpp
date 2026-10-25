// SPDX-License-Identifier: AGPL-3.0-only
#include "../src/DiagnosticTone.h"
#include <array>
#include <cassert>
#include <cmath>
#include <cstring>
#include <numbers>

int main()
{
    using namespace armix::diagnostics;
    assert (std::strcmp (routeName (Route::captureOnly), "CAPTURE_ONLY") == 0);
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
            assert (std::isfinite (value));
            assert (std::abs (value) <= 0.03163f);
            if (i > static_cast<int> (rate * 0.02))
            {
                sumSquares += static_cast<double> (value) * value;
                peak = std::max (peak, std::abs (value));
            }
            last = value;
        }
        assert (peak > 0.030f);
        assert (sumSquares / n > 0.00040 && sumSquares / n < 0.00052);
        // Muting must fade continuously rather than abruptly outputting a step.
        float previous = last;
        for (int i = 0; i < static_cast<int> (rate * 0.015); ++i)
        {
            const float v = tone.next (false);
            assert (std::isfinite (v));
            assert (std::abs (v) < 0.03163f);
            assert (std::abs (v - previous) < 0.0025f);
            previous = v;
        }
        for (int i = 0; i < 1000; ++i) assert (tone.next (false) == 0.f);
    }
}
