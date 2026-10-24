// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.

// Syllable boundaries inside one detected note. Plain g++, no JUCE.

#include "../Source/Audio/Correction/TimingAnchors.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace correction;

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
    std::printf("=== syllable onset tests ===\n");

    // Two syllables on one pitch with a 3-frame consonant gap at 40.
    {
        std::vector<float> pitch(100, 64.0f), rms(100, 1.0f);
        for (int f = 40; f < 43; ++f) { pitch[f] = 0.0f; rms[f] = 0.3f; }
        const auto on = findSyllableOnsets(rms, pitch, 0, 100);
        check(on.size() == 1, "one boundary found");
        check(!on.empty() && std::abs(on[0] - 40) <= 1, "at the consonant");
    }
    // Two syllables joined legato: no unvoiced gap, but a clear energy dip.
    {
        std::vector<float> pitch(100, 64.0f), rms(100, 1.0f);
        for (int f = 46; f < 54; ++f) rms[f] = 1.0f - 0.8f * (1.0f - std::abs(f - 50) / 4.0f);
        const auto on = findSyllableOnsets(rms, pitch, 0, 100);
        check(on.size() == 1 && std::abs(on[0] - 50) <= 1, "the dip bottom");
    }
    // Vibrato and a mild loudness wobble are not syllables.
    {
        std::vector<float> pitch(120), rms(120);
        for (int f = 0; f < 120; ++f)
        {
            pitch[f] = 64.0f + 0.5f * std::sin(f * 0.35f);
            rms[f] = 1.0f - 0.25f * (0.5f + 0.5f * std::sin(f * 0.35f));
        }
        check(findSyllableOnsets(rms, pitch, 0, 120).empty(), "vibrato is one note");
    }
    // Nothing too close to the note edges.
    {
        std::vector<float> pitch(60, 64.0f), rms(60, 1.0f);
        for (int f = 3; f < 6; ++f) pitch[f] = 0.0f;
        check(findSyllableOnsets(rms, pitch, 0, 60).empty(), "edge consonant ignored");
    }
    // Three syllables.
    {
        std::vector<float> pitch(150, 64.0f), rms(150, 1.0f);
        for (int f = 50; f < 53; ++f) pitch[f] = 0.0f;
        for (int f = 100; f < 103; ++f) pitch[f] = 0.0f;
        check(findSyllableOnsets(rms, pitch, 0, 150).size() == 2, "two boundaries");
    }

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
