// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.

// Unit tests for the Standard MIDI File reader and guide conversion.
//
// The files under test are built byte by byte here rather than checked in, so
// each case states exactly which part of the format it exercises.
//
// Free of JUCE and the Project model so it builds with a plain g++ call.

#include "../Source/Audio/Correction/MidiFileReader.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace correction;

namespace
{

int failures = 0;
int checks = 0;

void check(bool condition, const std::string& what)
{
    ++checks;
    if (!condition)
    {
        ++failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

void checkNear(double actual, double expected, double tolerance,
               const std::string& what)
{
    ++checks;
    if (std::abs(actual - expected) > tolerance)
    {
        ++failures;
        std::printf("  FAIL: %s (got %.5f, expected %.5f +/- %.5f)\n",
                    what.c_str(), actual, expected, tolerance);
    }
}

// --- a minimal SMF builder ----------------------------------------------

using Bytes = std::vector<std::uint8_t>;

void putBE16(Bytes& b, std::uint32_t v)
{
    b.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

void putBE32(Bytes& b, std::uint32_t v)
{
    b.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
    b.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    b.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    b.push_back(static_cast<std::uint8_t>(v & 0xFF));
}

void putVar(Bytes& b, std::uint32_t value)
{
    std::uint8_t buffer[4];
    int count = 0;
    buffer[count++] = static_cast<std::uint8_t>(value & 0x7F);
    value >>= 7;
    while (value > 0 && count < 4)
    {
        buffer[count++] = static_cast<std::uint8_t>((value & 0x7F) | 0x80);
        value >>= 7;
    }
    for (int i = count - 1; i >= 0; --i)
        b.push_back(buffer[i]);
}

void putTag(Bytes& b, const char* tag)
{
    for (int i = 0; i < 4; ++i)
        b.push_back(static_cast<std::uint8_t>(tag[i]));
}

/// Wrap raw event bytes in an MTrk chunk, appending end-of-track.
Bytes makeTrack(const Bytes& events, bool withEndOfTrack = true)
{
    Bytes body = events;
    if (withEndOfTrack)
    {
        putVar(body, 0);
        body.push_back(0xFF);
        body.push_back(0x2F);
        putVar(body, 0);
    }

    Bytes track;
    putTag(track, "MTrk");
    putBE32(track, static_cast<std::uint32_t>(body.size()));
    track.insert(track.end(), body.begin(), body.end());
    return track;
}

Bytes makeFile(const std::vector<Bytes>& tracks, std::uint32_t ticksPerQuarter,
               std::uint32_t format = 1)
{
    Bytes file;
    putTag(file, "MThd");
    putBE32(file, 6);
    putBE16(file, format);
    putBE16(file, static_cast<std::uint32_t>(tracks.size()));
    putBE16(file, ticksPerQuarter);
    for (const auto& t : tracks)
        file.insert(file.end(), t.begin(), t.end());
    return file;
}

void noteOn(Bytes& b, std::uint32_t delta, int channel, int note, int velocity)
{
    putVar(b, delta);
    b.push_back(static_cast<std::uint8_t>(0x90 | (channel & 0x0F)));
    b.push_back(static_cast<std::uint8_t>(note));
    b.push_back(static_cast<std::uint8_t>(velocity));
}

void noteOff(Bytes& b, std::uint32_t delta, int channel, int note)
{
    putVar(b, delta);
    b.push_back(static_cast<std::uint8_t>(0x80 | (channel & 0x0F)));
    b.push_back(static_cast<std::uint8_t>(note));
    b.push_back(0);
}

void setTempo(Bytes& b, std::uint32_t delta, std::uint32_t microsPerQuarter)
{
    putVar(b, delta);
    b.push_back(0xFF);
    b.push_back(0x51);
    putVar(b, 3);
    b.push_back(static_cast<std::uint8_t>((microsPerQuarter >> 16) & 0xFF));
    b.push_back(static_cast<std::uint8_t>((microsPerQuarter >> 8) & 0xFF));
    b.push_back(static_cast<std::uint8_t>(microsPerQuarter & 0xFF));
}

// --- tests ---------------------------------------------------------------

void testSimpleMelody()
{
    std::printf("a simple melody is read back\n");

    // 480 ticks per quarter, default 120 bpm: a quarter note is 0.5 s.
    Bytes ev;
    noteOn(ev, 0, 0, 60, 100);
    noteOff(ev, 480, 0, 60);
    noteOn(ev, 0, 0, 62, 100);
    noteOff(ev, 480, 0, 62);

    const auto file = makeFile({makeTrack(ev)}, 480);

    MidiFileData data;
    std::string error;
    const bool parsed = parseMidiFile(file, data, error);
    check(parsed, "the file parses: " + error);
    check(data.notes.size() == 2, "two notes");
    if (data.notes.size() < 2)
        return;

    check(data.notes[0].midiNote == 60, "first note is C4");
    check(data.notes[1].midiNote == 62, "second note is D4");
    checkNear(data.notes[0].startSeconds, 0.0, 1e-6, "first note starts at 0");
    checkNear(data.notes[0].endSeconds, 0.5, 1e-6, "first note is half a second");
    checkNear(data.notes[1].startSeconds, 0.5, 1e-6, "second note follows");
    checkNear(data.durationSeconds, 1.0, 1e-6, "the file is one second long");
}

void testRunningStatus()
{
    std::printf("running status is followed\n");

    // One 0x90 status byte, then bare data pairs: note-on, then note-on with
    // velocity 0 standing in for note-off.
    Bytes ev;
    putVar(ev, 0);
    ev.push_back(0x90);
    ev.push_back(60);
    ev.push_back(100);
    putVar(ev, 480);
    ev.push_back(60);
    ev.push_back(0); // velocity 0 = note off
    putVar(ev, 0);
    ev.push_back(64);
    ev.push_back(90);
    putVar(ev, 480);
    ev.push_back(64);
    ev.push_back(0);

    const auto file = makeFile({makeTrack(ev)}, 480);

    MidiFileData data;
    std::string error;
    const bool parsed = parseMidiFile(file, data, error);
    check(parsed, "the file parses: " + error);
    check(data.notes.size() == 2, "two notes despite the single status byte");
    if (data.notes.size() < 2)
        return;
    check(data.notes[0].midiNote == 60 && data.notes[1].midiNote == 64,
          "both pitches survive");
    checkNear(data.notes[0].endSeconds, 0.5, 1e-6,
              "velocity zero ended the first note");
}

void testTempoChange()
{
    std::printf("tempo changes shift later notes\n");

    // Start at 120 bpm, double to 240 bpm after the first quarter note.
    Bytes ev;
    setTempo(ev, 0, 500000); // 120 bpm
    noteOn(ev, 0, 0, 60, 100);
    noteOff(ev, 480, 0, 60);
    setTempo(ev, 0, 250000); // 240 bpm
    noteOn(ev, 0, 0, 62, 100);
    noteOff(ev, 480, 0, 62);

    const auto file = makeFile({makeTrack(ev)}, 480);

    MidiFileData data;
    std::string error;
    const bool parsed = parseMidiFile(file, data, error);
    check(parsed, "the file parses: " + error);
    if (data.notes.size() < 2)
    {
        check(false, "two notes");
        return;
    }

    checkNear(data.notes[0].endSeconds, 0.5, 1e-6, "the first note is slow");
    checkNear(data.notes[1].endSeconds - data.notes[1].startSeconds, 0.25, 1e-6,
              "the second note is twice as fast");
}

void testUnterminatedNote()
{
    std::printf("a note left sounding ends with its track\n");

    Bytes ev;
    noteOn(ev, 0, 0, 67, 100);
    // Time passes and the track ends with the note still sounding. The
    // end-of-track meta is written here rather than by makeTrack so that this
    // delta is the only one before it.
    putVar(ev, 960);
    ev.push_back(0xFF);
    ev.push_back(0x2F);
    putVar(ev, 0);

    const auto file = makeFile({makeTrack(ev, false)}, 480);

    MidiFileData data;
    std::string error;
    const bool parsed = parseMidiFile(file, data, error);
    check(parsed, "the file parses: " + error);
    check(data.notes.size() == 1, "the note is kept, not dropped");
    if (data.notes.empty())
        return;
    check(data.notes[0].endSeconds > data.notes[0].startSeconds,
          "it has a real length");
}

void testMultipleTracks()
{
    std::printf("tracks are reported and can be filtered\n");

    Bytes melody;
    noteOn(melody, 0, 0, 72, 100);
    noteOff(melody, 480, 0, 72);

    Bytes bass;
    noteOn(bass, 0, 1, 48, 100);
    noteOff(bass, 480, 1, 48);

    const auto file = makeFile({makeTrack(melody), makeTrack(bass)}, 480);

    MidiFileData data;
    std::string error;
    const bool parsed = parseMidiFile(file, data, error);
    check(parsed, "the file parses: " + error);
    check(data.notes.size() == 2, "both tracks contribute");
    check(data.tracksWithNotes.size() == 2, "both tracks are listed");

    GuideConversionOptions options;
    options.track = 0;
    const auto guide = toGuideNotes(data, options);
    check(guide.size() == 1, "filtering to track 0 keeps one note");
    if (!guide.empty())
        checkNear(guide[0].midiNote, 72.0, 1e-6, "and it is the melody note");
}

void testMonophonicReduction()
{
    std::printf("a chord is reduced to its top note\n");

    // A C major triad, all starting together.
    Bytes ev;
    noteOn(ev, 0, 0, 60, 100);
    noteOn(ev, 0, 0, 64, 100);
    noteOn(ev, 0, 0, 67, 100);
    noteOff(ev, 480, 0, 60);
    noteOff(ev, 0, 0, 64);
    noteOff(ev, 0, 0, 67);

    const auto file = makeFile({makeTrack(ev)}, 480);

    MidiFileData data;
    std::string error;
    const bool parsed = parseMidiFile(file, data, error);
    check(parsed, "the file parses: " + error);
    check(data.notes.size() == 3, "all three notes are parsed");

    const auto guide = toGuideNotes(data, {});
    check(guide.size() == 1, "the guide has one note at that moment");
    if (!guide.empty())
        checkNear(guide[0].midiNote, 67.0, 1e-6, "the melody note wins");
}

void testOverlapIsTrimmed()
{
    std::printf("an overlapping note trims the one before it\n");

    // A legato pair: the second starts before the first ends.
    Bytes ev;
    noteOn(ev, 0, 0, 60, 100);
    noteOn(ev, 240, 0, 62, 100);
    noteOff(ev, 240, 0, 60);
    noteOff(ev, 240, 0, 62);

    const auto file = makeFile({makeTrack(ev)}, 480);

    MidiFileData data;
    std::string error;
    const bool parsed = parseMidiFile(file, data, error);
    check(parsed, "the file parses: " + error);

    const auto guide = toGuideNotes(data, {});
    check(guide.size() == 2, "both notes survive");
    if (guide.size() < 2)
        return;
    check(guide[0].endFrame <= guide[1].startFrame,
          "the guide never asks for two pitches at once");
}

void testFrameConversionAndOffset()
{
    std::printf("seconds become frames, with the timeline offset applied\n");

    Bytes ev;
    noteOn(ev, 0, 0, 60, 100);
    noteOff(ev, 480, 0, 60);

    const auto file = makeFile({makeTrack(ev)}, 480);

    MidiFileData data;
    std::string error;
    const bool parsed = parseMidiFile(file, data, error);
    check(parsed, "the file parses: " + error);

    GuideConversionOptions options;
    options.framesPerSecond = 100.0; // round numbers for the assertion
    const auto guide = toGuideNotes(data, options);
    check(guide.size() == 1, "one note");
    if (guide.empty())
        return;
    check(guide[0].startFrame == 0, "it starts at frame 0");
    check(guide[0].endFrame == 50, "and is half a second long");

    // The audio starts 0.2 s into the MIDI timeline.
    options.timelineOffsetSeconds = 0.2;
    const auto shifted = toGuideNotes(data, options);
    check(shifted[0].startFrame == -20, "the offset moves the guide earlier");
}

void testSmpteDivisionIsRefused()
{
    std::printf("SMPTE timing is refused rather than guessed\n");

    Bytes ev;
    noteOn(ev, 0, 0, 60, 100);
    noteOff(ev, 480, 0, 60);
    auto file = makeFile({makeTrack(ev)}, 480);
    // Set the division's top bit, which marks SMPTE timing.
    file[12] = 0xE7; // -25 fps
    file[13] = 40;

    MidiFileData data;
    std::string error;
    check(!parseMidiFile(file, data, error), "parsing fails");
    check(!error.empty(), "and says why");
}

void testMalformedFilesAreRejected()
{
    std::printf("malformed files are rejected without reading past the end\n");

    MidiFileData data;
    std::string error;

    check(!parseMidiFile({}, data, error), "an empty file is rejected");
    check(!parseMidiFile({'M', 'T', 'h', 'd'}, data, error),
          "a header with no length is rejected");

    Bytes ev;
    noteOn(ev, 0, 0, 60, 100);
    noteOff(ev, 480, 0, 60);
    const auto good = makeFile({makeTrack(ev)}, 480);

    // Every truncation of a valid file must fail cleanly rather than crash.
    // ASan and UBSan are what make this test worth running.
    for (size_t cut = 1; cut < good.size(); ++cut)
    {
        Bytes truncated(good.begin(), good.begin() + static_cast<long>(cut));
        MidiFileData d;
        std::string e;
        parseMidiFile(truncated, d, e); // must not crash; result is irrelevant
    }
    check(true, "every truncation was handled");

    // A track whose declared length runs past the file.
    Bytes bad;
    putTag(bad, "MThd");
    putBE32(bad, 6);
    putBE16(bad, 1);
    putBE16(bad, 1);
    putBE16(bad, 480);
    putTag(bad, "MTrk");
    putBE32(bad, 0xFFFF);
    check(!parseMidiFile(bad, data, error), "an overlong track is rejected");
}

void testEmptyGuideIsAllowed()
{
    std::printf("a file with no notes gives an empty guide\n");

    const auto file = makeFile({makeTrack({})}, 480);

    MidiFileData data;
    std::string error;
    const bool parsed = parseMidiFile(file, data, error);
    check(parsed, "the file parses: " + error);
    check(data.notes.empty(), "no notes");
    check(toGuideNotes(data, {}).empty(), "and no guide notes");
}

} // namespace

int main()
{
    std::printf("=== MIDI file reader tests ===\n\n");

    testSimpleMelody();
    testRunningStatus();
    testTempoChange();
    testUnterminatedNote();
    testMultipleTracks();
    testMonophonicReduction();
    testOverlapIsTrimmed();
    testFrameConversionAndOffset();
    testSmpteDivisionIsRefused();
    testMalformedFilesAreRejected();
    testEmptyGuideIsAllowed();

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
