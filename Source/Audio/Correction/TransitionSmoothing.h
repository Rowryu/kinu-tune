// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.
#pragma once

#include <vector>

namespace correction
{

/// One note, as far as choosing a transition length is concerned.
struct TransitionNote
{
    int startFrame = 0;
    int endFrame = 0;   ///< exclusive
    float midiNote = 60.0f;
    bool isRest = false;

    int durationFrames() const { return endFrame - startFrame; }
};

/// The transition lengths chosen for one note's two edges, in frames.
struct NoteTransition
{
    int noteIndex = 0;
    int leftFrames = 0;
    int rightFrames = 0;
};

struct TransitionParams
{
    double framesPerSecond = 86.1328125;

    /// 0 = leave every transition as it is, 1 = the full computed length.
    float amount = 0.5f;

    /// Transition length for a one-semitone step, in seconds. Bigger
    /// intervals get proportionally longer, up to maxSeconds.
    double secondsPerSemitone = 0.012;

    /// Floor and ceiling on a transition, in seconds.
    double minSeconds = 0.025;
    double maxSeconds = 0.12;

    /// A transition may never take more than this share of the shorter of
    /// the two notes it joins.
    double maxShareOfNote = 0.35;

    /// Notes further apart than this are treated as separate phrases rather
    /// than a joined interval, and get no smoothing. A leap of an octave is
    /// usually meant to be heard as a leap.
    float maxIntervalSemitones = 12.0f;
};

/**
 * Choose how long each note-to-note transition should be.
 *
 * A fixed length cannot be right for both a semitone step and a fifth: the
 * same number of frames reads as a smear on one and as an edit on the other.
 * The length here grows with the interval and is then bounded by the notes
 * themselves, because a transition that eats a third of a short note stops
 * sounding like a transition.
 *
 * Only edges between two sounding notes that touch are smoothed. A note
 * followed by a rest, or by a gap, ends on its own and has nothing to glide
 * into; smoothing there would bend the release of a phrase towards silence.
 *
 * Returns one entry per note that gets any smoothing, in note order. Notes
 * absent from the result keep whatever they already had.
 */
std::vector<NoteTransition> planTransitions(
    const std::vector<TransitionNote>& notes,
    const TransitionParams& params = {});

} // namespace correction
