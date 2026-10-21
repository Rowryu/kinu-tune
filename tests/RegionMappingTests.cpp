// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.

// Mapping note frames from one track's analysis grid to another's.
//
// A project's frames are in its audio modification's own time, so two tracks
// have unrelated frame numbers even when they sound together. Getting this
// wrong does not crash anything - it silently points the guide at the wrong
// moment - so the cases here are mostly about trims, offsets and differing
// sample rates rather than about failure.

#include "../Source/Audio/Correction/RegionMapping.h"
#include "TestAssert.h"

#include <cmath>
#include <iostream>

using namespace correction;

namespace
{

bool near(double a, double b, double tol = 1e-6)
{
    return std::abs(a - b) <= tol;
}

RegionPlacement placement(double startInPlayback,
                          double startInModification = 0.0,
                          double sampleRate = 44100.0,
                          double duration = 0.0)
{
    RegionPlacement p;
    p.startInPlaybackSeconds = startInPlayback;
    p.startInModificationSeconds = startInModification;
    p.sampleRate = sampleRate;
    p.hopSize = 512;
    p.durationSeconds = duration;
    return p;
}

GuideNote note(int start, int end, float midi)
{
    GuideNote n;
    n.startFrame = start;
    n.endFrame = end;
    n.midiNote = midi;
    return n;
}

} // namespace

int main()
{
    const double fps = 44100.0 / 512.0;

    // --- frames to the host timeline and back ----------------------------
    {
        // A region starting 10 s into the song, untrimmed.
        const auto r = placement(10.0);

        CHECK(near(frameToPlaybackSeconds(0.0, r), 10.0));
        CHECK(near(frameToPlaybackSeconds(fps, r), 11.0, 1e-9));
        CHECK(near(playbackSecondsToFrame(11.0, r), fps, 1e-9));

        // Round trip at an arbitrary point.
        CHECK(near(playbackSecondsToFrame(frameToPlaybackSeconds(123.4, r), r),
                   123.4, 1e-9));
    }

    // --- a left trim shifts where the material starts ---------------------
    {
        // The region begins 10 s into the song but 2 s into its own audio,
        // so frame 0 of the analysis sounds 2 s BEFORE the region starts.
        const auto r = placement(10.0, 2.0);

        CHECK(near(frameToPlaybackSeconds(0.0, r), 8.0));
        CHECK(near(playbackSecondsToFrame(10.0, r), 2.0 * fps, 1e-9));
    }

    // --- two tracks, same timeline ---------------------------------------
    {
        // Guide starts at the top of the song; the vocal region starts at 30 s.
        const auto guide = placement(0.0);
        const auto vocal = placement(30.0);

        // A guide note at 30 s on the timeline is frame 0 of the vocal.
        const double guideFrameAt30s = 30.0 * fps;
        CHECK(near(mapFrameBetweenRegions(guideFrameAt30s, guide, vocal), 0.0,
                   1e-6));

        // And one second later is one second of frames in.
        CHECK(near(mapFrameBetweenRegions(31.0 * fps, guide, vocal), fps, 1e-6));

        // The mapping is invertible.
        CHECK(near(mapFrameBetweenRegions(
                       mapFrameBetweenRegions(500.0, guide, vocal), vocal, guide),
                   500.0, 1e-6));
    }

    // --- differing sample rates ------------------------------------------
    {
        // Mixing a 48 kHz guide with a 44.1 kHz vocal: a frame is a different
        // length of time on each side, which is the whole reason the mapping
        // goes through seconds rather than scaling frame numbers.
        const auto guide = placement(0.0, 0.0, 48000.0);
        const auto vocal = placement(0.0, 0.0, 44100.0);

        const double oneSecondOfGuideFrames = 48000.0 / 512.0;
        const double oneSecondOfVocalFrames = 44100.0 / 512.0;

        CHECK(near(mapFrameBetweenRegions(oneSecondOfGuideFrames, guide, vocal),
                   oneSecondOfVocalFrames, 1e-6));
    }

    // --- identity when the placements match -------------------------------
    {
        const auto r = placement(12.5, 3.25, 48000.0);
        for (double f : {0.0, 1.0, 97.5, 10000.0})
            CHECK(near(mapFrameBetweenRegions(f, r, r), f, 1e-6));
    }

    // --- converting a guide track's notes ---------------------------------
    {
        const auto guide = placement(0.0, 0.0, 44100.0);
        // The vocal region covers 30 s to 34 s of the song.
        const auto vocal = placement(30.0, 0.0, 44100.0, 4.0);

        const std::vector<GuideNote> notes = {
            note(static_cast<int>(std::lround(10.0 * fps)),
                 static_cast<int>(std::lround(11.0 * fps)), 60.0f), // 10-11s
            note(static_cast<int>(std::lround(31.0 * fps)),
                 static_cast<int>(std::lround(32.0 * fps)), 62.0f), // 31-32s
            note(static_cast<int>(std::lround(32.0 * fps)),
                 static_cast<int>(std::lround(33.0 * fps)), 64.0f), // 32-33s
            note(static_cast<int>(std::lround(90.0 * fps)),
                 static_cast<int>(std::lround(91.0 * fps)), 67.0f), // 90-91s
        };

        const auto mapped = guideNotesFromTrack(notes, guide, vocal);

        // The notes at 10 s and 90 s are nowhere near the region: dropped.
        CHECK(mapped.size() == 2);
        CHECK(near(mapped[0].midiNote, 62.0f, 1e-4));
        CHECK(near(mapped[1].midiNote, 64.0f, 1e-4));

        // 31 s is one second into a region that starts at 30 s.
        CHECK(std::abs(mapped[0].startFrame - std::lround(1.0 * fps)) <= 1);
        CHECK(std::abs(mapped[1].startFrame - std::lround(2.0 * fps)) <= 1);

        // Results come out in time order.
        CHECK(mapped[0].startFrame < mapped[1].startFrame);
    }

    // --- a note straddling the region edge is kept whole -------------------
    {
        const auto guide = placement(0.0);
        const auto vocal = placement(30.0, 0.0, 44100.0, 4.0);

        // 29.5 s to 30.5 s: half outside the region.
        const std::vector<GuideNote> notes = {
            note(static_cast<int>(std::lround(29.5 * fps)),
                 static_cast<int>(std::lround(30.5 * fps)), 60.0f)};

        const auto mapped = guideNotesFromTrack(notes, guide, vocal);
        CHECK(mapped.size() == 1);
        // Kept with its original length: trimming would move the centre the
        // correction aims at.
        CHECK(mapped[0].startFrame < 0);
        CHECK(std::abs((mapped[0].endFrame - mapped[0].startFrame) -
                       std::lround(1.0 * fps)) <= 1);
    }

    // --- unbounded target keeps everything --------------------------------
    {
        const auto guide = placement(0.0);
        const auto vocal = placement(30.0); // duration 0 = unbounded

        const std::vector<GuideNote> notes = {
            note(0, 10, 60.0f),
            note(static_cast<int>(std::lround(90.0 * fps)),
                 static_cast<int>(std::lround(91.0 * fps)), 67.0f)};

        CHECK(guideNotesFromTrack(notes, guide, vocal).size() == 2);
    }

    // --- degenerate inputs -------------------------------------------------
    {
        const auto r = placement(0.0);
        CHECK(guideNotesFromTrack({}, r, r).empty());

        // Inverted and zero-length notes are dropped rather than produced.
        const std::vector<GuideNote> bad = {note(100, 50, 60.0f),
                                            note(10, 10, 60.0f)};
        CHECK(guideNotesFromTrack(bad, r, r).empty());

        // A placement with no usable grid must not divide by zero.
        RegionPlacement broken;
        broken.sampleRate = 0.0;
        broken.hopSize = 0;
        CHECK(near(broken.secondsPerFrame(), 0.0));
        CHECK(guideNotesFromTrack({note(0, 10, 60.0f)}, broken, broken).empty());
    }

    std::cout << "Region mapping tests passed\n";
}
