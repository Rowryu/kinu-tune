// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.

// Unit tests for timing-map construction: the invariants the map must hold
// whatever it is asked for, and the cases from the project's test plan.
//
// Free of JUCE and the Project model so it builds with a plain g++ call.

#include "../Source/Audio/Correction/TimingMap.h"

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

void checkNear(double actual, double expected, double tolerance,
               const std::string& what)
{
    ++checks;
    if (std::abs(actual - expected) > tolerance)
    {
        ++failures;
        std::printf("  FAIL: %s (got %.3f, expected %.3f +/- %.3f)\n",
                    what.c_str(), actual, expected, tolerance);
    }
}

constexpr double kFps = 86.1328125;
constexpr int kTakeFrames = 900;

TimingTarget note(int start, int end, double targetStart, double targetEnd)
{
    TimingTarget t;
    t.sourceStartFrame = start;
    t.sourceEndFrame = end;
    t.targetStartFrame = targetStart;
    t.targetEndFrame = targetEnd;
    t.hasTarget = true;
    t.confidence = 1.0f;
    return t;
}

TimingTarget freeNote(int start, int end)
{
    TimingTarget t;
    t.sourceStartFrame = start;
    t.sourceEndFrame = end;
    t.hasTarget = false;
    return t;
}

/// Invariants that must hold for every map this module produces.
void assertInvariants(const TimingMap& map, const TimingParams& params,
                      int sourceFrameCount, const std::string& context)
{
    const auto& s = map.sourceFrame;
    if (s.empty())
    {
        check(false, context + ": map is empty");
        return;
    }

    bool monotonic = true;
    bool inRange = true;
    bool rateOk = true;
    double worstRate = 1.0;

    for (size_t i = 0; i < s.size(); ++i)
    {
        if (s[i] < -1e-6 || s[i] > sourceFrameCount + 1e-6)
            inRange = false;
        if (i > 0)
        {
            const double rate = s[i] - s[i - 1];
            if (rate < -1e-9)
                monotonic = false;
            // The bound is on the segments the map was built from; a little
            // slack absorbs the per-frame sampling of those segments.
            if (rate > params.maxRate + 1e-3 || rate < params.minRate - 1e-3)
            {
                rateOk = false;
                worstRate = rate;
            }
        }
    }

    check(monotonic, context + ": the map never goes backwards");
    check(inRange, context + ": every read stays inside the audio");
    if (!rateOk)
        std::printf("    (worst rate %.3f, bounds [%.2f, %.2f])\n", worstRate,
                    params.minRate, params.maxRate);
    check(rateOk, context + ": the local rate stays within bounds");
}

// --- tests ---------------------------------------------------------------

void testZeroStrengthIsIdentity()
{
    std::printf("zero strength leaves the timing alone\n");

    const std::vector<TimingTarget> notes = {note(100, 200, 60.0, 160.0),
                                             note(300, 400, 340.0, 440.0)};
    TimingParams params;
    params.correctionStrength = 0.0f;

    const auto map = buildTimingMap(notes, kTakeFrames, params);
    assertInvariants(map, params, kTakeFrames, "identity");

    bool identity = true;
    for (size_t i = 0; i < map.sourceFrame.size(); ++i)
        if (std::abs(map.sourceFrame[i] - static_cast<double>(i)) > 1e-6)
            identity = false;
    check(identity, "the map is the identity");
}

void testLateOnsetIsPulledEarlier()
{
    std::printf("a late entry is moved earlier\n");

    // Sung 40 frames (~0.46 s) behind the guide.
    const std::vector<TimingTarget> notes = {note(240, 340, 200.0, 300.0)};
    TimingParams params;
    params.correctionStrength = 1.0f;
    params.maxShiftSeconds = 1.0f; // the ceiling has its own test

    // These cases are about whether the note reaches its target, so the rate
    // ceiling is opened up to keep the two contracts separate; the shipping
    // default is narrow on purpose and has its own tests.
    params.minRate = 0.5;
    params.maxRate = 2.0;
    const auto map = buildTimingMap(notes, kTakeFrames, params);
    assertInvariants(map, params, kTakeFrames, "late onset");

    checkNear(map.noteStart[0], 200.0, 2.0, "the note starts on the target");
    check(map.noteStart[0] < 240.0, "it moved earlier, not later");
}

void testEarlyOnsetIsPushedLater()
{
    std::printf("an early entry is moved later\n");

    const std::vector<TimingTarget> notes = {note(200, 300, 240.0, 340.0)};
    TimingParams params;
    params.correctionStrength = 1.0f;
    params.maxShiftSeconds = 1.0f;

    // These cases are about whether the note reaches its target, so the rate
    // ceiling is opened up to keep the two contracts separate; the shipping
    // default is narrow on purpose and has its own tests.
    params.minRate = 0.5;
    params.maxRate = 2.0;
    const auto map = buildTimingMap(notes, kTakeFrames, params);
    assertInvariants(map, params, kTakeFrames, "early onset");

    checkNear(map.noteStart[0], 240.0, 2.0, "the note starts on the target");
}

void testStrengthScalesTheMove()
{
    std::printf("correction strength scales the move\n");

    const std::vector<TimingTarget> notes = {note(240, 340, 200.0, 300.0)};

    TimingParams full;
    full.correctionStrength = 1.0f;
    full.maxShiftSeconds = 1.0f;
    // About whether strength scales the move, not about the rate ceiling,
    // so the ceiling is opened up; the narrow shipping default has its own
    // tests.
    full.minRate = 0.5;
    full.maxRate = 2.0;
    TimingParams half = full;
    half.correctionStrength = 0.5f;

    const auto fullMap = buildTimingMap(notes, kTakeFrames, full);
    const auto halfMap = buildTimingMap(notes, kTakeFrames, half);

    checkNear(halfMap.noteStart[0], 220.0, 2.0, "half strength moves half way");
    checkNear(fullMap.noteStart[0], 200.0, 2.0, "full strength arrives");
}

void testMaxShiftIsRespected()
{
    std::printf("maximum timing shift is never exceeded\n");

    // Asked to move a full second; allowed a tenth of one.
    const std::vector<TimingTarget> notes = {
        note(400, 500, 400.0 - kFps, 500.0 - kFps)};
    TimingParams params;
    params.correctionStrength = 1.0f;
    params.maxShiftSeconds = 0.1f; // tighter than the 0.25 s default

    const auto map = buildTimingMap(notes, kTakeFrames, params);
    assertInvariants(map, params, kTakeFrames, "max shift");

    const double movedFrames = 400.0 - map.noteStart[0];
    check(movedFrames <= 0.1 * kFps + 2.0, "the move is clamped to the ceiling");
    check(movedFrames > 0.0, "but it still moved");
}

void testShortNoteIsLengthened()
{
    std::printf("a note that is too short is lengthened\n");

    // Sung for 50 frames where the guide wants 150.
    const std::vector<TimingTarget> notes = {note(300, 350, 300.0, 450.0)};
    TimingParams params;
    params.correctionStrength = 1.0f;
    params.correctNoteLength = true;
    params.maxShiftSeconds = 2.0f;
    // The bound under test, set here rather than inherited: the shipping
    // default is far narrower, because warping a vowel this hard is audible.
    params.minRate = 0.5;
    params.maxRate = 2.0;

    const auto map = buildTimingMap(notes, kTakeFrames, params);
    assertInvariants(map, params, kTakeFrames, "short note");

    const double length = map.noteEnd[0] - map.noteStart[0];
    check(length > 50.0, "the note got longer");
    // The rate floor caps the stretch at 2x here, so it cannot reach 150
    // frames in one go. That bound is the point, not a shortfall.
    checkNear(length, 100.0, 5.0, "stretched as far as the rate floor allows");
}

void testNoteLengthCorrectionCanBeDisabled()
{
    std::printf("with length correction off the note keeps its length\n");

    const std::vector<TimingTarget> notes = {note(300, 350, 260.0, 410.0)};
    TimingParams params;
    params.correctionStrength = 1.0f;
    params.correctNoteLength = false;
    params.maxShiftSeconds = 1.0f;

    const auto map = buildTimingMap(notes, kTakeFrames, params);
    assertInvariants(map, params, kTakeFrames, "length off");

    checkNear(map.noteEnd[0] - map.noteStart[0], 50.0, 2.0,
              "the length is unchanged");
    checkNear(map.noteStart[0], 260.0, 2.0, "but the note did move");
}

void testRateBoundsSurviveAnAbsurdRequest()
{
    std::printf("an impossible request is bounded, not obeyed\n");

    // Three notes each asked to land on top of the previous one.
    const std::vector<TimingTarget> notes = {note(100, 200, 100.0, 200.0),
                                             note(220, 320, 110.0, 210.0),
                                             note(340, 440, 120.0, 220.0)};
    TimingParams params;
    params.correctionStrength = 1.0f;
    params.maxShiftSeconds = 10.0f; // deliberately no ceiling worth the name

    const auto map = buildTimingMap(notes, kTakeFrames, params);
    assertInvariants(map, params, kTakeFrames, "absurd request");

    check(map.noteStart[0] < map.noteStart[1], "note order is preserved");
    check(map.noteStart[1] < map.noteStart[2], "note order is preserved");
}

void testUnmatchedNotesAreNotAnchored()
{
    std::printf("an unmatched note is carried by its neighbours\n");

    const std::vector<TimingTarget> notes = {note(100, 200, 60.0, 160.0),
                                             freeNote(220, 300),
                                             note(340, 440, 300.0, 400.0)};
    TimingParams params;
    params.correctionStrength = 1.0f;

    const auto map = buildTimingMap(notes, kTakeFrames, params);
    assertInvariants(map, params, kTakeFrames, "unmatched");

    check(map.noteStart[1] > map.noteStart[0], "it stays after the first note");
    check(map.noteStart[1] < map.noteStart[2], "and before the third");
}

void testLowConfidenceIsNotMoved()
{
    std::printf("a doubtful match does not move the note\n");

    auto doubtful = note(240, 340, 200.0, 300.0);
    doubtful.confidence = 0.1f;

    TimingParams params;
    params.correctionStrength = 1.0f;
    params.minConfidence = 0.25f;

    const auto map = buildTimingMap({doubtful}, kTakeFrames, params);
    assertInvariants(map, params, kTakeFrames, "low confidence");

    checkNear(map.noteStart[0], 240.0, 1.0, "the note stayed where it was sung");
}

void testFastTempoManyShortNotes()
{
    std::printf("a fast passage of short notes stays coherent\n");

    // Sixteen notes of ~0.12 s, each sung a little late.
    std::vector<TimingTarget> notes;
    for (int i = 0; i < 16; ++i)
    {
        const int start = 100 + i * 20;
        notes.push_back(note(start, start + 10,
                             static_cast<double>(start - 6),
                             static_cast<double>(start + 4)));
    }

    TimingParams params;
    params.correctionStrength = 1.0f;

    const auto map = buildTimingMap(notes, kTakeFrames, params);
    assertInvariants(map, params, kTakeFrames, "fast tempo");

    for (size_t i = 1; i < notes.size(); ++i)
        check(map.noteStart[i] > map.noteStart[i - 1],
              "note " + std::to_string(i) + " stays after its predecessor");
}

// Two notes that touch - the end of one is the start of the next, which is
// what the note list looks like all through a legato phrase - pulling in
// opposite directions. One source frame, two wishes. The onset is the
// audible event, so it is the one that must be honoured; the damage belongs
// in the tail of the note before it, where nobody hears it.
void testAbuttingNotesPullingApart()
{
    std::printf("abutting notes that disagree still move their onsets\n");

    TimingParams params;
    params.correctionStrength = 1.0f;

    // 100..200 wants to be 10 frames later, 200..300 ten frames earlier.
    std::vector<TimingTarget> notes = {note(100, 200, 110.0, 210.0),
                                       note(200, 300, 190.0, 290.0)};

    // These cases are about whether the note reaches its target, so the rate
    // ceiling is opened up to keep the two contracts separate; the shipping
    // default is narrow on purpose and has its own tests.
    params.minRate = 0.5;
    params.maxRate = 2.0;
    const auto map = buildTimingMap(notes, kTakeFrames, params);
    assertInvariants(map, params, kTakeFrames, "abutting notes");

    checkNear(map.noteStart[0], 110.0, 1.0, "the first onset moved later");
    checkNear(map.noteStart[1], 190.0, 1.0, "the second onset moved earlier");
    check(map.noteStart[1] > map.noteStart[0], "order is preserved");
}

// The same boundary, but both notes agree. Nothing clever should happen.
void testAbuttingNotesMovingTogether()
{
    std::printf("abutting notes that agree move as one\n");

    TimingParams params;
    params.correctionStrength = 1.0f;

    std::vector<TimingTarget> notes = {note(100, 200, 120.0, 220.0),
                                       note(200, 300, 220.0, 320.0)};

    // These cases are about whether the note reaches its target, so the rate
    // ceiling is opened up to keep the two contracts separate; the shipping
    // default is narrow on purpose and has its own tests.
    params.minRate = 0.5;
    params.maxRate = 2.0;
    const auto map = buildTimingMap(notes, kTakeFrames, params);
    assertInvariants(map, params, kTakeFrames, "abutting notes, agreeing");

    checkNear(map.noteStart[0], 120.0, 1.0, "the first onset moved");
    checkNear(map.noteStart[1], 220.0, 1.0, "so did the second");
    checkNear(map.noteEnd[1] - map.noteStart[1], 100.0, 1.0,
              "and the second kept its length");
}

// The case the narrow default bounds exist for. Two notes sung legato, the
// second asked to arrive 50 frames early. Nothing sits between them, so the
// first note's tail is the only thing that can absorb the move - and at the
// old bounds it would absorb all of it, compressing a short note by a third.
void testALegatoNeighbourIsNotWarpedToObey()
{
    std::printf("a legato neighbour is not warped to let the next note move\n");

    TimingParams params;
    params.correctionStrength = 1.0f;
    params.maxShiftSeconds = 2.0f;

    // 100..200 stays, 200..300 wants to start 50 frames early.
    const std::vector<TimingTarget> notes = {note(100, 200, 100.0, 200.0),
                                             note(200, 300, 150.0, 250.0)};

    const auto map = buildTimingMap(notes, kTakeFrames, params);
    assertInvariants(map, params, kTakeFrames, "legato neighbour");

    // It moves, but only as far as the first note's tail can be compressed
    // without warping: 100 source frames at the rate ceiling.
    const double firstLength = map.noteEnd[0] - map.noteStart[0];
    check(firstLength >= 100.0 / params.maxRate - 1.0,
          "the first note is not compressed past the ceiling");
    check(map.noteStart[1] < 200.0, "the second note still moved earlier");
    check(map.noteStart[1] > 150.0 + 1.0,
          "but it did not reach a target that would have warped its neighbour");
}

void testDegenerateInputs()
{
    std::printf("degenerate inputs are handled\n");

    TimingParams params;

    check(buildTimingMap({}, 0, params).sourceFrame.empty(),
          "no audio gives an empty map");

    const auto noNotes = buildTimingMap({}, 500, params);
    check(noNotes.sourceFrame.size() == 500, "a take with no notes maps whole");
    assertInvariants(noNotes, params, 500, "no notes");

    // Inverted and zero-length notes must not produce a broken map.
    std::vector<TimingTarget> bad = {note(300, 200, 100.0, 50.0),
                                     note(100, 100, 100.0, 100.0)};
    const auto badMap = buildTimingMap(bad, kTakeFrames, params);
    assertInvariants(badMap, params, kTakeFrames, "inverted notes");
}

} // namespace

int main()
{
    std::printf("=== timing map tests ===\n\n");

    testZeroStrengthIsIdentity();
    testLateOnsetIsPulledEarlier();
    testEarlyOnsetIsPushedLater();
    testStrengthScalesTheMove();
    testMaxShiftIsRespected();
    testShortNoteIsLengthened();
    testNoteLengthCorrectionCanBeDisabled();
    testRateBoundsSurviveAnAbsurdRequest();
    testUnmatchedNotesAreNotAnchored();
    testLowConfidenceIsNotMoved();
    testFastTempoManyShortNotes();
    testAbuttingNotesPullingApart();
    testAbuttingNotesMovingTogether();
    testALegatoNeighbourIsNotWarpedToObey();
    testDegenerateInputs();

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
