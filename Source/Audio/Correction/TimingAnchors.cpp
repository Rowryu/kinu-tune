// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.

#include "TimingAnchors.h"

#include <algorithm>
#include <cmath>

namespace correction
{
namespace
{

/// Three-point smoothing, enough to stop a single noisy frame from being
/// mistaken for the note's peak without blurring the envelope's shape.
std::vector<float> smooth(const std::vector<float>& values, int from, int to)
{
    std::vector<float> out(static_cast<size_t>(std::max(0, to - from)), 0.0f);
    for (int i = from; i < to; ++i)
    {
        const int a = std::max(from, i - 1);
        const int b = std::min(to - 1, i + 1);
        float sum = 0.0f;
        int count = 0;
        for (int k = a; k <= b; ++k)
        {
            sum += values[static_cast<size_t>(k)];
            ++count;
        }
        out[static_cast<size_t>(i - from)] = count > 0 ? sum / count : 0.0f;
    }
    return out;
}

} // namespace

std::vector<float> framewiseRms(const std::vector<float>& samples, int hopSize)
{
    if (samples.empty() || hopSize <= 0)
        return {};

    const int frames = static_cast<int>(samples.size()) / hopSize;
    std::vector<float> rms(static_cast<size_t>(std::max(0, frames)), 0.0f);

    for (int f = 0; f < frames; ++f)
    {
        double sum = 0.0;
        const int base = f * hopSize;
        for (int i = 0; i < hopSize; ++i)
        {
            const double x = samples[static_cast<size_t>(base + i)];
            sum += x * x;
        }
        rms[static_cast<size_t>(f)] =
            static_cast<float>(std::sqrt(sum / static_cast<double>(hopSize)));
    }

    return rms;
}

int findEnergyPeakFrame(const std::vector<float>& rms,
                        int startFrame,
                        int endFrame)
{
    const int n = static_cast<int>(rms.size());
    const int from = std::clamp(startFrame, 0, n);
    const int to = std::clamp(endFrame, from, n);
    const int midpoint = from + (to - from) / 2;

    if (to - from < 2)
        return midpoint;

    const auto smoothed = smooth(rms, from, to);

    float peak = 0.0f;
    for (float v : smoothed)
        peak = std::max(peak, v);

    if (peak <= 0.0f)
        return midpoint; // silence: nothing to anchor on

    // A sustained vowel has a broad flat top, so take the centre of the
    // region near the maximum rather than the single highest frame, which
    // would sit wherever the noise happened to land.
    const float threshold = peak * 0.9f;
    long long sum = 0;
    int count = 0;
    for (int i = 0; i < static_cast<int>(smoothed.size()); ++i)
    {
        if (smoothed[static_cast<size_t>(i)] >= threshold)
        {
            sum += from + i;
            ++count;
        }
    }

    return count > 0 ? static_cast<int>(sum / count) : midpoint;
}

int findPitchSettleFrame(const std::vector<float>& midiPitch,
                         int startFrame,
                         int endFrame,
                         float noteMidi,
                         float toleranceSemitones)
{
    const int n = static_cast<int>(midiPitch.size());
    const int from = std::clamp(startFrame, 0, n);
    const int to = std::clamp(endFrame, from, n);
    if (to - from < 2)
        return -1;

    // The longest run, not the first: a scoop passes through the note on its
    // way up and would otherwise claim the note's position.
    int bestStart = -1;
    int bestLength = 0;
    int runStart = -1;

    for (int i = from; i <= to; ++i)
    {
        const bool settled =
            i < to && midiPitch[static_cast<size_t>(i)] > 0.0f &&
            std::abs(midiPitch[static_cast<size_t>(i)] - noteMidi) <=
                toleranceSemitones;

        if (settled)
        {
            if (runStart < 0)
                runStart = i;
            continue;
        }

        if (runStart >= 0)
        {
            const int length = i - runStart;
            if (length > bestLength)
            {
                bestLength = length;
                bestStart = runStart;
            }
            runStart = -1;
        }
    }

    if (bestLength <= 0)
        return -1;
    return bestStart + bestLength / 2;
}

int anchorFrameFor(const std::vector<float>& rms,
                   int startFrame,
                   int endFrame,
                   TimingAnchor anchor)
{
    return anchorFrameFor(rms, {}, startFrame, endFrame, 0.0f, anchor);
}

int anchorFrameFor(const std::vector<float>& rms,
                   const std::vector<float>& midiPitch,
                   int startFrame,
                   int endFrame,
                   float noteMidi,
                   TimingAnchor anchor)
{
    if (anchor == TimingAnchor::NoteStart)
        return startFrame;

    if (anchor == TimingAnchor::PitchCentre)
    {
        const int settled =
            findPitchSettleFrame(midiPitch, startFrame, endFrame, noteMidi);
        if (settled >= 0)
            return settled;
        // No settled pitch: an unvoiced note, or one the detector lost. The
        // energy peak is the only thing left to go on.
    }

    return findEnergyPeakFrame(rms, startFrame, endFrame);
}

double TimingGrid::framesPerDivision() const
{
    const int divisions = std::max(1, divisionsPerBeat);
    if (bpm <= 0.0 || framesPerSecond <= 0.0)
        return 0.0;
    const double secondsPerBeat = 60.0 / bpm;
    return secondsPerBeat * framesPerSecond / static_cast<double>(divisions);
}

double TimingGrid::snap(double frame) const
{
    const double step = framesPerDivision();
    if (step <= 0.0)
        return frame;
    return firstBeatFrame +
           std::round((frame - firstBeatFrame) / step) * step;
}

std::vector<TimingTarget> gridTimingTargets(
    const std::vector<SungNote>& notes,
    const std::vector<float>& rms,
    const TimingGrid& grid,
    TimingAnchor anchor,
    double toleranceFrames,
    const std::vector<float>& midiPitch)
{
    std::vector<TimingTarget> targets;
    targets.reserve(notes.size());

    const double step = grid.framesPerDivision();

    for (const auto& note : notes)
    {
        TimingTarget target;
        target.sourceStartFrame = note.startFrame;
        target.sourceEndFrame = note.endFrame;
        target.confidence = 1.0f;

        if (step <= 0.0 || note.endFrame <= note.startFrame)
        {
            targets.push_back(target);
            continue;
        }

        const int anchorFrame = anchorFrameFor(
            rms, midiPitch, note.startFrame, note.endFrame, note.midiNote,
            anchor);
        const double snapped = grid.snap(static_cast<double>(anchorFrame));
        const double shift = snapped - static_cast<double>(anchorFrame);

        // Already close enough. A performance is not supposed to be exactly
        // on the grid, and stretching audio to close a gap nobody can hear
        // only costs quality.
        if (std::abs(shift) <= toleranceFrames)
        {
            targets.push_back(target);
            continue;
        }

        target.hasTarget = true;
        target.targetStartFrame = static_cast<double>(note.startFrame) + shift;
        target.targetEndFrame = static_cast<double>(note.endFrame) + shift;
        targets.push_back(target);
    }

    return targets;
}

std::vector<TimingTarget> guidedTimingTargets(
    const std::vector<SungNote>& notes,
    const std::vector<GuideNote>& guide,
    const std::vector<NoteMatch>& matches,
    const std::vector<float>& rms,
    const std::vector<float>& guideRms,
    TimingAnchor anchor,
    const std::vector<float>& midiPitch,
    const std::vector<float>& guideMidiPitch)
{
    std::vector<TimingTarget> targets;
    targets.reserve(notes.size());

    for (size_t i = 0; i < notes.size(); ++i)
    {
        const auto& note = notes[i];

        TimingTarget target;
        target.sourceStartFrame = note.startFrame;
        target.sourceEndFrame = note.endFrame;

        if (i >= matches.size() || !matches[i].isMatched() ||
            matches[i].guideIndex >= static_cast<int>(guide.size()) ||
            note.endFrame <= note.startFrame)
        {
            targets.push_back(target);
            continue;
        }

        const auto& guideNote = guide[static_cast<size_t>(matches[i].guideIndex)];

        const int sungAnchor =
            anchorFrameFor(rms, midiPitch, note.startFrame, note.endFrame,
                           note.midiNote, anchor);

        // A guide with no audio behind it (a MIDI file) can only be anchored
        // at its start. Using a peak on one side and a start on the other
        // would build the consonant delay into every correction.
        const int guideAnchor =
            guideRms.empty() && guideMidiPitch.empty()
                ? guideNote.startFrame
                : anchorFrameFor(guideRms, guideMidiPitch,
                                 guideNote.startFrame, guideNote.endFrame,
                                 guideNote.midiNote, anchor);

        const double shift =
            static_cast<double>(guideAnchor) - static_cast<double>(sungAnchor);

        target.hasTarget = true;
        target.confidence = matches[i].confidence;
        target.targetStartFrame = static_cast<double>(note.startFrame) + shift;
        // Follow the guide's length as well, so a note held too short is
        // lengthened rather than only moved.
        target.targetEndFrame =
            target.targetStartFrame +
            static_cast<double>(guideNote.endFrame - guideNote.startFrame);

        targets.push_back(target);
    }

    return targets;
}

} // namespace correction
