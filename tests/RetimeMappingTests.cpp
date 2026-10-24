// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.

// Consonant-preserving retime mapping. Plain g++, no JUCE.

#include "../Source/Audio/Synthesis/RetimeMapping.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace
{
int failures = 0, checks = 0;
void check(bool ok, const std::string& what)
{
    ++checks;
    if (!ok) { ++failures; std::printf("  FAIL: %s\n", what.c_str()); }
}
}

int main()
{
    std::printf("=== retime mapping tests ===\n");

    // Head 0 is the old linear stretch, frame for frame.
    bool same = true;
    for (int f = 100; f < 160; ++f)
    {
        const double normalized = (f - 100 + 0.5) / 60.0;
        const double old = 200 + normalized * 40 - 0.5;
        if (std::abs(retime::sourcePosition(f, 200, 240, 100, 160, 0) - old) > 1e-9)
            same = false;
    }
    check(same, "no head means the plain stretch");

    // A 40-frame note stretched to 60 with a 10-frame head.
    bool rigid = true;
    for (int f = 100; f < 110; ++f)
        if (retime::sourcePosition(f, 200, 240, 100, 160, 10) != 200 + (f - 100))
            rigid = false;
    check(rigid, "the head is copied one to one");

    bool monotonic = true;
    double prev = -1;
    for (int f = 100; f < 160; ++f)
    {
        const double s = retime::sourcePosition(f, 200, 240, 100, 160, 10);
        if (s < prev) monotonic = false;
        prev = s;
    }
    check(monotonic, "the mapping never runs backwards");
    check(retime::sourcePosition(159, 200, 240, 100, 160, 10) < 240.0, "the end stays inside the note");
    check(retime::sourcePosition(159, 200, 240, 100, 160, 10) > 238.0, "and reaches its end");

    // Head detection: 4 unvoiced frames then voiced -> 4 + 3 attack.
    std::vector<float> f0(100, 0.0f);
    for (int i = 24; i < 100; ++i) f0[i] = 220.0f;
    check(retime::headFrames(f0, 20, 80, 70) == 7, "consonant plus attack");
    check(retime::headFrames(f0, 20, 30, 10) <= 4, "capped to 40 percent of a short note");

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
