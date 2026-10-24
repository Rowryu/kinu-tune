// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.

// Unit tests for the perceived note centre. Plain g++, no JUCE.

#include "../Source/Audio/Correction/NoteCentre.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace correction;

namespace
{
int failures = 0, checks = 0;
void near(double got, double want, double tol, const std::string& what)
{
    ++checks;
    if (std::abs(got - want) > tol)
    {
        ++failures;
        std::printf("  FAIL: %s (got %.3f, want %.3f)\n", what.c_str(), got, want);
    }
}
float hz(float midi) { return 440.0f * std::pow(2.0f, (midi - 69.0f) / 12.0f); }

void testLongQuietScoopDoesNotDragTheCentre()
{
    std::printf("a long quiet scoop does not drag the centre\n");
    std::vector<float> f0, rms;
    // 20 frames rising from two semitones under, quiet and steep-ish.
    for (int i = 0; i < 20; ++i) { f0.push_back(hz(62.0f + 2.0f * i / 20.0f)); rms.push_back(0.1f + 0.01f * i); }
    // 30 frames held on 64, loud.
    for (int i = 0; i < 30; ++i) { f0.push_back(hz(64.0f)); rms.push_back(1.0f); }
    double mean = 0; for (float f : f0) mean += 69.0 + 12.0 * std::log2(f / 440.0); mean /= f0.size();
    near(perceivedCentreMidi(f0, rms), 64.0, 0.02, "the held pitch is the centre");
    std::printf("  (a plain mean would have said %.2f)\n", mean);
}

void testFallAtTheEndIsIgnored()
{
    std::printf("a fall at the end is ignored\n");
    std::vector<float> f0, rms;
    for (int i = 0; i < 40; ++i) { f0.push_back(hz(67.2f)); rms.push_back(0.9f); }
    for (int i = 0; i < 15; ++i) { f0.push_back(hz(67.2f - 0.4f * i)); rms.push_back(0.9f - 0.06f * i); }
    near(perceivedCentreMidi(f0, rms), 67.2, 0.02, "the held part, 20 cents sharp, is kept");
}

void testVibratoCentresOnItsMiddle()
{
    std::printf("vibrato centres on its middle\n");
    std::vector<float> f0, rms;
    for (int i = 0; i < 120; ++i) { f0.push_back(hz(60.0f + 0.5f * std::sin(i * 0.35f))); rms.push_back(1.0f); }
    near(perceivedCentreMidi(f0, rms), 60.0, 0.15, "the middle of the wobble");
}

void testUnvoicedFramesAreSkippedAndEmptyIsNegative()
{
    std::printf("unvoiced frames are skipped\n");
    std::vector<float> f0{0, 0, hz(70), hz(70), 0}, rms{1, 1, 1, 1, 1};
    near(perceivedCentreMidi(f0, rms), 70.0, 0.01, "only voiced frames count");
    std::vector<float> none{0, 0, 0};
    ++checks; if (perceivedCentreMidi(none, rms) >= 0) { ++failures; std::printf("  FAIL: nothing voiced should be negative\n"); }
}

void testPureSlideStillHasACentre()
{
    std::printf("a note that is only a slide still has a centre\n");
    std::vector<float> f0, rms;
    for (int i = 0; i < 20; ++i) { f0.push_back(hz(60.0f + 0.3f * i)); rms.push_back(0.2f); }
    const float c = perceivedCentreMidi(f0, rms);
    ++checks; if (!(c >= 60.0f && c <= 66.0f)) { ++failures; std::printf("  FAIL: centre %.2f outside the slide\n", c); }
}
} // namespace

int main()
{
    std::printf("=== note centre tests ===\n\n");
    testLongQuietScoopDoesNotDragTheCentre();
    testFallAtTheEndIsIgnored();
    testVibratoCentresOnItsMiddle();
    testUnvoicedFramesAreSkippedAndEmptyIsNegative();
    testPureSlideStillHasACentre();
    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
