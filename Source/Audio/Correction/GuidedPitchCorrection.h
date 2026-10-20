// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.
#pragma once

#include "MidiGuideTypes.h"
#include "NoteAlignment.h"
#include "PitchTargeting.h"

#include "../../Models/Project.h"

#include <vector>

namespace correction
{

/// What one run of guided correction did, for the status line and the tests.
struct GuidedPitchReport
{
    int notesConsidered = 0;  ///< sung notes offered to the matcher
    int notesMatched = 0;     ///< of those, paired with a guide note
    int notesCorrected = 0;   ///< of those, actually moved
    float largestShiftSemitones = 0.0f;
};

/// One note's before-and-after, enough to undo the run exactly.
struct GuidedPitchEdit
{
    int noteIndex = 0;
    float previousMidi = 0.0f;
    float previousCorrectionSource = 0.0f;
    float newMidi = 0.0f;
    float newCorrectionSource = 0.0f;
};

/**
 * Correct the project's note pitches towards a MIDI guide.
 *
 * This is the editor's existing Correct Pitch with a different answer to one
 * question. Correct Pitch moves a note from its sung pitch towards the
 * nearest note of the editor scale; guided correction moves it towards the
 * guide note the matcher paired it with. Everything else is deliberately the
 * same path: the correction reference stays put, so the inspector's slider
 * still reads the share of the sung deviation that has been removed, and
 * running the correction twice does not compound.
 *
 * Only the note centre moves. deltaPitch is untouched, so vibrato, scoops,
 * falls and portamento survive.
 *
 * `edits` receives the before-and-after of every note that moved, in project
 * note order, which is what an undo action needs. Pass nullptr if not wanted.
 *
 * Writes go through NotePitch, as dev-rules.md section 1 requires, and the
 * caller is responsible for the undo record and for refreshing the UI.
 */
GuidedPitchReport applyGuidedPitchCorrection(
    Project& project,
    const std::vector<GuideNote>& guide,
    const PitchTargetParams& params = {},
    const AlignmentParams& alignment = {},
    std::vector<GuidedPitchEdit>* edits = nullptr);

/// Put back what applyGuidedPitchCorrection changed.
void revertGuidedPitchCorrection(Project& project,
                                 const std::vector<GuidedPitchEdit>& edits);

/// Re-apply what applyGuidedPitchCorrection changed.
void reapplyGuidedPitchCorrection(Project& project,
                                  const std::vector<GuidedPitchEdit>& edits);

} // namespace correction
