// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.
#pragma once

#include "MidiGuideTypes.h"

#include <vector>

namespace correction
{

/**
 * Match sung notes to MIDI guide notes with a monotonic DTW-style search.
 *
 * Both sequences are in time order and a singer does not reorder the melody,
 * so the alignment is monotonic: the path may match a pair, skip a sung note,
 * or skip a guide note, but never cross. That is what separates this from
 * nearest-neighbour matching, which happily assigns two sung notes to one
 * guide note and leaves a hole next to it.
 *
 * Pair cost combines pitch distance and onset distance, each capped so one
 * bad outlier cannot bend the whole path around itself. A pair that survives
 * into the path still carries its cost out as a confidence, because a match
 * being the best available is not the same as it being right.
 *
 * Returns one entry per sung note, in order. Unmatched notes carry
 * guideIndex -1 and confidence 0, which the targeting stage reads as
 * "leave this note alone".
 */
std::vector<NoteMatch> alignNotesToGuide(const std::vector<SungNote>& sung,
                                         const std::vector<GuideNote>& guide,
                                         const AlignmentParams& params = {});

/// Cost of pairing one sung note with one guide note. Exposed for testing.
float pairCost(const SungNote& sung,
               const GuideNote& guide,
               const AlignmentParams& params);

} // namespace correction
