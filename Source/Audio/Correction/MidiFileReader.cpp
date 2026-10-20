// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.

#include "MidiFileReader.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace correction
{
namespace
{

/// A cursor that refuses to read past the end of the buffer. Every read in
/// the parser goes through this, so a truncated file ends the parse with an
/// error instead of reading other people's memory.
class Reader
{
public:
    Reader(const std::uint8_t* data, size_t size) : data_(data), size_(size) {}

    bool ok() const { return ok_; }
    size_t position() const { return pos_; }
    size_t remaining() const { return pos_ <= size_ ? size_ - pos_ : 0; }

    void fail() { ok_ = false; }

    std::uint8_t byte()
    {
        if (!ok_ || pos_ >= size_)
        {
            ok_ = false;
            return 0;
        }
        return data_[pos_++];
    }

    std::uint32_t be16()
    {
        const std::uint32_t a = byte();
        const std::uint32_t b = byte();
        return (a << 8) | b;
    }

    std::uint32_t be32()
    {
        const std::uint32_t a = byte();
        const std::uint32_t b = byte();
        const std::uint32_t c = byte();
        const std::uint32_t d = byte();
        return (a << 24) | (b << 16) | (c << 8) | d;
    }

    /// MIDI variable-length quantity: seven bits per byte, top bit continues.
    std::uint32_t varLength()
    {
        std::uint32_t value = 0;
        for (int i = 0; i < 4; ++i)
        {
            const std::uint8_t b = byte();
            if (!ok_)
                return 0;
            value = (value << 7) | static_cast<std::uint32_t>(b & 0x7F);
            if ((b & 0x80) == 0)
                return value;
        }
        // A fifth continuation byte is malformed; a valid quantity is at most
        // four bytes.
        ok_ = false;
        return 0;
    }

    void skip(size_t count)
    {
        if (!ok_ || count > remaining())
        {
            ok_ = false;
            return;
        }
        pos_ += count;
    }

    void seek(size_t position)
    {
        if (position > size_)
        {
            ok_ = false;
            return;
        }
        pos_ = position;
    }

    bool matches(const char* tag)
    {
        if (remaining() < 4)
            return false;
        for (int i = 0; i < 4; ++i)
            if (data_[pos_ + static_cast<size_t>(i)] !=
                static_cast<std::uint8_t>(tag[i]))
                return false;
        return true;
    }

private:
    const std::uint8_t* data_ = nullptr;
    size_t size_ = 0;
    size_t pos_ = 0;
    bool ok_ = true;
};

/// Tempo changes, as (tick, microseconds per quarter note).
using TempoMap = std::vector<std::pair<std::uint64_t, std::uint32_t>>;

/// Convert a tick position to seconds, walking the tempo map.
double ticksToSeconds(std::uint64_t tick, const TempoMap& tempos,
                      std::uint32_t ticksPerQuarter)
{
    if (ticksPerQuarter == 0)
        return 0.0;

    double seconds = 0.0;
    std::uint64_t lastTick = 0;
    std::uint32_t microsPerQuarter = 500000; // 120 bpm, the MIDI default

    for (const auto& change : tempos)
    {
        if (change.first >= tick)
            break;
        const std::uint64_t span = change.first - lastTick;
        seconds += static_cast<double>(span) *
                   static_cast<double>(microsPerQuarter) /
                   (static_cast<double>(ticksPerQuarter) * 1e6);
        lastTick = change.first;
        microsPerQuarter = change.second;
    }

    const std::uint64_t span = tick - lastTick;
    seconds += static_cast<double>(span) * static_cast<double>(microsPerQuarter) /
               (static_cast<double>(ticksPerQuarter) * 1e6);
    return seconds;
}

/// A note that has sounded but not yet stopped.
struct PendingNote
{
    std::uint64_t startTick = 0;
    int velocity = 0;
};

} // namespace

bool parseMidiFile(const std::vector<std::uint8_t>& bytes,
                   MidiFileData& out,
                   std::string& error)
{
    out = MidiFileData{};
    error.clear();

    Reader reader(bytes.data(), bytes.size());

    if (!reader.matches("MThd"))
    {
        error = "not a MIDI file (no MThd header)";
        return false;
    }
    reader.skip(4);

    const std::uint32_t headerLength = reader.be32();
    if (!reader.ok() || headerLength < 6)
    {
        error = "truncated MIDI header";
        return false;
    }

    const size_t headerEnd = reader.position() + headerLength;
    const std::uint32_t format = reader.be16();
    const std::uint32_t numTracks = reader.be16();
    const std::uint32_t division = reader.be16();
    if (!reader.ok())
    {
        error = "truncated MIDI header";
        return false;
    }

    if ((division & 0x8000) != 0)
    {
        // SMPTE time division. Refusing is better than guessing: the guide's
        // timing would be silently wrong, and these files are rare.
        error = "SMPTE time division is not supported; export the MIDI with "
                "ticks-per-quarter-note timing";
        return false;
    }

    const std::uint32_t ticksPerQuarter = division & 0x7FFF;
    if (ticksPerQuarter == 0)
    {
        error = "MIDI header declares zero ticks per quarter note";
        return false;
    }

    out.format = static_cast<int>(format);
    out.numTracks = static_cast<int>(numTracks);

    reader.seek(headerEnd);
    if (!reader.ok())
    {
        error = "truncated MIDI header";
        return false;
    }

    // Tempo changes apply across the whole file, so they are gathered first
    // and the notes are converted to seconds afterwards.
    TempoMap tempos;
    struct RawNote
    {
        std::uint64_t startTick;
        std::uint64_t endTick;
        int midiNote;
        int channel;
        int track;
        int velocity;
    };
    std::vector<RawNote> rawNotes;
    std::uint64_t maxTick = 0;

    for (std::uint32_t trackIndex = 0; trackIndex < numTracks; ++trackIndex)
    {
        if (reader.remaining() == 0)
            break; // fewer tracks than the header promised; take what we have

        if (!reader.matches("MTrk"))
        {
            error = "expected a track chunk (MTrk) at track " +
                    std::to_string(trackIndex);
            return false;
        }
        reader.skip(4);

        const std::uint32_t trackLength = reader.be32();
        if (!reader.ok() || trackLength > reader.remaining())
        {
            error = "truncated track " + std::to_string(trackIndex);
            return false;
        }

        const size_t trackEnd = reader.position() + trackLength;

        std::uint64_t tick = 0;
        std::uint8_t runningStatus = 0;
        // Keyed by (channel << 8) | note, because the same pitch can sound on
        // two channels at once.
        std::map<int, PendingNote> pending;

        while (reader.ok() && reader.position() < trackEnd)
        {
            tick += reader.varLength();
            if (!reader.ok())
                break;

            std::uint8_t status = reader.byte();
            if (!reader.ok())
                break;

            if (status < 0x80)
            {
                // Running status: this byte is data, and the previous status
                // applies. Rewind one byte so the data reads line up.
                if (runningStatus == 0)
                {
                    error = "running status before any status byte in track " +
                            std::to_string(trackIndex);
                    return false;
                }
                reader.seek(reader.position() - 1);
                status = runningStatus;
            }
            else if (status < 0xF0)
            {
                runningStatus = status;
            }

            if (status == 0xFF)
            {
                const std::uint8_t metaType = reader.byte();
                const std::uint32_t length = reader.varLength();
                if (!reader.ok() || length > reader.remaining())
                {
                    error = "truncated meta event in track " +
                            std::to_string(trackIndex);
                    return false;
                }

                if (metaType == 0x51 && length == 3)
                {
                    const std::uint32_t a = reader.byte();
                    const std::uint32_t b = reader.byte();
                    const std::uint32_t c = reader.byte();
                    tempos.emplace_back(tick, (a << 16) | (b << 8) | c);
                }
                else
                {
                    reader.skip(length);
                }

                if (metaType == 0x2F)
                    break; // end of track
                continue;
            }

            if (status == 0xF0 || status == 0xF7)
            {
                const std::uint32_t length = reader.varLength();
                if (!reader.ok() || length > reader.remaining())
                {
                    error = "truncated sysex in track " +
                            std::to_string(trackIndex);
                    return false;
                }
                reader.skip(length);
                runningStatus = 0; // sysex cancels running status
                continue;
            }

            const std::uint8_t messageType = status & 0xF0;
            const int channel = status & 0x0F;

            switch (messageType)
            {
                case 0x80: // note off
                case 0x90: // note on
                {
                    const int note = reader.byte() & 0x7F;
                    const int velocity = reader.byte() & 0x7F;
                    if (!reader.ok())
                        break;

                    const int key = (channel << 8) | note;
                    // A note-on with zero velocity is a note-off; many files
                    // use it so running status can cover a whole phrase.
                    const bool isNoteOn = (messageType == 0x90) && velocity > 0;

                    if (isNoteOn)
                    {
                        // A second note-on for a sounding pitch ends the
                        // first, rather than losing it.
                        auto existing = pending.find(key);
                        if (existing != pending.end())
                            rawNotes.push_back({existing->second.startTick, tick,
                                                note, channel,
                                                static_cast<int>(trackIndex),
                                                existing->second.velocity});
                        pending[key] = PendingNote{tick, velocity};
                    }
                    else
                    {
                        auto found = pending.find(key);
                        if (found != pending.end())
                        {
                            rawNotes.push_back({found->second.startTick, tick,
                                                note, channel,
                                                static_cast<int>(trackIndex),
                                                found->second.velocity});
                            pending.erase(found);
                        }
                    }
                    break;
                }

                case 0xA0: // polyphonic aftertouch
                case 0xB0: // control change
                case 0xE0: // pitch bend
                    reader.skip(2);
                    break;

                case 0xC0: // program change
                case 0xD0: // channel aftertouch
                    reader.skip(1);
                    break;

                default:
                    error = "unexpected status byte in track " +
                            std::to_string(trackIndex);
                    return false;
            }
        }

        if (!reader.ok())
        {
            error = "truncated event data in track " + std::to_string(trackIndex);
            return false;
        }

        // Notes still sounding at the end of the track end there, rather than
        // being dropped.
        for (const auto& entry : pending)
            rawNotes.push_back({entry.second.startTick, tick,
                                entry.first & 0xFF, (entry.first >> 8) & 0x0F,
                                static_cast<int>(trackIndex),
                                entry.second.velocity});

        maxTick = std::max(maxTick, tick);
        reader.seek(trackEnd);
    }

    std::sort(tempos.begin(), tempos.end());

    out.notes.reserve(rawNotes.size());
    for (const auto& raw : rawNotes)
    {
        if (raw.endTick <= raw.startTick)
            continue; // zero-length note: nothing to guide with

        MidiNoteEvent note;
        note.startSeconds = ticksToSeconds(raw.startTick, tempos, ticksPerQuarter);
        note.endSeconds = ticksToSeconds(raw.endTick, tempos, ticksPerQuarter);
        note.midiNote = raw.midiNote;
        note.channel = raw.channel;
        note.track = raw.track;
        note.velocity = raw.velocity;
        out.notes.push_back(note);
    }

    std::sort(out.notes.begin(), out.notes.end(),
              [](const MidiNoteEvent& a, const MidiNoteEvent& b) {
                  if (a.startSeconds != b.startSeconds)
                      return a.startSeconds < b.startSeconds;
                  return a.midiNote < b.midiNote;
              });

    out.durationSeconds = ticksToSeconds(maxTick, tempos, ticksPerQuarter);

    for (const auto& note : out.notes)
        if (std::find(out.tracksWithNotes.begin(), out.tracksWithNotes.end(),
                      note.track) == out.tracksWithNotes.end())
            out.tracksWithNotes.push_back(note.track);
    std::sort(out.tracksWithNotes.begin(), out.tracksWithNotes.end());

    return true;
}

std::vector<GuideNote> toGuideNotes(const MidiFileData& data,
                                    const GuideConversionOptions& options)
{
    const double fps =
        options.framesPerSecond > 0.0 ? options.framesPerSecond : 1.0;

    std::vector<MidiNoteEvent> selected;
    for (const auto& note : data.notes)
        if (options.track < 0 || note.track == options.track)
            selected.push_back(note);

    if (options.forceMonophonic)
    {
        // Highest note wins, because the melody sits above any harmony
        // written on the same track. Sorting by start then descending pitch
        // puts the winner first at every onset.
        std::sort(selected.begin(), selected.end(),
                  [](const MidiNoteEvent& a, const MidiNoteEvent& b) {
                      if (a.startSeconds != b.startSeconds)
                          return a.startSeconds < b.startSeconds;
                      return a.midiNote > b.midiNote;
                  });

        std::vector<MidiNoteEvent> mono;
        for (const auto& note : selected)
        {
            if (!mono.empty())
            {
                MidiNoteEvent& previous = mono.back();
                if (note.startSeconds < previous.endSeconds)
                {
                    if (note.startSeconds <= previous.startSeconds)
                    {
                        // Same onset: the earlier entry is the higher note.
                        continue;
                    }
                    // Overlap: trim the note before so the guide never asks
                    // for two pitches at once.
                    previous.endSeconds = note.startSeconds;
                    if (previous.endSeconds <= previous.startSeconds)
                    {
                        mono.pop_back();
                        mono.push_back(note);
                        continue;
                    }
                }
            }
            mono.push_back(note);
        }
        selected.swap(mono);
    }

    std::vector<GuideNote> guide;
    guide.reserve(selected.size());

    for (const auto& note : selected)
    {
        const double start =
            (note.startSeconds - options.timelineOffsetSeconds) * fps;
        const double end =
            (note.endSeconds - options.timelineOffsetSeconds) * fps;

        GuideNote g;
        g.startFrame = static_cast<int>(std::lround(start));
        g.endFrame = static_cast<int>(std::lround(end));
        g.midiNote = static_cast<float>(note.midiNote);

        if (g.endFrame <= g.startFrame)
            g.endFrame = g.startFrame + 1;

        guide.push_back(g);
    }

    return guide;
}

} // namespace correction
