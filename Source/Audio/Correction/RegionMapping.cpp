// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.

#include "RegionMapping.h"

#include <algorithm>
#include <cmath>

namespace correction
{

double frameToPlaybackSeconds(double frame, const RegionPlacement& region)
{
    const double secondsPerFrame = region.secondsPerFrame();
    if (secondsPerFrame <= 0.0)
        return region.startInPlaybackSeconds;

    const double inModification = frame * secondsPerFrame;
    return region.startInPlaybackSeconds +
           (inModification - region.startInModificationSeconds);
}

double playbackSecondsToFrame(double seconds, const RegionPlacement& region)
{
    const double secondsPerFrame = region.secondsPerFrame();
    if (secondsPerFrame <= 0.0)
        return 0.0;

    const double inModification = region.startInModificationSeconds +
                                  (seconds - region.startInPlaybackSeconds);
    return inModification / secondsPerFrame;
}

double mapFrameBetweenRegions(double frame,
                              const RegionPlacement& from,
                              const RegionPlacement& to)
{
    return playbackSecondsToFrame(frameToPlaybackSeconds(frame, from), to);
}

std::vector<GuideNote> guideNotesFromTrack(
    const std::vector<GuideNote>& notes,
    const RegionPlacement& from,
    const RegionPlacement& to)
{
    std::vector<GuideNote> result;
    result.reserve(notes.size());

    // The window the target region actually covers, in its own frames. A
    // duration of zero means the caller does not want the clipping.
    const double targetSecondsPerFrame = to.secondsPerFrame();
    const bool bounded = to.durationSeconds > 0.0 && targetSecondsPerFrame > 0.0;
    const double targetStartFrame =
        bounded ? playbackSecondsToFrame(to.startInPlaybackSeconds, to) : 0.0;
    const double targetEndFrame =
        bounded ? playbackSecondsToFrame(
                      to.startInPlaybackSeconds + to.durationSeconds, to)
                : 0.0;

    for (const auto& note : notes)
    {
        const double start =
            mapFrameBetweenRegions(static_cast<double>(note.startFrame), from, to);
        const double end =
            mapFrameBetweenRegions(static_cast<double>(note.endFrame), from, to);

        if (end <= start)
            continue;

        // Drop only notes with no overlap at all. A note straddling the edge
        // is kept whole: trimming it would move its centre, and the centre is
        // what the correction aims at.
        if (bounded && (end <= targetStartFrame || start >= targetEndFrame))
            continue;

        GuideNote mapped;
        mapped.startFrame = static_cast<int>(std::lround(start));
        mapped.endFrame = static_cast<int>(std::lround(end));
        if (mapped.endFrame <= mapped.startFrame)
            mapped.endFrame = mapped.startFrame + 1;
        mapped.midiNote = note.midiNote;

        // The anchor is a position in the same frames as the start and end,
        // so it travels the same way.
        if (note.anchorFrame >= 0)
        {
            const double anchor = mapFrameBetweenRegions(
                static_cast<double>(note.anchorFrame), from, to);
            mapped.anchorFrame = static_cast<int>(std::lround(anchor));
        }

        result.push_back(mapped);
    }

    std::sort(result.begin(), result.end(),
              [](const GuideNote& a, const GuideNote& b)
              { return a.startFrame < b.startFrame; });

    return result;
}

} // namespace correction
