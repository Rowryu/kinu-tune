// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.
#pragma once

#include "MidiGuideTypes.h"
#include "NoteAlignment.h"
#include "TimingAnchors.h"
#include "TimingMap.h"

#include "../../Models/Project.h"

#include <vector>

namespace correction
{

/// How the targets are chosen.
enum class TimingMode
{
    /// Line each note up with the guide note it was matched to.
    Guide,
    /// Line each note up with the nearest division of the host's grid.
    Grid,
};

struct TimingCorrectionParams
{
    TimingMode mode = TimingMode::Guide;
    TimingAnchor anchor = TimingAnchor::EnergyPeak;
    TimingParams timing;
    AlignmentParams alignment;

    /// Grid settings, used when mode is Grid.
    TimingGrid grid;

    /// How close to a grid line counts as already correct, in frames.
    double gridToleranceFrames = 2.0;
};

/// What one run did.
struct TimingReport
{
    int notesConsidered = 0;
    int notesMatched = 0;   ///< guide mode only
    int notesMoved = 0;
    double largestShiftFrames = 0.0;
};

/// One note's position before and after, enough to undo the run.
struct TimingEdit
{
    int noteIndex = 0;
    int previousStartFrame = 0;
    int previousEndFrame = 0;
    int newStartFrame = 0;
    int newEndFrame = 0;
};

/**
 * Move notes in time towards a guide or a grid.
 *
 * Only the destination range of each note changes - its source range stays
 * put - which is exactly what a manual timing drag does, so the existing
 * synthesis path renders the result and nothing new has to be taught how to
 * stretch audio.
 *
 * The caller applies `edits` through the editor's own timing path (see
 * applyNoteTimingStates), which marks the right notes dirty, rebuilds the
 * curves and keeps undo honest. This function computes and reports; it does
 * NOT write to the project.
 */
TimingReport planTimingCorrection(const Project& project,
                                  const std::vector<GuideNote>& guide,
                                  const TimingCorrectionParams& params,
                                  std::vector<TimingEdit>& edits);

} // namespace correction
