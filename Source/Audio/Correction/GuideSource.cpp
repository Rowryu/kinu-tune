// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.

#include "GuideSource.h"

#include <algorithm>
#include <fstream>
#include <iterator>

namespace correction
{

double LoadedGuide::firstNoteSeconds() const
{
    if (notes.empty())
        return 0.0;
    return static_cast<double>(notes.front().startFrame);
}

double LoadedGuide::lastNoteSeconds() const
{
    if (notes.empty())
        return 0.0;
    return static_cast<double>(notes.back().endFrame);
}

std::vector<MidiNoteEvent> joinTiedNotes(const std::vector<MidiNoteEvent>& notes,
                                         double toleranceSeconds)
{
    std::vector<MidiNoteEvent> joined;
    joined.reserve(notes.size());

    for (const auto& note : notes)
    {
        if (!joined.empty())
        {
            MidiNoteEvent& previous = joined.back();
            const bool samePitch = previous.midiNote == note.midiNote;
            const bool sameTrack = previous.track == note.track;
            const bool touching =
                note.startSeconds - previous.endSeconds <= toleranceSeconds &&
                note.startSeconds >= previous.startSeconds;

            if (samePitch && sameTrack && touching)
            {
                previous.endSeconds = std::max(previous.endSeconds, note.endSeconds);
                continue;
            }
        }
        joined.push_back(note);
    }

    return joined;
}

bool loadGuideFromFile(const std::string& path,
                       LoadedGuide& out,
                       std::string& error)
{
    error.clear();

    std::ifstream in(path, std::ios::binary);
    if (!in)
    {
        error = "could not open the file";
        return false;
    }

    const std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                          std::istreambuf_iterator<char>());
    if (bytes.empty())
    {
        error = "the file is empty";
        return false;
    }

    MidiFileData parsed;
    if (!parseMidiFile(bytes, parsed, error))
        return false;

    if (parsed.notes.empty())
    {
        error = "the file contains no notes";
        return false;
    }

    out.midi = std::move(parsed);

    const auto slash = path.find_last_of("/\\");
    out.fileName = slash == std::string::npos ? path : path.substr(slash + 1);

    // A file with notes on exactly one track needs no choosing.
    out.selectedTrack = out.midi.tracksWithNotes.size() == 1
                            ? out.midi.tracksWithNotes.front()
                            : -1;

    out.notes.clear();
    return true;
}

void rebuildGuideNotes(LoadedGuide& guide, double framesPerSecond)
{
    guide.notes.clear();
    if (!guide.isLoaded())
        return;

    // Join ties before anything else, so the placement works on the notes
    // the matcher will actually see.
    MidiFileData joined = guide.midi;
    joined.notes = joinTiedNotes(guide.midi.notes);

    GuideConversionOptions options;
    options.framesPerSecond = framesPerSecond;
    options.track = guide.selectedTrack;
    options.forceMonophonic = true;

    // Rescale first, then offset. Doing it in this order keeps the two
    // controls independent: a tempo correction does not move an offset the
    // user has already dialled in, which it would if the offset were
    // expressed on the file's own timeline and scaled along with it.
    const double scale = guide.placement.timeScale();
    if (scale != 1.0)
        for (auto& note : joined.notes)
        {
            note.startSeconds *= scale;
            note.endSeconds *= scale;
        }

    options.timelineOffsetSeconds = guide.placement.offsetSeconds;

    guide.notes = toGuideNotes(joined, options);
}

} // namespace correction
