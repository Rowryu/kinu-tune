// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.

#include "GuidedPitchCorrection.h"

#include "../../Utils/Constants.h"
#include "../../Utils/NotePitch.h"
#include "../../Utils/PitchCurveProcessor.h"

#include <algorithm>
#include <cmath>

namespace correction
{
namespace
{

/// The pitch a correction should be measured from: the note as sung.
///
/// Correct Pitch keeps this separate from the current pitch so that
/// correcting twice does not compound, and so the inspector slider can show
/// the share of the sung deviation that has been removed.
///
/// PitchToolController additionally asks ScaleUtils::correctionShare whether
/// the current pitch is a 0-100% correction of the reference, and starts over
/// from the current pitch when it is not. That check does NOT transfer here,
/// and using it was a bug: correctionShare measures against the nearest note
/// of the EDITOR SCALE, while guided correction aims at the guide note
/// instead. For a note sung close to a scale pitch the scale deviation is
/// about zero, so the check reported "not a valid correction" for a pitch
/// this function had itself just corrected, discarded the sung reference, and
/// made a second run correct the already-corrected pitch again.
///
/// No check is needed. Note's constructor sets the reference to the sung
/// pitch, Note::setMidiNote keeps the two together for edits and pitch
/// drawing, and NotePitch::applyEdit carries the reference through a move.
/// Only setMidiNoteFromPitchCorrection moves the pitch without the reference -
/// which is exactly the correction this wants to measure from.
float correctionSourceFor(const Note& note)
{
    return note.getLastNonMacroMidiNote();
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
        s.midiNote = correctionSourceFor(note);

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
