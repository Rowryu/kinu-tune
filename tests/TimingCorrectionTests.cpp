// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.

// Planning timing correction against a real Project: that the plan touches
// the destination range and not the source, that rests are left to follow,
// and that nothing is written until the caller applies it.

#include "../Source/Audio/Correction/TimingCorrection.h"
#include "../Source/Utils/Constants.h"
#include "TestAssert.h"

#include <cmath>
#include <iostream>

using namespace correction;

namespace
{

constexpr double kFps =
    static_cast<double>(SAMPLE_RATE) / static_cast<double>(HOP_SIZE);

GuideNote guide(int start, int end, float midi)
{
    GuideNote g;
    g.startFrame = start;
    g.endFrame = end;
    g.midiNote = midi;
    return g;
}

/// A take with one loud block per note, so every note has a findable peak.
void fillAudio(Project& project, int frames,
               const std::vector<std::pair<int, int>>& loudSpans)
{
    auto& audio = project.getAudioData();
    audio.sampleRate = SAMPLE_RATE;
    audio.originalWaveform.setSize(1, frames * HOP_SIZE);
    audio.originalWaveform.clear();

    auto* data = audio.originalWaveform.getWritePointer(0);
    for (const auto& span : loudSpans)
        for (int f = span.first; f < span.second; ++f)
            for (int i = 0; i < HOP_SIZE; ++i)
                data[f * HOP_SIZE + i] = 0.6f;

    // getNumFrames() reads the mel spectrogram, which stands in for the
    // analysis length here.
    audio.melSpectrogram.assign(static_cast<size_t>(frames),
                                std::vector<float>(1, 0.0f));
}

} // namespace

int main()
{
    // --- a late note is pulled towards its guide --------------------------
    {
        Project project;
        project.addNote(Note(100, 160, 60));
        fillAudio(project, 600, {{110, 160}});

        // The guide wants it 40 frames earlier.
        const std::vector<GuideNote> g = {guide(60, 120, 60.0f)};

        TimingCorrectionParams params;
        params.mode = TimingMode::Guide;
        params.timing.framesPerSecond = kFps;
        params.timing.correctionStrength = 1.0f;
        params.timing.maxShiftSeconds = 2.0f;

        std::vector<TimingEdit> edits;
        const auto report = planTimingCorrection(project, g, params, edits);

        CHECK(report.notesConsidered == 1);
        CHECK(report.notesMatched == 1);
        CHECK(report.notesMoved == 1);
        CHECK(edits.size() == 1);
        CHECK(edits[0].newStartFrame < edits[0].previousStartFrame);

        // Planning writes nothing: the project is untouched until the caller
        // applies the edits through the editor's own timing path.
        CHECK(project.getNotes()[0].getStartFrame() == 100);
        CHECK(project.getNotes()[0].getEndFrame() == 160);
        // And the source range is never what moves.
        CHECK(project.getNotes()[0].getSrcStartFrame() == 100);
    }

    // --- zero strength plans nothing ---------------------------------------
    {
        Project project;
        project.addNote(Note(100, 160, 60));
        fillAudio(project, 600, {{110, 160}});
        const std::vector<GuideNote> g = {guide(60, 120, 60.0f)};

        TimingCorrectionParams params;
        params.timing.framesPerSecond = kFps;
        params.timing.correctionStrength = 0.0f;

        std::vector<TimingEdit> edits;
        const auto report = planTimingCorrection(project, g, params, edits);
        CHECK(report.notesMoved == 0);
        CHECK(edits.empty());
    }

    // --- no guide in guide mode is a no-op ---------------------------------
    {
        Project project;
        project.addNote(Note(100, 160, 60));
        fillAudio(project, 600, {{110, 160}});

        TimingCorrectionParams params;
        params.mode = TimingMode::Guide;

        std::vector<TimingEdit> edits;
        const auto report = planTimingCorrection(project, {}, params, edits);
        CHECK(report.notesConsidered == 1);
        CHECK(report.notesMoved == 0);
        CHECK(edits.empty());
    }

    // --- grid mode needs no guide ------------------------------------------
    {
        Project project;
        project.addNote(Note(100, 160, 60));
        fillAudio(project, 600, {{110, 160}});

        TimingCorrectionParams params;
        params.mode = TimingMode::Grid;
        params.timing.framesPerSecond = kFps;
        params.timing.correctionStrength = 1.0f;
        params.timing.maxShiftSeconds = 2.0f;
        params.grid.framesPerSecond = kFps;
        params.grid.bpm = 120.0;
        params.grid.divisionsPerBeat = 4;
        params.gridToleranceFrames = 0.0;

        std::vector<TimingEdit> edits;
        const auto report = planTimingCorrection(project, {}, params, edits);
        CHECK(report.notesConsidered == 1);
        // Whether it moves depends on where the peak falls, but it must not
        // crash and must not claim matches in a mode that has no guide.
        CHECK(report.notesMatched == 0);
    }

    // --- rests are not planned for --------------------------------------
    {
        Project project;
        project.addNote(Note(100, 160, 60));
        project.addNote(Note(160, 200, 0));
        project.getNotes()[1].setRest(true);
        project.addNote(Note(200, 260, 62));
        fillAudio(project, 600, {{110, 160}, {210, 260}});

        const std::vector<GuideNote> g = {guide(60, 120, 60.0f),
                                          guide(160, 220, 62.0f)};

        TimingCorrectionParams params;
        params.timing.framesPerSecond = kFps;
        params.timing.correctionStrength = 1.0f;
        params.timing.maxShiftSeconds = 2.0f;

        std::vector<TimingEdit> edits;
        const auto report = planTimingCorrection(project, g, params, edits);

        CHECK(report.notesConsidered == 2); // the rest is not one of them
        for (const auto& edit : edits)
            CHECK(edit.noteIndex != 1); // and is never planned for
    }

    // --- note order survives ------------------------------------------------
    {
        Project project;
        for (int i = 0; i < 6; ++i)
            project.addNote(Note(100 + i * 50, 140 + i * 50, 60 + i));

        std::vector<std::pair<int, int>> spans;
        for (int i = 0; i < 6; ++i)
            spans.push_back({110 + i * 50, 140 + i * 50});
        fillAudio(project, 800, spans);

        // Every guide note asks the take to move earlier by a lot.
        std::vector<GuideNote> g;
        for (int i = 0; i < 6; ++i)
            g.push_back(guide(20 + i * 50, 60 + i * 50,
                              static_cast<float>(60 + i)));

        TimingCorrectionParams params;
        params.timing.framesPerSecond = kFps;
        params.timing.correctionStrength = 1.0f;
        params.timing.maxShiftSeconds = 5.0f;

        std::vector<TimingEdit> edits;
        planTimingCorrection(project, g, params, edits);

        for (size_t i = 1; i < edits.size(); ++i)
            CHECK(edits[i].newStartFrame > edits[i - 1].newStartFrame);
    }

    // --- an empty project is survivable -------------------------------------
    {
        Project project;
        TimingCorrectionParams params;
        std::vector<TimingEdit> edits;
        const auto report = planTimingCorrection(project, {}, params, edits);
        CHECK(report.notesConsidered == 0);
        CHECK(edits.empty());
    }

    // --- a project with notes but no audio must not read out of bounds -----
    {
        Project project;
        project.addNote(Note(100, 160, 60));
        const std::vector<GuideNote> g = {guide(60, 120, 60.0f)};

        TimingCorrectionParams params;
        params.timing.framesPerSecond = kFps;
        params.timing.correctionStrength = 1.0f;
        params.timing.maxShiftSeconds = 2.0f;

        std::vector<TimingEdit> edits;
        const auto report = planTimingCorrection(project, g, params, edits);
        CHECK(report.notesConsidered == 1); // no crash; anchors fall back
    }

    std::cout << "Timing correction tests passed\n";
}
