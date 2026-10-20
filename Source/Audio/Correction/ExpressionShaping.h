// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.
#pragma once

#include <vector>

namespace correction
{

/// Half-open frame range of one note on the project's frame grid.
struct NoteSpan
{
    int startFrame = 0;
    int endFrame = 0; ///< exclusive
};

/**
 * Controls for how much of the singer's own pitch movement survives.
 *
 * These operate on deltaPitch - the per-frame offset from the note centre, in
 * MIDI semitones - so they are independent of where the note itself sits.
 * Defaults keep everything the singer did.
 */
struct ExpressionParams
{
    /// Frames per second of the mel grid.
    double framesPerSecond = 86.1328125; // 44100 / 512

    /// 1 = keep all of the slow pitch movement (portamento, slow scoops and
    /// falls), 0 = flatten it onto the note centre.
    float expressionPreservation = 1.0f;

    /// 1 = keep the fast movement exactly, 0 = remove it. This is the vibrato
    /// band; see the note on fast scoops in the .cpp.
    float vibratoPreservation = 1.0f;

    /// 1 = remove each note's slow pitch drift entirely, 0 = leave it. Drift
    /// is the linear trend across a note's interior: the singer sagging or
    /// creeping over a long note. Scoops and falls live at the note edges and
    /// are deliberately kept out of the fit.
    float pitchDriftCorrection = 0.0f;

    /// Boundary between "slow movement" and "vibrato", in Hz. Human vibrato
    /// sits around 4-7 Hz; portamento and slow scoops are well below.
    float vibratoCutoffHz = 3.5f;

    /// Fraction of a note at each end excluded from its drift fit, so an
    /// onset scoop or a release fall cannot be mistaken for drift.
    float edgeExclusionRatio = 0.2f;
};

/**
 * Reshape a take's deltaPitch according to the expression controls.
 *
 * `deltaPitch` is the whole take's dense curve (Project::deltaPitch); each
 * note is then shaped over its own frames. deltaPitch is measured from each
 * note's centre, so it steps at every note boundary, and the band split has
 * to stay on one side of that step - see the .cpp for the measurements behind
 * this and for what it costs at a note's first and last half-window.
 *
 * Frames not covered by any note are passed through untouched.
 *
 * Returns a curve the same length as the input. With default parameters the
 * output equals the input, so the stage is a no-op until the user asks for
 * something.
 */
std::vector<float> shapeDeltaPitch(const std::vector<float>& deltaPitch,
                                   const std::vector<NoteSpan>& notes,
                                   const ExpressionParams& params = {});

/// Zero-phase moving average over the whole curve. Exposed for testing.
std::vector<float> zeroPhaseMovingAverage(const std::vector<float>& input,
                                          int windowFrames);

} // namespace correction
