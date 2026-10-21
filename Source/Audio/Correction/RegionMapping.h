// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.
#pragma once

#include "MidiGuideTypes.h"

#include <vector>

namespace correction
{

/**
 * Where one playback region sits, and on what grid its notes are measured.
 *
 * A project's note frames are in its audio modification's own time, starting
 * at the modification, with no timeline padding - the analysis does not bake
 * in where the host placed the region. Two tracks therefore have unrelated
 * frame numbers even when they sound at the same moment, and the host
 * timeline is the only thing they share.
 */
struct RegionPlacement
{
    /// Where this region begins on the host timeline, in seconds.
    double startInPlaybackSeconds = 0.0;

    /// How far into the audio modification this region starts, in seconds.
    /// A region trimmed at its left edge starts part-way into the material.
    double startInModificationSeconds = 0.0;

    /// Sample rate of the analysis that produced the frames.
    double sampleRate = 44100.0;

    /// Frames per hop of that analysis.
    int hopSize = 512;

    /// Length of the region on the host timeline, in seconds. Zero means
    /// "unbounded", which is what a caller passes when it does not care.
    double durationSeconds = 0.0;

    double secondsPerFrame() const
    {
        if (sampleRate <= 0.0 || hopSize <= 0)
            return 0.0;
        return static_cast<double>(hopSize) / sampleRate;
    }
};

/// Host-timeline position, in seconds, of a frame in `region`.
double frameToPlaybackSeconds(double frame, const RegionPlacement& region);

/// Frame in `region` of a host-timeline position.
double playbackSecondsToFrame(double seconds, const RegionPlacement& region);

/// Map a frame from one region's grid to another's, through the host
/// timeline. Returns the fractional frame; callers round as they see fit.
double mapFrameBetweenRegions(double frame,
                              const RegionPlacement& from,
                              const RegionPlacement& to);

/**
 * Convert notes analysed on one track into guide notes on another's grid.
 *
 * `notes` are (startFrame, endFrame, midiNote) triples read from the guide
 * track's project. Notes that land entirely outside the target region are
 * dropped: a guide track usually runs the length of the song while the region
 * being corrected is one phrase, and a guide note nowhere near the audio can
 * only mislead the matcher.
 *
 * Rests must be filtered out by the caller; this does not know about them.
 */
std::vector<GuideNote> guideNotesFromTrack(
    const std::vector<GuideNote>& notes,
    const RegionPlacement& from,
    const RegionPlacement& to);

} // namespace correction
