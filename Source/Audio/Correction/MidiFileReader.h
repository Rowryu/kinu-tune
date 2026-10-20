// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.
#pragma once

#include "MidiGuideTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace correction
{

/// One note from a MIDI file, in seconds from the start of the file.
struct MidiNoteEvent
{
    double startSeconds = 0.0;
    double endSeconds = 0.0;
    int midiNote = 60;
    int channel = 0;
    int track = 0;
    int velocity = 0;
};

struct MidiFileData
{
    std::vector<MidiNoteEvent> notes; ///< sorted by start, then pitch
    double durationSeconds = 0.0;
    int format = 0;
    int numTracks = 0;
    /// Tracks that contained at least one note, so a caller can offer a
    /// choice when the file has more than one.
    std::vector<int> tracksWithNotes;
};

/**
 * Parse a Standard MIDI File.
 *
 * Hand-written rather than taken from JUCE so the guide path can be tested
 * without a plug-in host, and so a malformed file is a reported error rather
 * than an assertion in the middle of a DAW.
 *
 * Handles format 0, 1 and 2, running status, tempo changes, note-on with
 * velocity zero as a note-off, and notes left unterminated at the end of a
 * track. Every read is bounds-checked: a truncated or corrupt file returns
 * false with a reason rather than reading past the end.
 *
 * Returns false and fills `error` on a file it cannot read.
 */
bool parseMidiFile(const std::vector<std::uint8_t>& bytes,
                   MidiFileData& out,
                   std::string& error);

/// Options for turning parsed MIDI into a pitch/timing guide.
struct GuideConversionOptions
{
    double framesPerSecond = 86.1328125; // 44100 / 512

    /// Seconds of the MIDI timeline that line up with frame 0 of the audio.
    /// Positive values mean the audio starts later than the MIDI file.
    double timelineOffsetSeconds = 0.0;

    /// Only take notes from this track; -1 takes every track.
    int track = -1;

    /// A vocal line is one note at a time. When several notes overlap, keep
    /// the highest - the melody, rather than a harmony part underneath it -
    /// and trim the one before it so the guide stays monophonic.
    bool forceMonophonic = true;
};

/**
 * Convert parsed MIDI into guide notes on the project's frame grid.
 *
 * A guide has to be monophonic to be usable: note matching pairs each sung
 * note with one guide note, and a chord would offer several answers for the
 * same moment with no way to choose.
 */
std::vector<GuideNote> toGuideNotes(const MidiFileData& data,
                                    const GuideConversionOptions& options = {});

} // namespace correction
