// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.

// Guided pitch correction against a real Project: that the matcher's answer
// reaches Note::midiNote, that the correction reference behaves the way the
// editor's own Correct Pitch does, and that a run can be undone exactly.

#include "../Source/Audio/Correction/GuidedPitchCorrection.h"
#include "TestAssert.h"

#include <cmath>
#include <iostream>

using namespace correction;

namespace
{

GuideNote guide(int start, int end, float midi)
{
    GuideNote g;
    g.startFrame = start;
    g.endFrame = end;
    g.midiNote = midi;
    return g;
}

bool near(float a, float b, float tolerance = 0.02f)
{
    return std::abs(a - b) <= tolerance;
}

} // namespace

int main()
{
    // --- the correction reaches the notes --------------------------------
    {
        Project project;
        project.addNote(Note(0, 100, 59.7f));   // a shade flat of C4
        project.addNote(Note(100, 200, 62.3f)); // a shade sharp of D4

        const std::vector<GuideNote> g = {guide(0, 100, 60.0f),
                                          guide(100, 200, 62.0f)};

        PitchTargetParams params;
        params.correctionStrength = 1.0f;

        const auto report = applyGuidedPitchCorrection(project, g, params);

        CHECK(report.notesConsidered == 2);
        CHECK(report.notesMatched == 2);
        CHECK(report.notesCorrected == 2);
        CHECK(near(project.getNotes()[0].getMidiNote(), 60.0f, 0.1f));
        CHECK(near(project.getNotes()[1].getMidiNote(), 62.0f, 0.1f));
    }

    // --- the reference stays, so a second run does not compound ----------
    {
        // 59.0 is deliberately a whole semitone - a note of the default
        // editor scale. An earlier version borrowed
        // ScaleUtils::correctionShare to decide whether the stored reference
        // was still valid, but that measures deviation from the nearest SCALE
        // pitch, which is zero here. It therefore called a pitch it had just
        // corrected invalid, threw the sung reference away, and corrected the
        // corrected pitch again on the next run. A sung pitch off the scale
        // would have hidden the bug.
        Project project;
        project.addNote(Note(0, 100, 59.0f));
        const std::vector<GuideNote> g = {guide(0, 100, 60.0f)};

        PitchTargetParams params;
        params.correctionStrength = 0.5f;

        applyGuidedPitchCorrection(project, g, params);
        const float afterFirst = project.getNotes()[0].getMidiNote();

        // Half the deviation of a semitone: roughly a quarter tone up.
        CHECK(afterFirst > 59.0f && afterFirst < 60.0f);
        // The reference is the pitch as sung, not the corrected one.
        CHECK(near(project.getNotes()[0].getLastNonMacroMidiNote(), 59.0f));

        applyGuidedPitchCorrection(project, g, params);
        const float afterSecond = project.getNotes()[0].getMidiNote();

        // Running it again at the same strength is idempotent: it corrects
        // the same share of the same sung deviation, rather than half of
        // what is left each time.
        CHECK(near(afterSecond, afterFirst));
    }

    // --- strength zero changes nothing -----------------------------------
    {
        Project project;
        project.addNote(Note(0, 100, 59.0f));
        const std::vector<GuideNote> g = {guide(0, 100, 60.0f)};

        PitchTargetParams params;
        params.correctionStrength = 0.0f;

        const auto report = applyGuidedPitchCorrection(project, g, params);
        CHECK(report.notesCorrected == 0);
        CHECK(near(project.getNotes()[0].getMidiNote(), 59.0f, 1e-4f));
    }

    // --- an empty guide is a no-op, not a crash --------------------------
    {
        Project project;
        project.addNote(Note(0, 100, 59.0f));

        const auto report = applyGuidedPitchCorrection(project, {}, {});
        CHECK(report.notesConsidered == 1);
        CHECK(report.notesCorrected == 0);
        CHECK(near(project.getNotes()[0].getMidiNote(), 59.0f, 1e-4f));

        Project empty;
        const auto none = applyGuidedPitchCorrection(empty, {guide(0, 10, 60.0f)}, {});
        CHECK(none.notesConsidered == 0);
        CHECK(none.notesCorrected == 0);
    }

    // --- rests are not offered to the matcher ----------------------------
    {
        Project project;
        project.addNote(Note(0, 100, 60.0f));
        project.addNote(Note(100, 150, 0.0f));
        project.getNotes()[1].setRest(true);
        project.addNote(Note(150, 250, 61.6f));

        const std::vector<GuideNote> g = {guide(0, 100, 60.0f),
                                          guide(150, 250, 62.0f)};

        PitchTargetParams params;
        params.correctionStrength = 1.0f;

        const auto report = applyGuidedPitchCorrection(project, g, params);

        CHECK(report.notesConsidered == 2);
        CHECK(report.notesMatched == 2);
        // The rest keeps whatever it had; only the sung notes move.
        CHECK(near(project.getNotes()[2].getMidiNote(), 62.0f, 0.1f));
    }

    // --- a wrong note is left alone, not dragged -------------------------
    {
        Project project;
        project.addNote(Note(0, 100, 55.0f)); // a fourth below the guide
        const std::vector<GuideNote> g = {guide(0, 100, 60.0f)};

        PitchTargetParams params;
        params.correctionStrength = 1.0f;
        params.maxShiftSemitones = 2.0f;
        params.minConfidence = 0.0f;

        applyGuidedPitchCorrection(project, g, params);

        const float moved = project.getNotes()[0].getMidiNote() - 55.0f;
        CHECK(std::abs(moved) < 1e-4f);
    }

    // --- undo and redo restore exactly -----------------------------------
    {
        Project project;
        project.addNote(Note(0, 100, 59.7f));
        project.addNote(Note(100, 200, 62.3f));
        project.addNote(Note(200, 300, 64.1f));

        const float before0 = project.getNotes()[0].getMidiNote();
        const float before1 = project.getNotes()[1].getMidiNote();
        const float before2 = project.getNotes()[2].getMidiNote();

        const std::vector<GuideNote> g = {guide(0, 100, 60.0f),
                                          guide(100, 200, 62.0f),
                                          guide(200, 300, 64.0f)};

        PitchTargetParams params;
        params.correctionStrength = 1.0f;

        std::vector<GuidedPitchEdit> edits;
        const auto report =
            applyGuidedPitchCorrection(project, g, params, {}, &edits);

        CHECK(report.notesCorrected == 3);
        CHECK(static_cast<int>(edits.size()) == report.notesCorrected);

        const float corrected0 = project.getNotes()[0].getMidiNote();

        revertGuidedPitchCorrection(project, edits);
        CHECK(near(project.getNotes()[0].getMidiNote(), before0, 1e-5f));
        CHECK(near(project.getNotes()[1].getMidiNote(), before1, 1e-5f));
        CHECK(near(project.getNotes()[2].getMidiNote(), before2, 1e-5f));
        // The reference has to come back too, or the inspector's Correct
        // Pitch slider reads a different value after an undo.
        CHECK(near(project.getNotes()[0].getLastNonMacroMidiNote(), before0, 1e-5f));

        reapplyGuidedPitchCorrection(project, edits);
        CHECK(near(project.getNotes()[0].getMidiNote(), corrected0, 1e-5f));
    }

    std::cout << "Guided pitch correction tests passed\n";
}
