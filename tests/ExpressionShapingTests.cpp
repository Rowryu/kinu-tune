// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.

// Unit tests for deltaPitch expression shaping: vibrato preservation, scoop
// preservation and slow drift correction.
//
// The curves here are whole takes with the note placed well inside them, the
// way the plug-in will call this, so the take's own ends never stand in for a
// note boundary.
//
// Free of JUCE and the Project model so it builds with a plain g++ call.

#include "../Source/Audio/Correction/ExpressionShaping.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace correction;

namespace
{

int failures = 0;
int checks = 0;

void check(bool condition, const std::string& what)
{
    ++checks;
    if (!condition)
    {
        ++failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

void checkNear(float actual, float expected, float tolerance,
               const std::string& what)
{
    ++checks;
    if (std::abs(actual - expected) > tolerance)
    {
        ++failures;
        std::printf("  FAIL: %s (got %.5f, expected %.5f +/- %.5f)\n",
                    what.c_str(), actual, expected, tolerance);
    }
}

constexpr double kFps = 86.1328125;
constexpr double kPi = 3.14159265358979323846;

// A take long enough that its own ends are far from the note under test.
constexpr int kTakeFrames = 500;
constexpr int kNoteStart = 150;
constexpr int kNoteEnd = 350;
// The band split needs a full window, which it only has away from the note's
// own ends; vibrato assertions are therefore made over the interior. See the
// note on the per-note split in ExpressionShaping.cpp.
constexpr int kInteriorStart = kNoteStart + 20;
constexpr int kInteriorEnd = kNoteEnd - 20;
constexpr int kNoteLength = kNoteEnd - kNoteStart;

std::vector<NoteSpan> theNote()
{
    return {NoteSpan{kNoteStart, kNoteEnd}};
}

float peakToPeak(const std::vector<float>& v, int from, int to)
{
    if (from >= to)
        return 0.0f;
    float lo = v[static_cast<size_t>(from)];
    float hi = lo;
    for (int i = from; i < to; ++i)
    {
        lo = std::min(lo, v[static_cast<size_t>(i)]);
        hi = std::max(hi, v[static_cast<size_t>(i)]);
    }
    return hi - lo;
}

float meanOf(const std::vector<float>& v, int from, int to)
{
    if (from >= to)
        return 0.0f;
    double s = 0.0;
    for (int i = from; i < to; ++i)
        s += v[static_cast<size_t>(i)];
    return static_cast<float>(s / static_cast<double>(to - from));
}

float slopeOf(const std::vector<float>& v, int from, int to)
{
    const int n = to - from;
    if (n < 2)
        return 0.0f;
    double sx = 0, sy = 0, sxy = 0, sxx = 0;
    for (int i = 0; i < n; ++i)
    {
        const double y = v[static_cast<size_t>(from + i)];
        sx += i;
        sy += y;
        sxy += static_cast<double>(i) * y;
        sxx += static_cast<double>(i) * i;
    }
    const double d = n * sxx - sx * sx;
    return d == 0.0 ? 0.0f : static_cast<float>((n * sxy - sx * sy) / d);
}

/// A take-length vibrato of +/- `depth` semitones.
std::vector<float> makeVibratoTake(float depth, double rateHz = 5.5)
{
    std::vector<float> v(static_cast<size_t>(kTakeFrames), 0.0f);
    for (int i = 0; i < kTakeFrames; ++i)
        v[static_cast<size_t>(i)] = depth * static_cast<float>(std::sin(
            2.0 * kPi * rateHz * static_cast<double>(i) / kFps));
    return v;
}

/// A scoop into the note: starts `depth` semitones flat at the note onset and
/// reaches the centre over `rampFrames`.
void addScoop(std::vector<float>& take, int rampFrames, float depth)
{
    for (int i = 0; i < rampFrames; ++i)
    {
        const float t = static_cast<float>(i) / static_cast<float>(rampFrames);
        take[static_cast<size_t>(kNoteStart + i)] += -depth * (1.0f - t);
    }
}

// --- tests ---------------------------------------------------------------

void testDefaultsAreIdentity()
{
    std::printf("defaults leave the curve untouched\n");

    const auto take = makeVibratoTake(0.4f);
    const auto out = shapeDeltaPitch(take, theNote(), {});

    check(out.size() == take.size(), "length preserved");
    bool identical = true;
    for (size_t i = 0; i < take.size(); ++i)
        if (out[i] != take[i])
            identical = false;
    check(identical, "output is bit-identical to the input");
}

void testVibratoPreservedAtFull()
{
    std::printf("vibrato survives when preservation is 1\n");

    const auto take = makeVibratoTake(0.4f);

    // Force the filter path with a drift correction that finds no drift: the
    // vibrato must still come through intact.
    ExpressionParams params;
    params.pitchDriftCorrection = 1.0f;
    const auto out = shapeDeltaPitch(take, theNote(), params);

    checkNear(peakToPeak(out, kInteriorStart, kInteriorEnd),
              peakToPeak(take, kInteriorStart, kInteriorEnd), 0.03f,
              "vibrato depth is retained");
}

void testVibratoRemovedAtZero()
{
    std::printf("vibrato is removed when preservation is 0\n");

    const auto take = makeVibratoTake(0.4f);

    ExpressionParams params;
    params.vibratoPreservation = 0.0f;
    const auto out = shapeDeltaPitch(take, theNote(), params);

    check(peakToPeak(out, kInteriorStart, kInteriorEnd) <
              peakToPeak(take, kInteriorStart, kInteriorEnd) * 0.15f,
          "almost all of the vibrato is gone");
}

void testVibratoHalved()
{
    std::printf("vibrato preservation scales the depth\n");

    const auto take = makeVibratoTake(0.4f);

    ExpressionParams full;
    full.pitchDriftCorrection = 1.0f;
    ExpressionParams half = full;
    half.vibratoPreservation = 0.5f;

    const auto fullOut = shapeDeltaPitch(take, theNote(), full);
    const auto halfOut = shapeDeltaPitch(take, theNote(), half);

    checkNear(peakToPeak(halfOut, kInteriorStart, kInteriorEnd),
              peakToPeak(fullOut, kInteriorStart, kInteriorEnd) * 0.5f, 0.06f,
              "half preservation gives half the depth");
}

void testOnlyTheNoteIsShaped()
{
    std::printf("frames outside the note are passed through\n");

    const auto take = makeVibratoTake(0.4f);

    ExpressionParams params;
    params.vibratoPreservation = 0.0f;
    const auto out = shapeDeltaPitch(take, theNote(), params);

    bool untouched = true;
    for (int i = 0; i < kNoteStart; ++i)
        if (out[static_cast<size_t>(i)] != take[static_cast<size_t>(i)])
            untouched = false;
    for (int i = kNoteEnd; i < kTakeFrames; ++i)
        if (out[static_cast<size_t>(i)] != take[static_cast<size_t>(i)])
            untouched = false;
    check(untouched, "everything outside the note is bit-identical");
}

void testDriftIsCorrected()
{
    std::printf("slow drift across a long note is flattened\n");

    // The note sags by a semitone over its length; no vibrato.
    std::vector<float> take(static_cast<size_t>(kTakeFrames), 0.0f);
    for (int i = 0; i < kNoteLength; ++i)
        take[static_cast<size_t>(kNoteStart + i)] =
            -1.0f * static_cast<float>(i) / static_cast<float>(kNoteLength - 1);

    ExpressionParams params;
    params.pitchDriftCorrection = 1.0f;
    const auto out = shapeDeltaPitch(take, theNote(), params);

    const float before = std::abs(slopeOf(take, kNoteStart, kNoteEnd));
    const float after = std::abs(slopeOf(out, kNoteStart, kNoteEnd));
    check(after < before * 0.2f, "the tilt is largely removed");
}

void testDriftCorrectionKeepsTheLevel()
{
    std::printf("drift correction removes tilt, not level\n");

    std::vector<float> take(static_cast<size_t>(kTakeFrames), 0.0f);
    for (int i = 0; i < kNoteLength; ++i)
        take[static_cast<size_t>(kNoteStart + i)] =
            0.5f - 1.0f * static_cast<float>(i) / static_cast<float>(kNoteLength - 1);

    ExpressionParams params;
    params.pitchDriftCorrection = 1.0f;
    const auto out = shapeDeltaPitch(take, theNote(), params);

    // Moving the note centre is the targeting stage's job, so the mean of the
    // delta curve must survive this stage.
    checkNear(meanOf(out, kNoteStart, kNoteEnd),
              meanOf(take, kNoteStart, kNoteEnd), 0.03f,
              "mean offset is preserved");
}

void testSlowScoopSurvivesDriftCorrection()
{
    std::printf("an onset scoop survives drift correction\n");

    std::vector<float> take(static_cast<size_t>(kTakeFrames), 0.0f);
    addScoop(take, 60, 1.0f); // ~0.7 s scoop, clearly in the phrasing band

    ExpressionParams params;
    params.pitchDriftCorrection = 1.0f;
    const auto out = shapeDeltaPitch(take, theNote(), params);

    // The scoop sits at the note edge and is excluded from the drift fit, so
    // the correction must not iron it flat.
    check(peakToPeak(out, kNoteStart, kNoteEnd) > 0.7f,
          "the scoop keeps most of its depth");
    check(out[static_cast<size_t>(kNoteStart)] < -0.6f,
          "the note still starts clearly flat");
}

void testExpressionPreservationFlattensSlowScoops()
{
    std::printf("expression preservation flattens slow scoops\n");

    std::vector<float> take(static_cast<size_t>(kTakeFrames), 0.0f);
    addScoop(take, 60, 1.0f);

    ExpressionParams params;
    params.expressionPreservation = 0.0f;
    const auto out = shapeDeltaPitch(take, theNote(), params);

    check(peakToPeak(out, kNoteStart, kNoteEnd) < 0.15f,
          "the slow scoop is flattened");
}

void testVeryFastScoopIsOnlyPartlyFlattened()
{
    std::printf("KNOWN LIMIT: a 0.12 s scoop is only partly flattened\n");

    // At the note's first half-window the filter cannot resolve vibrato, so
    // movement there is read as articulation - which is what lets scoops be
    // flattened at all. A scoop shorter than that window is still faster than
    // the split can fully follow. Measured residual is ~0.35 of a semitone
    // from 1.0; asserted here so it cannot drift silently.
    std::vector<float> take(static_cast<size_t>(kTakeFrames), 0.0f);
    addScoop(take, 10, 1.0f);

    ExpressionParams params;
    params.expressionPreservation = 0.0f;
    const auto out = shapeDeltaPitch(take, theNote(), params);

    const float residual = peakToPeak(out, kNoteStart, kNoteEnd);
    check(residual < 0.5f, "most of the fast scoop is still removed");
    check(residual > 0.15f, "some of it survives, as measured");
}

void testVibratoAtTheNoteEdgeCountsAsArticulation()
{
    std::printf("KNOWN LIMIT: vibrato at the note edge is read as "
                "articulation\n");

    // The companion to the test above: with vibrato preservation at zero, the
    // interior is cleaned but the first and last half-window keep their
    // movement, because at a note onset a scoop and the first half-cycle of a
    // vibrato are the same shape. Treating it as articulation leaves attacks
    // alone, which is the safer of the two errors.
    const auto take = makeVibratoTake(0.4f);

    ExpressionParams params;
    params.vibratoPreservation = 0.0f;
    const auto out = shapeDeltaPitch(take, theNote(), params);

    check(peakToPeak(out, kInteriorStart, kInteriorEnd) < 0.15f,
          "the interior is cleaned");
    check(peakToPeak(out, kNoteStart, kNoteStart + 12) > 0.1f,
          "the attack keeps its movement");
}

void testVibratoKeptWhileSlowScoopFlattened()
{
    std::printf("vibrato and phrasing are controlled independently\n");

    auto take = makeVibratoTake(0.3f);
    addScoop(take, 60, 1.0f);

    ExpressionParams params;
    params.expressionPreservation = 0.0f; // straighten the scoop
    params.vibratoPreservation = 1.0f;    // but keep the vibrato
    const auto out = shapeDeltaPitch(take, theNote(), params);

    // The scoop's semitone of slow movement is gone, so what remains should
    // be close to the vibrato's own depth rather than the combined range.
    checkNear(peakToPeak(out, kInteriorStart, kInteriorEnd), 0.6f, 0.15f,
              "vibrato depth remains");
    check(out[static_cast<size_t>(kNoteStart)] > -0.6f,
          "the scoop no longer drags the onset flat");
}

void testDegenerateInputsAreSafe()
{
    std::printf("empty, tiny and out-of-range inputs are handled\n");

    ExpressionParams params;
    params.vibratoPreservation = 0.0f;
    params.pitchDriftCorrection = 1.0f;

    check(shapeDeltaPitch({}, theNote(), params).empty(),
          "empty input gives empty output");

    const std::vector<float> one = {0.3f};
    check(shapeDeltaPitch(one, {NoteSpan{0, 1}}, params).size() == 1,
          "single frame survives");

    const std::vector<float> two = {0.3f, -0.2f};
    check(shapeDeltaPitch(two, {NoteSpan{0, 2}}, params).size() == 2,
          "two frames survive");

    // Spans that run off either end must not read out of bounds.
    const auto take = makeVibratoTake(0.2f);
    const std::vector<NoteSpan> bad = {NoteSpan{-50, 20},
                                       NoteSpan{kTakeFrames - 5, kTakeFrames + 80},
                                       NoteSpan{10, 5}};
    check(shapeDeltaPitch(take, bad, params).size() == take.size(),
          "out-of-range and inverted spans are clamped away");

    check(shapeDeltaPitch(take, {}, params).size() == take.size(),
          "a take with no notes is returned whole");
}

void testNoNotesMeansNoChange()
{
    std::printf("a take with no notes is left alone\n");

    const auto take = makeVibratoTake(0.4f);

    ExpressionParams params;
    params.vibratoPreservation = 0.0f;
    const auto out = shapeDeltaPitch(take, {}, params);

    bool identical = true;
    for (size_t i = 0; i < take.size(); ++i)
        if (out[i] != take[i])
            identical = false;
    check(identical, "nothing was shaped");
}

void testZeroPhase()
{
    std::printf("the filter does not shift vibrato in time\n");

    const auto take = makeVibratoTake(0.4f);
    const auto smoothed = zeroPhaseMovingAverage(take, 25);

    // A phase-shifting filter would make the residual look like a shifted
    // copy of the input; a zero-phase one leaves it in phase, so the
    // correlation between input and residual stays positive.
    double correlation = 0.0;
    for (size_t i = 0; i < take.size(); ++i)
        correlation += static_cast<double>(take[i]) *
                       (static_cast<double>(take[i]) - smoothed[i]);
    check(correlation > 0.0, "residual stays in phase with the input");
}

} // namespace

int main()
{
    std::printf("=== expression shaping tests ===\n\n");

    testDefaultsAreIdentity();
    testVibratoPreservedAtFull();
    testVibratoRemovedAtZero();
    testVibratoHalved();
    testOnlyTheNoteIsShaped();
    testDriftIsCorrected();
    testDriftCorrectionKeepsTheLevel();
    testSlowScoopSurvivesDriftCorrection();
    testExpressionPreservationFlattensSlowScoops();
    testVeryFastScoopIsOnlyPartlyFlattened();
    testVibratoAtTheNoteEdgeCountsAsArticulation();
    testVibratoKeptWhileSlowScoopFlattened();
    testDegenerateInputsAreSafe();
    testNoNotesMeansNoChange();
    testZeroPhase();

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
