// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.
#pragma once

#include <array>
#include <optional>
#include <vector>

namespace correction
{

enum class ChordQuality
{
    Major,
    Minor,
    Dominant7,
    Minor7,
    Diminished,
    Sus4
};

/// One chord held over a span of analysis frames. root < 0 means no chord
/// (silence, drums only, or nothing the templates recognise).
struct ChordSpan
{
    int startFrame = 0;
    int endFrame = 0; ///< exclusive
    int root = -1;    ///< pitch class, 0 = C
    ChordQuality quality = ChordQuality::Major;
    /// Pitch classes of the chord as bits (bit 0 = C).
    unsigned toneMask = 0;
};

using Chroma = std::array<float, 12>;

/// Pitch-class energy per analysis frame. `hopSamples` is the analysis hop
/// in samples at `sampleRate`, so frame i of the result lines up with frame i
/// of the project the audio belongs to. Frames are computed every `stride`
/// hops and held in between: chords do not change faster than that.
std::vector<Chroma> chromagram(const std::vector<float>& mono,
                               double sampleRate,
                               double hopSamples,
                               int frameCount,
                               int stride = 4);

/// The chord sequence that best explains the chromagram: template matching
/// against major, minor, 7th, minor 7th, diminished and sus4 chords on every
/// root, smoothed with a switch penalty so a passing note does not become a
/// chord change. Spans shorter than `minChordSeconds` are folded into a
/// neighbour.
std::vector<ChordSpan> estimateChords(const std::vector<Chroma>& chroma,
                                      double framesPerSecond,
                                      double minChordSeconds = 0.3);

/// Tone mask of the chord that covers most of [startFrame, endFrame), or 0
/// when no chord covers it.
unsigned chordMaskFor(const std::vector<ChordSpan>& chords,
                      int startFrame,
                      int endFrame);

/**
 * Move a diatonic harmony note onto the chord when it clashes with it.
 *
 * `melodyMidi` is the sung note, `diatonicTarget` the harmony the key alone
 * suggests (a third above, say). If the target is a chord tone it stands.
 * Otherwise the nearest chord tone within two semitones of it is taken,
 * keeping it on the same side of the melody and at least a minor third away
 * from it - a harmony a second from the melody is the clash this exists to
 * avoid. Tones in the key are preferred. With no chord, or no such tone, the
 * diatonic target is returned unchanged.
 *
 * `scaleMask` is the key's pitch classes as bits (bit 0 = C).
 */
int chordAwareTarget(int melodyMidi,
                     int diatonicTarget,
                     unsigned chordMask,
                     unsigned scaleMask);

} // namespace correction
