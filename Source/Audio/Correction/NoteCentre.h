// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.
#pragma once

#include <vector>

namespace correction
{

struct NoteCentreParams
{
    /// A frame whose pitch moves faster than this, in semitones per frame,
    /// is a transition rather than the held note (about 13 st/s at 86 fps).
    float steepSemitonesPerFrame = 0.15f;

    /// A frame quieter than this share of the note's loudest frame counts as
    /// "not really sung yet / any more".
    float quietShareOfPeak = 0.5f;

    /// Weight kept by a frame that is both steep and quiet. Not zero, so a
    /// note that is nothing but a slide still has a centre.
    float transitionWeight = 0.02f;
};

/**
 * Where a sung note was aimed, as a listener hears it.
 *
 * A note's head and tail - the scoop into it and the fall out of it - move
 * fast and are sung quietly, and they barely affect whether the note sounds
 * in tune. The held, loud part decides that. An average over every frame lets
 * a long scoop drag the centre toward it, and correcting from that centre
 * then moves a note that was already in tune. Here each frame is weighted by
 * its loudness, frames that are both steep and quiet are all but ignored, and
 * the centre is the weighted median, so a brief excursion cannot pull it.
 *
 * Inputs are per analysis frame: f0 in Hz (0 = unvoiced) and RMS. Returns the
 * centre in MIDI semitones, or a negative number if no frame was voiced.
 */
float perceivedCentreMidi(const std::vector<float>& f0Hz,
                          const std::vector<float>& rms,
                          const NoteCentreParams& params = {});

} // namespace correction
