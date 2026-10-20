// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.

// Guide placement: tie joining, tempo correction and timeline offset.
//
// The tempo case is not hypothetical. A MIDI export of a 162 BPM project
// arrived written at the default 120 BPM; the guide parsed correctly and
// still drifted, because every time in it was 1.35x too long.

#include "../Source/Audio/Correction/GuideSource.h"
#include "TestAssert.h"

#include <cmath>
#include <iostream>

using namespace correction;

namespace
{

constexpr double kFps = 86.1328125;

MidiNoteEvent ev(double start, double end, int midi, int track = 0)
{
    MidiNoteEvent n;
    n.startSeconds = start;
    n.endSeconds = end;
    n.midiNote = midi;
    n.track = track;
    return n;
}

bool near(double a, double b, double tol = 1e-6)
{
    return std::abs(a - b) <= tol;
}

} // namespace

int main()
{
    // --- tie joining ------------------------------------------------------
    {
        // An exporter split one held note into three touching pieces.
        const std::vector<MidiNoteEvent> split = {
            ev(0.0, 0.5, 60), ev(0.5, 1.0, 60), ev(1.0, 1.5, 60)};
        const auto joined = joinTiedNotes(split);
        CHECK(joined.size() == 1);
        CHECK(near(joined[0].startSeconds, 0.0));
        CHECK(near(joined[0].endSeconds, 1.5));

        // A real repetition has a gap and must survive as two notes.
        const std::vector<MidiNoteEvent> repeated = {ev(0.0, 0.4, 60),
                                                     ev(0.5, 1.0, 60)};
        CHECK(joinTiedNotes(repeated).size() == 2);

        // Different pitches are never joined, touching or not.
        const std::vector<MidiNoteEvent> stepwise = {ev(0.0, 0.5, 60),
                                                     ev(0.5, 1.0, 62)};
        CHECK(joinTiedNotes(stepwise).size() == 2);

        // Nor are same-pitch notes on different tracks.
        const std::vector<MidiNoteEvent> twoTracks = {ev(0.0, 0.5, 60, 0),
                                                      ev(0.5, 1.0, 60, 1)};
        CHECK(joinTiedNotes(twoTracks).size() == 2);

        CHECK(joinTiedNotes({}).empty());
    }

    // --- no rescale when the file carries the project tempo ---------------
    {
        LoadedGuide g;
        g.midi.notes = {ev(1.0, 2.0, 60)};
        g.placement.projectBpm = 162.0;
        g.placement.midiFileBpm = 0.0; // trust the file
        CHECK(near(g.placement.timeScale(), 1.0));
        CHECK(!g.placement.isRescaling());

        rebuildGuideNotes(g, kFps);
        CHECK(g.notes.size() == 1);
        CHECK(g.notes[0].startFrame == static_cast<int>(std::lround(1.0 * kFps)));
    }

    // --- the 120-into-162 case --------------------------------------------
    {
        LoadedGuide g;
        // A note the exporter wrote at 1.35 s because it used 120 BPM; at the
        // project's 162 BPM it belongs at 1.0 s.
        g.midi.notes = {ev(1.35, 2.70, 60)};
        g.placement.projectBpm = 162.0;
        g.placement.midiFileBpm = 120.0;

        CHECK(near(g.placement.timeScale(), 120.0 / 162.0, 1e-12));
        CHECK(g.placement.isRescaling());

        rebuildGuideNotes(g, kFps);
        CHECK(g.notes.size() == 1);
        CHECK(near(g.notes[0].startFrame, std::lround(1.0 * kFps), 1.0));
        CHECK(near(g.notes[0].endFrame, std::lround(2.0 * kFps), 1.0));
    }

    // --- offset is applied on the audio timeline, after the rescale -------
    {
        // Changing the tempo must not move an offset the user already set,
        // so the same offset has to mean the same number of audio frames
        // whatever the scale is.
        LoadedGuide plain;
        plain.midi.notes = {ev(1.0, 2.0, 60)};
        plain.placement.offsetSeconds = 0.5;
        rebuildGuideNotes(plain, kFps);

        LoadedGuide scaled;
        scaled.midi.notes = {ev(1.35, 2.70, 60)};
        scaled.placement.projectBpm = 162.0;
        scaled.placement.midiFileBpm = 120.0;
        scaled.placement.offsetSeconds = 0.5;
        rebuildGuideNotes(scaled, kFps);

        // Both describe a note at 1.0 s shifted back by 0.5 s.
        CHECK(near(plain.notes[0].startFrame, scaled.notes[0].startFrame, 1.0));
        CHECK(near(plain.notes[0].startFrame,
                   std::lround(0.5 * kFps), 1.0));
    }

    // --- track selection ---------------------------------------------------
    {
        LoadedGuide g;
        g.midi.notes = {ev(0.0, 1.0, 60, 0), ev(0.0, 1.0, 48, 1)};
        g.selectedTrack = 1;
        rebuildGuideNotes(g, kFps);
        CHECK(g.notes.size() == 1);
        CHECK(near(g.notes[0].midiNote, 48.0f, 1e-4));

        g.selectedTrack = -1; // every track, reduced to one voice
        rebuildGuideNotes(g, kFps);
        CHECK(g.notes.size() == 1);
        CHECK(near(g.notes[0].midiNote, 60.0f, 1e-4)); // the higher wins
    }

    // --- degenerate placements are survivable ------------------------------
    {
        LoadedGuide g;
        g.midi.notes = {ev(0.0, 1.0, 60)};

        g.placement.projectBpm = 0.0; // nonsense; must not divide by zero
        g.placement.midiFileBpm = 120.0;
        CHECK(near(g.placement.timeScale(), 1.0));
        rebuildGuideNotes(g, kFps);
        CHECK(g.notes.size() == 1);

        g.placement.projectBpm = 162.0;
        g.placement.midiFileBpm = -5.0;
        CHECK(near(g.placement.timeScale(), 1.0));

        LoadedGuide empty;
        rebuildGuideNotes(empty, kFps);
        CHECK(empty.notes.empty());
        CHECK(!empty.isLoaded());
    }

    // --- loading reports failures rather than asserting --------------------
    {
        LoadedGuide g;
        std::string error;
        CHECK(!loadGuideFromFile("/nonexistent/path/to/guide.mid", g, error));
        CHECK(!error.empty());

        CHECK(!loadGuideFromBytes({}, "empty.mid", g, error));
        CHECK(!error.empty());

        const std::vector<std::uint8_t> garbage = {'n', 'o', 'p', 'e'};
        CHECK(!loadGuideFromBytes(garbage, "garbage.mid", g, error));
        CHECK(!error.empty());
    }

    // --- a non-ASCII file name survives ------------------------------------
    {
        // The plug-in hands over bytes and a display name precisely so a
        // Japanese file name cannot be mangled by a narrow-path open, which
        // is what broke the first build someone tried.
        const std::vector<std::uint8_t> bytes = {
            'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 0, 0, 1, 0x01, 0xE0,
            'M', 'T', 'r', 'k', 0, 0, 0, 12,
            0x00, 0x90, 60, 100,
            0x81, 0x60, 0x80, 60, 0,
            0x00, 0xFF, 0x2F, 0x00};

        LoadedGuide g;
        std::string error;
        // Universal character names rather than literal bytes, so the
        // test says the same thing whatever encoding this file is saved
        // in. These three are "\u30ac\u30a4\u30c9" - guide.
        const std::string name = u8"\u30ac\u30a4\u30c9.mid";
        CHECK(loadGuideFromBytes(bytes, "C:\\music\\" + name, g, error));
        CHECK(g.fileName == name);
        CHECK(g.midi.notes.size() == 1);
    }

    std::cout << "Guide source tests passed\n";
}
