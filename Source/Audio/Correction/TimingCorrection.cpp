// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.

#include "TimingCorrection.h"

#include "../../Utils/Constants.h"
#include "PitchTargeting.h"

#include <algorithm>
#include <cmath>

namespace correction
{
namespace
{

/// Loudness per analysis frame of the take, for finding note anchors.
///
/// Read from originalWaveform when it is there: that is the pristine
/// recording, and anchoring on already-resynthesised audio would let one
/// correction's output shift the next one's idea of where a note sits.
std::vector<float> projectRms(const Project& project)
{
    const auto& audio = project.getAudioData();
    const auto& buffer = audio.originalWaveform.getNumSamples() > 0
                             ? audio.originalWaveform
                             : audio.waveform;

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0 || buffer.getNumChannels() <= 0)
        return {};

    std::vector<float> mono(static_cast<size_t>(numSamples), 0.0f);
    const int channels = buffer.getNumChannels();
    for (int channel = 0; channel < channels; ++channel)
    {
        const float* data = buffer.getReadPointer(channel);
        for (int i = 0; i < numSamples; ++i)
            mono[static_cast<size_t>(i)] += data[i];
    }
    if (channels > 1)
        for (auto& x : mono)
            x /= static_cast<float>(channels);

    return framewiseRms(mono, HOP_SIZE);
}

} // namespace

TimingReport planTimingCorrection(const Project& project,
                                  const std::vector<GuideNote>& guide,
                                  const TimingCorrectionParams& params,
                                  std::vector<TimingEdit>& edits)
{
    TimingReport report;

    // Rests carry no pitch to match on and no onset to place. They are the
    // gaps between notes, and the gaps follow whatever the notes do.
    std::vector<int> noteIndices;
    std::vector<SungNote> sung;
    const auto& notes = project.getNotes();
    for (int i = 0; i < static_cast<int>(notes.size()); ++i)
    {
        const Note& note = notes[static_cast<size_t>(i)];
        if (note.isRest())
            continue;

        SungNote s;
        // Timing works on where the note SOUNDS, which is its destination
        // range: a note already moved by hand should be corrected from where
        // it now sits, not from where it was recorded.
        s.startFrame = note.getStartFrame();
        s.endFrame = note.getEndFrame();
        s.midiNote = note.getLastNonMacroMidiNote();

        sung.push_back(s);
        noteIndices.push_back(i);
    }

    report.notesConsidered = static_cast<int>(sung.size());
    if (sung.empty())
        return report;

    const auto rms = projectRms(project);

    std::vector<TimingTarget> targets;
    if (params.mode == TimingMode::Guide)
    {
        if (guide.empty())
            return report;

        const auto matches = alignNotesToGuide(sung, guide, params.alignment);
        for (const auto& match : matches)
            if (match.isMatched())
                ++report.notesMatched;

        // A MIDI guide has no audio, so guidedTimingTargets anchors both
        // sides on note starts; a track guide would pass its own RMS here.
        targets = guidedTimingTargets(sung, guide, matches, rms, {},
                                      params.anchor);
    }
    else
    {
        targets = gridTimingTargets(sung, rms, params.grid, params.anchor,
                                    params.gridToleranceFrames);
    }

    const int frameCount = project.getAudioData().getNumFrames();
    const auto map = buildTimingMap(targets, std::max(1, frameCount),
                                    params.timing);

    edits.clear();
    edits.reserve(sung.size());

    for (size_t i = 0; i < sung.size() && i < map.noteStart.size(); ++i)
    {
        const int newStart = static_cast<int>(std::lround(map.noteStart[i]));
        int newEnd = static_cast<int>(std::lround(map.noteEnd[i]));
        if (newEnd <= newStart)
            newEnd = newStart + 1;

        if (newStart == sung[i].startFrame && newEnd == sung[i].endFrame)
            continue;

        TimingEdit edit;
        edit.noteIndex = noteIndices[i];
        edit.previousStartFrame = sung[i].startFrame;
        edit.previousEndFrame = sung[i].endFrame;
        edit.newStartFrame = newStart;
        edit.newEndFrame = newEnd;
        edits.push_back(edit);

        ++report.notesMoved;
        report.largestShiftFrames =
            std::max(report.largestShiftFrames,
                     std::abs(static_cast<double>(newStart) -
                              static_cast<double>(sung[i].startFrame)));
    }

    return report;
}

} // namespace correction
