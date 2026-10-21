// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.

#include "TransitionSmoothing.h"

#include <algorithm>
#include <cmath>

namespace correction
{
namespace
{

/// True when `later` starts where `earlier` ends, give or take a frame.
/// Analysis boundaries are integers, so a one-frame gap is rounding rather
/// than a real silence.
bool touches(const TransitionNote& earlier, const TransitionNote& later)
{
    const int gap = later.startFrame - earlier.endFrame;
    return gap >= -1 && gap <= 1;
}

} // namespace

std::vector<NoteTransition> planTransitions(
    const std::vector<TransitionNote>& notes,
    const TransitionParams& params)
{
    std::vector<NoteTransition> result;
    if (notes.size() < 2)
        return result;

    const float amount = std::clamp(params.amount, 0.0f, 1.0f);
    if (amount <= 0.0f)
        return result;

    const double fps =
        params.framesPerSecond > 0.0 ? params.framesPerSecond : 1.0;
    const auto toFrames = [fps](double seconds)
    { return seconds * fps; };

    // Indexed by note, so both sides of a join land on the same entry.
    std::vector<NoteTransition> perNote(notes.size());
    for (size_t i = 0; i < notes.size(); ++i)
        perNote[i].noteIndex = static_cast<int>(i);

    bool any = false;

    for (size_t i = 0; i + 1 < notes.size(); ++i)
    {
        const auto& left = notes[i];
        const auto& right = notes[i + 1];

        // A rest on either side means the phrase breaks here. Gliding into
        // silence, or out of it, bends a release that was sung as a release.
        if (left.isRest || right.isRest)
            continue;
        if (left.durationFrames() <= 0 || right.durationFrames() <= 0)
            continue;
        if (!touches(left, right))
            continue;

        const float interval = std::abs(right.midiNote - left.midiNote);
        if (interval <= 0.0f)
            continue; // a repeated pitch has nothing to glide across
        if (interval > params.maxIntervalSemitones)
            continue; // a leap is meant to be heard as a leap

        // Longer for a wider interval, then bounded at both ends.
        double seconds = params.secondsPerSemitone * static_cast<double>(interval);
        seconds = std::clamp(seconds, params.minSeconds, params.maxSeconds);

        double frames = toFrames(seconds) * static_cast<double>(amount);

        // A transition is a join, so it may not take over either of the
        // notes it joins. The shorter one decides.
        const double share = std::clamp(params.maxShareOfNote, 0.0, 0.5);
        const double budget =
            share * static_cast<double>(std::min(left.durationFrames(),
                                                 right.durationFrames()));
        frames = std::min(frames, budget);

        const int rounded = static_cast<int>(std::lround(frames));
        if (rounded <= 0)
            continue;

        // The join is shared: the earlier note's right edge and the later
        // note's left edge describe the same glide.
        perNote[i].rightFrames = rounded;
        perNote[i + 1].leftFrames = rounded;
        any = true;
    }

    if (!any)
        return result;

    for (const auto& entry : perNote)
        if (entry.leftFrames > 0 || entry.rightFrames > 0)
            result.push_back(entry);

    return result;
}

} // namespace correction
