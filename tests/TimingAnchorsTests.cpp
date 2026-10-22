// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.

// Anchoring timing correction on where a note is heard rather than where its
// data begins, and lining those anchors up with a grid or a guide.

#include "../Source/Audio/Correction/TimingAnchors.h"
#include "TestAssert.h"

#include <cmath>
#include <iostream>

using namespace correction;

namespace
{

constexpr double kFps = 86.1328125;

SungNote sung(int start, int end, float midi = 60.0f)
{
    SungNote n;
    n.startFrame = start;
    n.endFrame = end;
    n.midiNote = midi;
    return n;
}

GuideNote guide(int start, int end, float midi = 60.0f)
{
    GuideNote n;
    n.startFrame = start;
    n.endFrame = end;
    n.midiNote = midi;
    return n;
}

/// A syllable: a quiet consonant, then the vowel swelling and holding.
std::vector<float> syllable(int totalFrames, int noteStart, int consonant,
                            int body)
{
    std::vector<float> rms(static_cast<size_t>(totalFrames), 0.0f);
    for (int i = 0; i < consonant; ++i)
        rms[static_cast<size_t>(noteStart + i)] = 0.05f;
    for (int i = 0; i < body; ++i)
        rms[static_cast<size_t>(noteStart + consonant + i)] = 0.8f;
    return rms;
}

bool near(double a, double b, double tol)
{
    return std::abs(a - b) <= tol;
}

} // namespace

int main()
{
    // --- framewise RMS -----------------------------------------------------
    {
        std::vector<float> samples(1024, 0.5f);
        const auto rms = framewiseRms(samples, 512);
        CHECK(rms.size() == 2);
        CHECK(near(rms[0], 0.5, 1e-5));

        CHECK(framewiseRms({}, 512).empty());
        CHECK(framewiseRms(samples, 0).empty());
    }

    // --- the peak is the vowel, not the consonant --------------------------
    {
        // Note runs 100..140. Frames 100-109 are consonant, 110-139 vowel.
        const auto rms = syllable(200, 100, 10, 30);
        const int peak = findEnergyPeakFrame(rms, 100, 140);

        // The centre of the vowel, well after the note's own start.
        CHECK(peak > 115);
        CHECK(peak < 135);
        CHECK(anchorFrameFor(rms, 100, 140, TimingAnchor::NoteStart) == 100);
        CHECK(anchorFrameFor(rms, 100, 140, TimingAnchor::EnergyPeak) == peak);
    }

    // --- a lone noisy frame does not become the peak -----------------------
    {
        auto rms = syllable(200, 100, 10, 30);
        rms[101] = 1.0f; // a click during the consonant
        const int peak = findEnergyPeakFrame(rms, 100, 140);
        CHECK(peak > 115); // still the vowel
    }

    // --- silence and degenerate spans fall back to the midpoint ------------
    {
        const std::vector<float> silent(200, 0.0f);
        CHECK(findEnergyPeakFrame(silent, 100, 140) == 120);
        CHECK(findEnergyPeakFrame(silent, 100, 100) == 100);
        CHECK(findEnergyPeakFrame({}, 100, 140) == 0);
        // A span past the end of the data must not read out of bounds.
        CHECK(findEnergyPeakFrame(silent, 190, 400) >= 190);
    }

    // --- the grid ----------------------------------------------------------
    {
        TimingGrid grid;
        grid.framesPerSecond = kFps;
        grid.bpm = 120.0;       // half a second per beat
        grid.divisionsPerBeat = 4; // sixteenths: 0.125 s
        grid.firstBeatFrame = 0.0;

        const double step = grid.framesPerDivision();
        CHECK(near(step, 0.125 * kFps, 1e-6));

        CHECK(near(grid.snap(0.0), 0.0, 1e-9));
        CHECK(near(grid.snap(step * 2.0 + 0.4), step * 2.0, 1e-6));
        CHECK(near(grid.snap(step * 2.0 - 0.4), step * 2.0, 1e-6));

        // An offset grid: the take does not start on a beat.
        grid.firstBeatFrame = 17.0;
        CHECK(near(grid.snap(17.0), 17.0, 1e-9));
        CHECK(near(grid.snap(17.0 + step + 0.3), 17.0 + step, 1e-6));

        TimingGrid broken;
        broken.bpm = 0.0;
        CHECK(near(broken.framesPerDivision(), 0.0, 1e-12));
        CHECK(near(broken.snap(42.0), 42.0, 1e-12)); // no divide by zero
    }

    // --- the grid is anchored where it is told, not at frame zero ---------
    {
        // A region that starts 28.3 s into a 120 bpm song: its frame zero is
        // not on a beat, so a grid left at zero lands between the host's
        // beats. firstBeatFrame carries that, and without it grid mode snaps
        // notes to lines the song does not have.
        TimingGrid grid;
        grid.framesPerSecond = kFps;
        grid.bpm = 120.0;
        grid.divisionsPerBeat = 4;

        const double secondsPerBeat = 0.5;
        const double offset = 28.3;
        const double beatBefore =
            std::floor(offset / secondsPerBeat) * secondsPerBeat; // 28.0
        grid.firstBeatFrame = (beatBefore - offset) * kFps;       // negative

        CHECK(grid.firstBeatFrame < 0.0);

        // A project frame whose host time is exactly a beat must snap to
        // itself.
        const double hostBeat = 29.0;
        const double frameOfHostBeat = (hostBeat - offset) * kFps;
        CHECK(near(grid.snap(frameOfHostBeat), frameOfHostBeat, 0.001));

        // And the unanchored grid would NOT have put a line there.
        TimingGrid unanchored = grid;
        unanchored.firstBeatFrame = 0.0;
        CHECK(!near(unanchored.snap(frameOfHostBeat), frameOfHostBeat, 0.5));
    }

    // --- grid targets move the peak, not the note start --------------------
    {
        TimingGrid grid;
        grid.framesPerSecond = kFps;
        grid.bpm = 120.0;
        grid.divisionsPerBeat = 4;
        const double step = grid.framesPerDivision();

        // One syllable whose vowel peak sits well off the grid.
        const auto rms = syllable(400, 100, 10, 30);
        const int peak = findEnergyPeakFrame(rms, 100, 140);
        const double snapped = grid.snap(static_cast<double>(peak));

        const auto targets = gridTimingTargets({sung(100, 140)}, rms, grid,
                                               TimingAnchor::EnergyPeak, 0.0);
        CHECK(targets.size() == 1);
        CHECK(targets[0].hasTarget);

        // The whole note moves by however far the PEAK had to travel.
        const double shift = snapped - static_cast<double>(peak);
        CHECK(near(targets[0].targetStartFrame, 100.0 + shift, 1e-6));
        CHECK(near(targets[0].targetEndFrame, 140.0 + shift, 1e-6));

        // Anchoring on the start instead gives a different answer, which is
        // the whole point of the choice.
        const auto startAnchored = gridTimingTargets(
            {sung(100, 140)}, rms, grid, TimingAnchor::NoteStart, 0.0);
        CHECK(startAnchored[0].hasTarget);
        CHECK(!near(startAnchored[0].targetStartFrame,
                    targets[0].targetStartFrame, 0.5));

        // Unused, but it documents the grid spacing the test assumes.
        CHECK(step > 0.0);
    }

    // --- a note already close to the grid is left alone --------------------
    {
        TimingGrid grid;
        grid.framesPerSecond = kFps;
        grid.bpm = 120.0;
        grid.divisionsPerBeat = 4;

        const auto rms = syllable(400, 100, 10, 30);
        const int peak = findEnergyPeakFrame(rms, 100, 140);

        // Put the grid right on the peak, then allow a two-frame tolerance.
        grid.firstBeatFrame = static_cast<double>(peak) + 1.0;
        const auto targets = gridTimingTargets({sung(100, 140)}, rms, grid,
                                               TimingAnchor::EnergyPeak, 2.0);
        CHECK(targets.size() == 1);
        CHECK(!targets[0].hasTarget); // within tolerance: no correction
    }

    // --- guided targets line anchor against anchor -------------------------
    {
        // Sung syllable at 100..140, guide syllable at 160..200: both have
        // the same shape, so their peaks are the same distance in.
        const auto rms = syllable(400, 100, 10, 30);
        const auto guideRms = syllable(400, 160, 10, 30);

        const std::vector<SungNote> notes = {sung(100, 140)};
        const std::vector<GuideNote> guides = {guide(160, 200)};

        NoteMatch match;
        match.sungIndex = 0;
        match.guideIndex = 0;
        match.confidence = 0.9f;

        const auto targets = guidedTimingTargets(notes, guides, {match}, rms,
                                                 guideRms,
                                                 TimingAnchor::EnergyPeak);
        CHECK(targets.size() == 1);
        CHECK(targets[0].hasTarget);
        CHECK(near(targets[0].confidence, 0.9, 1e-6));

        // Identical envelopes 60 frames apart: the note should move 60.
        CHECK(near(targets[0].targetStartFrame, 160.0, 1.5));
        // And take the guide's length.
        CHECK(near(targets[0].targetEndFrame - targets[0].targetStartFrame,
                   40.0, 1e-6));
    }

    // --- a MIDI guide has no audio, so both sides use the start ------------
    {
        const auto rms = syllable(400, 100, 10, 30);
        const std::vector<SungNote> notes = {sung(100, 140)};
        const std::vector<GuideNote> guides = {guide(160, 200)};

        NoteMatch match;
        match.sungIndex = 0;
        match.guideIndex = 0;
        match.confidence = 1.0f;

        // Empty guide RMS: the guide is a MIDI file.
        const auto targets = guidedTimingTargets(notes, guides, {match}, rms, {},
                                                 TimingAnchor::EnergyPeak);
        CHECK(targets[0].hasTarget);
        // The sung peak goes to the guide's START, which is earlier than
        // matching peak to peak would give - stated here so the difference
        // is visible rather than surprising.
        const int peak = findEnergyPeakFrame(rms, 100, 140);
        CHECK(near(targets[0].targetStartFrame, 100.0 + (160.0 - peak), 1e-6));
    }

    // --- unmatched notes get no target -------------------------------------
    {
        const auto rms = syllable(400, 100, 10, 30);
        NoteMatch unmatched;
        unmatched.sungIndex = 0;
        unmatched.guideIndex = -1;

        const auto targets = guidedTimingTargets({sung(100, 140)}, {},
                                                 {unmatched}, rms, {});
        CHECK(targets.size() == 1);
        CHECK(!targets[0].hasTarget);
    }

    // --- empty inputs ------------------------------------------------------
    {
        CHECK(gridTimingTargets({}, {}, {}).empty());
        CHECK(guidedTimingTargets({}, {}, {}, {}, {}).empty());
    }

    // --- a fine grid still moves notes -------------------------------------
    //
    // The tolerance used to be a number of frames, set once and meant for a
    // coarse grid. At 200 BPM with a sixteenth-note grid the lines are 6.46
    // frames apart, so the furthest a note can sit from one is 3.23 frames,
    // and the old default of 3 frames meant almost nothing could qualify as
    // out of time. The card now hands down a share of that furthest distance,
    // which is the same proportion at any tempo.
    {
        TimingGrid grid;
        grid.framesPerSecond = kFps;
        grid.bpm = 200.0;
        grid.divisionsPerBeat = 4;
        grid.firstBeatFrame = 0.0;

        const double step = grid.framesPerDivision();
        CHECK(step > 6.4 && step < 6.5);          // the case from the report

        const double furthest = step / 2.0;       // 3.23 frames
        const double tolerance = 0.25 * furthest; // the new default, 25%

        // The vowel's centre lands around frame 9-10, about 2.5 to 2.9 frames
        // from the nearest line: well out of time, and well inside the old
        // 3-frame tolerance.
        const auto rms = syllable(40, 4, 3, 6);
        const auto note = sung(4, 13);

        const auto now = gridTimingTargets({note}, rms, grid,
                                           TimingAnchor::EnergyPeak, tolerance);
        CHECK(now.size() == 1);
        CHECK(now[0].hasTarget);

        const auto before = gridTimingTargets({note}, rms, grid,
                                              TimingAnchor::EnergyPeak, 3.0);
        CHECK(before.size() == 1);
        CHECK(!before[0].hasTarget);              // what the user saw
    }

    std::cout << "Timing anchor tests passed\n";
}
