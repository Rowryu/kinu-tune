// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.

#include "GuidedPitchCorrection.h"

#include "../../Utils/Constants.h"
#include "../../Utils/NotePitch.h"
#include "../../Utils/PitchCurveProcessor.h"
#include "../../Utils/ScaleUtils.h"

#include <algorithm>
#include <cmath>

namespace correction
{
namespace
{

/// The pitch a correction should be measured from.
///
/// Correct Pitch keeps a separate reference - the pitch as sung - so that
/// correcting twice does not compound, and so the inspector slider can show
/// the share of the sung deviation that has been removed. A note whose
/// current pitch is not a 0-100% correction of its stored reference (an older
/// project, or a pitch that was drawn in) starts over from where it is, which
/// is what the editor's own Correct Pitch does on the first drag.
float correctionSourceFor(const Project& project, const Note& note)
{
    const int referenceHz = project.getPitchReferenceHz();
    float source = note.getLastNonMacroMidiNote();

    if (!ScaleUtils::correctionShare(project, source, note.getMidiNote(),
                                     referenceHz))
        source = note.getMidiNote();

    return source;
}

} // namespace

GuidedPitchReport applyGuidedPitchCorrection(
    Project& project,
    const std::vector<GuideNote>& guide,
    const PitchTargetParams& params,
    const AlignmentParams& alignment,
    std::vector<GuidedPitchEdit>* edits)
{
    GuidedPitchReport report;

    auto& notes = project.getNotes();

    // Rests carry no pitch, so they are not offered to the matcher at all; an
    // unmatched rest would otherwise sit between two sung notes and make the
    // monotonic path pay a skip for nothing.
    std::vector<int> noteIndices;
    std::vector<SungNote> sung;
    for (int i = 0; i < static_cast<int>(notes.size()); ++i)
    {
        const Note& note = notes[static_cast<size_t>(i)];
        if (note.isRest())
            continue;

        SungNote s;
        s.startFrame = note.getSrcStartFrame();
        s.endFrame = note.getSrcEndFrame();
        // Match on the sung pitch, not the current one: after one run the
        // current pitch already sits near the guide, and matching on it would
        // make a wrong pairing look right the second time round.
        s.midiNote = correctionSourceFor(project, note);

        sung.push_back(s);
        noteIndices.push_back(i);
    }

    report.notesConsidered = static_cast<int>(sung.size());
    if (sung.empty() || guide.empty())
        return report;

    const auto matches = alignNotesToGuide(sung, guide, alignment);
    const auto targets = computePitchTargets(sung, guide, matches, params);

    for (size_t i = 0; i < targets.size(); ++i)
    {
        if (matches[i].isMatched())
            ++report.notesMatched;

        if (!targets[i].corrected)
            continue;

        Note& note = notes[static_cast<size_t>(noteIndices[i])];

        GuidedPitchEdit edit;
        edit.noteIndex = noteIndices[i];
        edit.previousMidi = note.getMidiNote();
        edit.previousCorrectionSource = note.getLastNonMacroMidiNote();

        // The reference is written first and then left alone, exactly as the
        // editor's Correct Pitch does, so a later manual correction on the
        // same note starts from the sung pitch rather than from this result.
        NotePitch::setCorrectionSource(note, sung[i].midiNote);
        NotePitch::applyCorrection(note, targets[i].correctedMidiNote);

        edit.newMidi = note.getMidiNote();
        edit.newCorrectionSource = note.getLastNonMacroMidiNote();
        if (edits != nullptr)
            edits->push_back(edit);

        ++report.notesCorrected;
        report.largestShiftSemitones =
            std::max(report.largestShiftSemitones,
                     std::abs(targets[i].appliedShiftSemitones));
    }

    if (report.notesCorrected > 0)
        PitchCurveProcessor::rebuildBaseFromNotes(project);

    return report;
}

void revertGuidedPitchCorrection(Project& project,
                                 const std::vector<GuidedPitchEdit>& edits)
{
    auto& notes = project.getNotes();
    for (const auto& edit : edits)
    {
        if (edit.noteIndex < 0 || edit.noteIndex >= static_cast<int>(notes.size()))
            continue;
        NotePitch::restore(notes[static_cast<size_t>(edit.noteIndex)],
                           edit.previousMidi, edit.previousCorrectionSource);
    }

    if (!edits.empty())
        PitchCurveProcessor::rebuildBaseFromNotes(project);
}

void reapplyGuidedPitchCorrection(Project& project,
                                  const std::vector<GuidedPitchEdit>& edits)
{
    auto& notes = project.getNotes();
    for (const auto& edit : edits)
    {
        if (edit.noteIndex < 0 || edit.noteIndex >= static_cast<int>(notes.size()))
            continue;
        NotePitch::restore(notes[static_cast<size_t>(edit.noteIndex)],
                           edit.newMidi, edit.newCorrectionSource);
    }

    if (!edits.empty())
        PitchCurveProcessor::rebuildBaseFromNotes(project);
}

} // namespace correction
