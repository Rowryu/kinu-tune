// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.
// Unit tests for the MIDI-guide alignment and pitch-targeting stages.
//
// Deliberately free of JUCE and the Project model so it builds and runs
// anywhere, including a plain g++ invocation.

#include "../Source/Audio/Correction/NoteAlignment.h"
#include "../Source/Audio/Correction/PitchTargeting.h"

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
        std::printf("  FAIL: %s (got %.4f, expected %.4f +/- %.4f)\n",
                    what.c_str(), actual, expected, tolerance);
    }
}

// 86.13 frames per second: one frame is ~11.6 ms at 44.1 kHz / hop 512.
constexpr int kFramesPerBeat = 43; // ~0.5 s

SungNote sungNote(int startBeat, int lengthBeats, float midi)
{
    SungNote n;
    n.startFrame = startBeat * kFramesPerBeat;
    n.endFrame = (startBeat + lengthBeats) * kFramesPerBeat;
    n.midiNote = midi;
    return n;
}

GuideNote guideNote(int startBeat, int lengthBeats, float midi)
{
    GuideNote n;
    n.startFrame = startBeat * kFramesPerBeat;
    n.endFrame = (startBeat + lengthBeats) * kFramesPerBeat;
    n.midiNote = midi;
    return n;
}

// --- alignment -----------------------------------------------------------

void testOneToOneAlignment()
{
    std::printf("one-to-one alignment\n");

    // Sung slightly flat of a C-D-E-F guide, each by a few tens of cents.
    const std::vector<SungNote> sung = {
        sungNote(0, 1, 59.7f), sungNote(1, 1, 61.8f),
        sungNote(2, 1, 63.6f), sungNote(3, 1, 64.9f)};
    const std::vector<GuideNote> guide = {
        guideNote(0, 1, 60.0f), guideNote(1, 1, 62.0f),
        guideNote(2, 1, 64.0f), guideNote(3, 1, 65.0f)};

    const auto matches = alignNotesToGuide(sung, guide);

    check(matches.size() == 4, "one result per sung note");
    for (int i = 0; i < 4; ++i)
    {
        check(matches[static_cast<size_t>(i)].guideIndex == i,
              "sung note " + std::to_string(i) + " matches guide note " +
                  std::to_string(i));
        check(matches[static_cast<size_t>(i)].confidence > 0.8f,
              "close match is high confidence");
    }
}

void testExtraSungNoteStaysUnmatched()
{
    std::printf("extra sung note (ad-lib / breath) stays unmatched\n");

    // The singer adds a note between guide notes 0 and 1 that is nowhere near
    // the melody. Nearest-neighbour matching would steal a guide note for it.
    std::vector<SungNote> sung = {sungNote(0, 1, 60.0f), sungNote(1, 1, 71.0f),
                                  sungNote(2, 1, 62.0f)};
    const std::vector<GuideNote> guide = {guideNote(0, 1, 60.0f),
                                          guideNote(2, 1, 62.0f)};

    const auto matches = alignNotesToGuide(sung, guide);

    check(matches[0].guideIndex == 0, "first sung note keeps guide 0");
    check(!matches[1].isMatched(), "ad-lib note is left unmatched");
    check(matches[2].guideIndex == 1, "third sung note keeps guide 1");
}

void testMonotonicityIsPreserved()
{
    std::printf("alignment stays monotonic under timing error\n");

    // Every sung note is late by most of a beat. A greedy matcher pairs note
    // 0 with guide 1, and the whole sequence slips by one.
    const std::vector<SungNote> sung = {
        sungNote(0, 1, 60.0f), sungNote(1, 1, 62.0f), sungNote(2, 1, 64.0f)};
    std::vector<GuideNote> guide = {guideNote(0, 1, 60.0f),
                                    guideNote(1, 1, 62.0f),
                                    guideNote(2, 1, 64.0f)};
    for (auto& g : guide)
    {
        g.startFrame -= 30;
        g.endFrame -= 30;
    }

    const auto matches = alignNotesToGuide(sung, guide);

    int previousGuide = -1;
    for (const auto& m : matches)
    {
        if (!m.isMatched())
            continue;
        check(m.guideIndex > previousGuide, "guide indices strictly increase");
        previousGuide = m.guideIndex;
    }
    check(matches[0].guideIndex == 0, "pitch agreement anchors the alignment");
}

void testEmptyGuideLeavesEverythingUnmatched()
{
    std::printf("empty guide leaves every note unmatched\n");

    const std::vector<SungNote> sung = {sungNote(0, 1, 60.0f)};
    const auto matches = alignNotesToGuide(sung, {});

    check(matches.size() == 1, "still one result per sung note");
    check(!matches[0].isMatched(), "nothing to match against");
}

// --- targeting -----------------------------------------------------------

void testCentsLevelCorrection()
{
    std::printf("a note tens of cents flat is pulled to the guide\n");

    const std::vector<SungNote> sung = {sungNote(0, 1, 59.7f)};
    const std::vector<GuideNote> guide = {guideNote(0, 1, 60.0f)};
    const auto matches = alignNotesToGuide(sung, guide);

    PitchTargetParams params;
    params.correctionStrength = 1.0f;
    const auto targets = computePitchTargets(sung, guide, matches, params);

    check(targets[0].corrected, "note was corrected");
    // Confidence is just below 1 because of the residual pitch cost, so the
    // note lands very close to the guide rather than exactly on it.
    checkNear(targets[0].correctedMidiNote, 60.0f, 0.05f,
              "note centre lands on the guide");
}

void testStrengthScalesTheMove()
{
    std::printf("correction strength scales the move\n");

    const std::vector<SungNote> sung = {sungNote(0, 1, 59.0f)};
    const std::vector<GuideNote> guide = {guideNote(0, 1, 60.0f)};
    const auto matches = alignNotesToGuide(sung, guide);

    PitchTargetParams full;
    full.correctionStrength = 1.0f;
    PitchTargetParams half;
    half.correctionStrength = 0.5f;

    const auto fullTargets = computePitchTargets(sung, guide, matches, full);
    const auto halfTargets = computePitchTargets(sung, guide, matches, half);

    checkNear(halfTargets[0].appliedShiftSemitones,
              fullTargets[0].appliedShiftSemitones * 0.5f, 1e-4f,
              "half strength moves half as far");

    PitchTargetParams off;
    off.correctionStrength = 0.0f;
    const auto offTargets = computePitchTargets(sung, guide, matches, off);
    check(!offTargets[0].corrected, "zero strength leaves the note alone");
    checkNear(offTargets[0].correctedMidiNote, 59.0f, 1e-6f,
              "zero strength preserves the original centre");
}

void testMaxShiftIsRespected()
{
    std::printf("maximum pitch shift is never exceeded\n");

    // Sung a fourth below the guide: a wrong note, not a tuning error.
    const std::vector<SungNote> sung = {sungNote(0, 1, 55.0f)};
    const std::vector<GuideNote> guide = {guideNote(0, 1, 60.0f)};
    const auto matches = alignNotesToGuide(sung, guide);

    PitchTargetParams params;
    params.correctionStrength = 1.0f;
    params.maxShiftSemitones = 2.0f;
    params.minConfidence = 0.0f; // force a correction to test the clamp
    const auto targets = computePitchTargets(sung, guide, matches, params);

    check(std::abs(targets[0].appliedShiftSemitones) <= 2.0f + 1e-5f,
          "shift is clamped to the ceiling");
}

void testLowConfidenceIsLeftAlone()
{
    std::printf("a doubtful match is not forced onto the guide\n");

    // Five semitones apart with a large onset error: the pair survives the
    // path only because there is nothing else, and should not be trusted.
    const std::vector<SungNote> sung = {sungNote(0, 1, 55.0f)};
    const std::vector<GuideNote> guide = {guideNote(4, 1, 60.0f)};
    const auto matches = alignNotesToGuide(sung, guide);

    PitchTargetParams params;
    params.correctionStrength = 1.0f;
    params.minConfidence = 0.25f;
    const auto targets = computePitchTargets(sung, guide, matches, params);

    check(matches[0].confidence < 0.25f, "match confidence is low");
    check(!targets[0].corrected, "low-confidence match is skipped");
    checkNear(targets[0].correctedMidiNote, 55.0f, 1e-6f,
              "the original pitch survives untouched");
}

void testUnmatchedNotesAreUntouched()
{
    std::printf("unmatched notes are returned unchanged\n");

    const std::vector<SungNote> sung = {sungNote(0, 1, 67.3f)};
    const auto matches = alignNotesToGuide(sung, {});
    const auto targets = computePitchTargets(sung, {}, matches, {});

    check(!targets[0].corrected, "nothing was applied");
    checkNear(targets[0].correctedMidiNote, 67.3f, 1e-6f,
              "pitch is passed through exactly");
}

void testShortAndLongNotes()
{
    std::printf("note length does not change the decision\n");

    // One frame long and eight beats long, same pitch error.
    SungNote shortNote = sungNote(0, 1, 59.5f);
    shortNote.endFrame = shortNote.startFrame + 1;
    const SungNote longNote = sungNote(0, 8, 59.5f);

    const std::vector<GuideNote> guide = {guideNote(0, 1, 60.0f)};

    PitchTargetParams params;
    params.correctionStrength = 1.0f;

    const std::vector<SungNote> shortSeq = {shortNote};
    const std::vector<SungNote> longSeq = {longNote};
    const auto shortTargets = computePitchTargets(
        shortSeq, guide, alignNotesToGuide(shortSeq, guide), params);
    const auto longTargets = computePitchTargets(
        longSeq, guide, alignNotesToGuide(longSeq, guide), params);

    checkNear(shortTargets[0].appliedShiftSemitones,
              longTargets[0].appliedShiftSemitones, 1e-5f,
              "both notes get the same correction");
}

} // namespace

int main()
{
    std::printf("=== MIDI guide correction tests ===\n\n");

    testOneToOneAlignment();
    testExtraSungNoteStaysUnmatched();
    testMonotonicityIsPreserved();
    testEmptyGuideLeavesEverythingUnmatched();
    testCentsLevelCorrection();
    testStrengthScalesTheMove();
    testMaxShiftIsRespected();
    testLowConfidenceIsLeftAlone();
    testUnmatchedNotesAreUntouched();
    testShortAndLongNotes();

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
