// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.
#pragma once

#include "MidiFileReader.h"
#include "MidiGuideTypes.h"

#include <string>
#include <vector>

namespace correction
{

/// How a loaded MIDI file is placed against the audio.
///
/// Two of these exist because a MIDI export does not always carry the
/// project's tempo. A file written at the default 120 BPM for a 162 BPM
/// project parses correctly and still lands in the wrong place, stretched by
/// 162/120, so the guide drifts further out of time the longer it runs - the
/// kind of failure that looks like a broken matcher rather than a wrong
/// tempo. Measured on real material: 120 BPM exported, 162 BPM project,
/// 1.35x. The plug-in cannot detect this on its own, so it is exposed.
struct GuidePlacement
{
    /// Tempo of the project the audio belongs to.
    double projectBpm = 120.0;

    /// Tempo the MIDI file was written at, when it disagrees with the
    /// project. Zero means "trust the file", which is correct whenever the
    /// export carried the real tempo.
    double midiFileBpm = 0.0;

    /// Seconds of the (already tempo-corrected) MIDI timeline that line up
    /// with the start of the audio. Positive means the audio starts later.
    double offsetSeconds = 0.0;

    /// Factor applied to MIDI times to reach audio times.
    double timeScale() const
    {
        if (midiFileBpm <= 0.0 || projectBpm <= 0.0)
            return 1.0;
        return midiFileBpm / projectBpm;
    }

    bool isRescaling() const { return timeScale() != 1.0; }
};

/// A loaded guide, ready to correct against.
struct LoadedGuide
{
    std::string fileName;        ///< for display; empty when nothing is loaded
    MidiFileData midi;
    int selectedTrack = -1;      ///< -1 = every track
    GuidePlacement placement;

    /// The guide on the project's frame grid, rebuilt whenever the placement
    /// or the track changes.
    std::vector<GuideNote> notes;

    bool isLoaded() const { return !midi.notes.empty(); }

    /// Where the guide sits in the audio, in seconds. Useful for telling the
    /// user the guide landed outside the take.
    double firstNoteSeconds() const;
    double lastNoteSeconds() const;
};

/// Parse MIDI bytes already read from somewhere into `out`, labelling it
/// `displayName`. Returns false and fills `error` when they cannot be parsed.
///
/// This is the entry point the plug-in uses, because opening the file is the
/// caller's job: on Windows std::ifstream takes a narrow path in the system
/// code page, so a UTF-8 path - which is what juce::String produces - fails
/// to open for any name outside ASCII. JUCE opens files by wide path and
/// gets this right, so the UI hands over bytes rather than a path.
bool loadGuideFromBytes(const std::vector<std::uint8_t>& bytes,
                        const std::string& displayName,
                        LoadedGuide& out,
                        std::string& error);

/// Read a MIDI file from disk into `out`. Returns false and fills `error`
/// on a file that cannot be read or parsed.
///
/// Opens the path with std::ifstream, so it is only safe where the path is
/// ASCII - tests and command-line tools. The plug-in uses
/// loadGuideFromBytes.
bool loadGuideFromFile(const std::string& path,
                       LoadedGuide& out,
                       std::string& error);

/// Rebuild `guide.notes` from the parsed MIDI, the selected track and the
/// placement. Call after changing any of those.
void rebuildGuideNotes(LoadedGuide& guide, double framesPerSecond);

/// Tied notes split by an exporter arrive as several same-pitch notes that
/// touch. Joining them keeps one sung note from facing several guide notes
/// it could equally well match. Exposed for testing.
std::vector<MidiNoteEvent> joinTiedNotes(const std::vector<MidiNoteEvent>& notes,
                                         double toleranceSeconds = 0.03);

} // namespace correction
