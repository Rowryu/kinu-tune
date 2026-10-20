// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.
#pragma once

#include <vector>

/**
 * Plain data types shared by the MIDI-guide correction stages.
 *
 * These deliberately avoid JUCE and the Project model so the alignment and
 * targeting logic can be unit-tested on its own. The plug-in layer converts
 * Note/Project data into these structs and writes the results back.
 */
namespace correction
{

/// One note of the MIDI guide track, on the project's mel-frame grid.
struct GuideNote
{
    int startFrame = 0;
    int endFrame = 0;      ///< exclusive
    float midiNote = 60.0f;

    int durationFrames() const { return endFrame - startFrame; }
};

/// One analysed sung note, as the segmenter produced it.
struct SungNote
{
    int startFrame = 0;
    int endFrame = 0;      ///< exclusive
    /// Representative pitch of the note in MIDI units (Note::midiNote), i.e.
    /// the note centre with the expression component already factored out.
    float midiNote = 60.0f;

    int durationFrames() const { return endFrame - startFrame; }
};

/// Result of matching one sung note against the guide.
struct NoteMatch
{
    int sungIndex = -1;
    int guideIndex = -1;    ///< -1 when the sung note has no counterpart
    /// 0..1. Low when the pair was a poor fit in pitch or time; correction is
    /// scaled by this so a doubtful match cannot drag a note to a wrong pitch.
    float confidence = 0.0f;

    bool isMatched() const { return guideIndex >= 0; }
};

/// Tunable weights for the alignment search.
struct AlignmentParams
{
    /// Frames per second of the mel grid, used to express time costs in
    /// seconds so the weights stay meaningful at any hop size.
    double framesPerSecond = 86.1328125; // 44100 / 512

    /// Cost per semitone of pitch disagreement.
    float pitchWeight = 1.0f;
    /// Pitch disagreement is capped here, so an octave error does not
    /// dominate the whole path.
    float maxPitchCostSemitones = 12.0f;

    /// Cost per second of onset disagreement.
    float timeWeight = 2.0f;
    /// Onset disagreement is capped here for the same reason.
    float maxTimeCostSeconds = 1.0f;

    /// Cost of leaving a sung note or a guide note unmatched. Raising this
    /// makes the path prefer matching everything; lowering it lets genuinely
    /// extra notes (breaths, ad-libs, unsung guide notes) stay unmatched.
    float skipCost = 1.5f;

    /// Pair cost at which confidence reaches zero. Costs below this map
    /// linearly onto 1..0.
    float zeroConfidenceCost = 3.0f;
};

} // namespace correction
