// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.
#pragma once

#include <vector>

namespace correction
{

/// One sung note's measured position and where timing correction wants it.
struct TimingTarget
{
    int sourceStartFrame = 0;
    int sourceEndFrame = 0; ///< exclusive

    /// Where this note should start. Ignored when `hasTarget` is false, which
    /// is how an unmatched note says "leave my timing to the neighbours".
    double targetStartFrame = 0.0;
    /// Where this note should end. Only used when `correctNoteLength` is on.
    double targetEndFrame = 0.0;

    bool hasTarget = false;
    /// 0..1, from note matching. Scales the move like it scales pitch, so a
    /// doubtful match cannot yank a syllable across the bar.
    float confidence = 1.0f;
};

struct TimingParams
{
    double framesPerSecond = 86.1328125; // 44100 / 512

    /// 0 = leave timing alone, 1 = put every note exactly on its target.
    float correctionStrength = 0.75f;

    /// Hard ceiling on how far a note may move, in seconds.
    float maxShiftSeconds = 0.25f;

    /// Whether note ends are corrected too, or only onsets.
    bool correctNoteLength = true;

    /// Bounds on the local playback rate, as source frames per output frame.
    /// 0.5 means the audio may be stretched to at most twice its length and
    /// 2.0 that it may be compressed to at most half. Time stretching is what
    /// costs quality, so this is the control that decides how audible the
    /// correction is, regardless of how far notes are asked to move.
    ///
    /// The defaults are deliberately narrow. A sung note moved under a grid
    /// or a guide is not itself stretched - its start and end shift together
    /// - so what absorbs the move is whatever sits between it and its
    /// neighbour. In a legato phrase there is nothing between them, and the
    /// previous note's tail takes the whole of it: shifting a note 50 ms
    /// earlier compresses a 150 ms neighbour by a third. At the old bounds
    /// that was permitted, and it is what "the audio sounds stretched" is.
    ///
    /// A sixth either way is about where a stretched vowel stops passing for
    /// an unstretched one. Past that the map stops short rather than warping
    /// the audio to obey, which is the trade worth making: a note that lands
    /// a little late still sounds like singing, and one dragged there does
    /// not.
    double minRate = 0.85;
    double maxRate = 1.18;

    /// Matches below this confidence are not moved.
    float minConfidence = 0.25f;
};

/// A monotonic source-to-output mapping, ready for DspSynthesisRequest.
struct TimingMap
{
    /// Per output frame: the fractional frame index to read from the original
    /// audio. This is exactly DspSynthesisRequest::sourceFrame.
    std::vector<double> sourceFrame;

    /// Where each input note ended up, in output frames, in the input's
    /// order. Note i spans [noteStart[i], noteEnd[i]).
    std::vector<double> noteStart;
    std::vector<double> noteEnd;

    int numOutputFrames() const { return static_cast<int>(sourceFrame.size()); }
};

/**
 * Build a time map that moves notes towards their targets.
 *
 * The map is built from anchors - a source position paired with the output
 * position it should land on - and then interpolated. Working in anchors
 * rather than cutting and sliding audio is what lets one pass follow the
 * whole correction continuously: there are no splices to crossfade and no
 * silences to fill, because every output frame reads from somewhere.
 *
 * Two guarantees hold whatever the inputs: the map never goes backwards, and
 * the local rate stays inside [minRate, maxRate]. The second is the audible
 * one - a note asked to move further than the rate budget allows gets as far
 * as the budget permits rather than being dragged there and smeared.
 *
 * `sourceFrameCount` is the length of the analysed audio.
 */
TimingMap buildTimingMap(const std::vector<TimingTarget>& notes,
                         int sourceFrameCount,
                         const TimingParams& params = {});

} // namespace correction
