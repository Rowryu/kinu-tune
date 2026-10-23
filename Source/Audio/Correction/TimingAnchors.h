// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.
#pragma once

#include "MidiGuideTypes.h"
#include "TempoMap.h"
#include "TimingMap.h"

#include <vector>

namespace correction
{

/// Which moment inside a note is lined up with the target.
enum class TimingAnchor
{
    /// The frame the note starts on.
    ///
    /// This is where a note "begins" in the data, and it is NOT where it is
    /// heard as beginning. A syllable opening on a consonant starts well
    /// before the beat it lands on, so putting note starts on the grid drags
    /// the whole phrase late.
    NoteStart,

    /// The peak of the note's energy.
    ///
    /// Closer to what a listener times a note by: the vowel carries the
    /// weight, and the beat is felt where the sound swells rather than where
    /// the consonant begins. This is what "line the peak up with the grid"
    /// asks for.
    EnergyPeak,

    /// The middle of the stretch where the pitch sits on the note.
    ///
    /// Energy is a poor clock for a sung line: sibilance and breath are loud
    /// and carry no pitch, so a peak can land on an "s" or on a breath before
    /// the note. The pitch does not do that - it arrives when the vowel does
    /// and stays while the note is held - so the middle of that stretch is a
    /// steadier description of where the note is than the loudest moment.
    /// Falls back to the energy peak for a note with no settled pitch, which
    /// is what an unvoiced one is.
    PitchCentre,
};

/// Per-frame loudness of the take, on the analysis frame grid.
/// Root-mean-square over each hop, which is enough to find a peak and is
/// cheap compared with anything spectral.
std::vector<float> framewiseRms(const std::vector<float>& samples,
                                int hopSize);

/**
 * The frame inside [startFrame, endFrame) carrying the note's weight.
 *
 * The maximum is smoothed first and then taken as the centre of the region
 * that is within a few percent of it: a sustained vowel has a broad, flat
 * top where the single loudest frame is wherever the noise happened to land,
 * and moving a note by where a noise peak fell is not what was asked for.
 *
 * Falls back to the note's midpoint when there is no usable energy.
 */
int findEnergyPeakFrame(const std::vector<float>& rms,
                        int startFrame,
                        int endFrame);

/**
 * The middle of the longest run of frames whose pitch is within
 * `toleranceSemitones` of `noteMidi`, or -1 when the note never settles.
 *
 * `midiPitch` is per-frame pitch in MIDI numbers with zero where the frame is
 * unvoiced, which is what makes this immune to consonants and breath: they
 * are not voiced, so they are not in any run.
 */
int findPitchSettleFrame(const std::vector<float>& midiPitch,
                         int startFrame,
                         int endFrame,
                         float noteMidi,
                         float toleranceSemitones = 1.0f);

/// Anchor frame of a note under the chosen rule.
int anchorFrameFor(const std::vector<float>& rms,
                   int startFrame,
                   int endFrame,
                   TimingAnchor anchor);

/// As above, with the pitch track available. PitchCentre needs it; the other
/// rules ignore it, and so does this one when the note never settles.
int anchorFrameFor(const std::vector<float>& rms,
                   const std::vector<float>& midiPitch,
                   int startFrame,
                   int endFrame,
                   float noteMidi,
                   TimingAnchor anchor);

/// A musical grid: where the beats are, in frames.
struct TimingGrid
{
    double framesPerSecond = 86.1328125;
    double bpm = 120.0;
    /// Grid lines per beat. 1 = quarter notes, 2 = eighths, 4 = sixteenths.
    int divisionsPerBeat = 4;
    /// Frame the first beat of the take falls on. A region rarely starts on
    /// a beat, so this carries the host's bar position. Used only when there
    /// is no tempo map.
    double firstBeatFrame = 0.0;

    /// The song's tempo map, when the host gave us one, and where this
    /// region's frame zero sits on the song's timeline.
    ///
    /// With a map the grid is read straight off the host's own beats and
    /// `bpm` and `firstBeatFrame` are ignored. Without one the grid is built
    /// from a single tempo laid down from `firstBeatFrame`, which is right
    /// only for a song that holds one tempo throughout: under a tempo track
    /// such a grid is correct at the start and drifts further out with every
    /// bar after it.
    TempoMap tempoMap;
    double hostOffsetSeconds = 0.0;

    /// Spacing of the grid, in frames. With a tempo map this varies along the
    /// take, so it is the spacing at `frame`; `framesPerDivision()` answers
    /// for the start of the region.
    double framesPerDivisionAt(double frame) const;
    double framesPerDivision() const { return framesPerDivisionAt(0.0); }
    /// Nearest grid frame to `frame`.
    double snap(double frame) const;

private:
    /// Quarter-note position of a frame of this region, and back again.
    double quartersAtFrame(double frame) const;
    double frameAtQuarters(double quarters) const;
};

/**
 * Timing targets that put each note's anchor on the nearest grid line.
 *
 * Notes whose anchor already sits within `toleranceFrames` of a line are
 * returned with no target: a performance is not meant to be exactly on the
 * grid, and moving a note that is already close enough costs audio quality
 * for nothing.
 */
std::vector<TimingTarget> gridTimingTargets(
    const std::vector<SungNote>& notes,
    const std::vector<float>& rms,
    const TimingGrid& grid,
    TimingAnchor anchor = TimingAnchor::EnergyPeak,
    double toleranceFrames = 2.0,
    const std::vector<float>& midiPitch = {});

/**
 * Timing targets that line each sung note up with the guide note it was
 * matched to, anchor against anchor.
 *
 * The guide's anchor is its own start when it came from a MIDI file - there
 * is no audio to find a peak in - and from `guideRms` when it came from a
 * track, which is the better of the two: a sung guide carries the same
 * consonant delay as the take being corrected, so the two anchors describe
 * the same moment.
 */
std::vector<TimingTarget> guidedTimingTargets(
    const std::vector<SungNote>& notes,
    const std::vector<GuideNote>& guide,
    const std::vector<NoteMatch>& matches,
    const std::vector<float>& rms,
    const std::vector<float>& guideRms,
    TimingAnchor anchor = TimingAnchor::EnergyPeak,
    const std::vector<float>& midiPitch = {},
    const std::vector<float>& guideMidiPitch = {});

} // namespace correction
