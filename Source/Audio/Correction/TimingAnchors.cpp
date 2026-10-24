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

double TimingGrid::quartersAtFrame(double frame) const
{
    return tempoMap.quartersAt(hostOffsetSeconds + frame / framesPerSecond);
}

double TimingGrid::frameAtQuarters(double quarters) const
{
    return (tempoMap.secondsAt(quarters) - hostOffsetSeconds) * framesPerSecond;
}

double TimingGrid::framesPerDivisionAt(double frame) const
{
    const int divisions = std::max(1, divisionsPerBeat);
    if (framesPerSecond <= 0.0)
        return 0.0;

    if (!tempoMap.isEmpty())
    {
        // Under a tempo track the spacing is a local quantity: one division
        // at 100 BPM is twice one at 200. Measure it where it is being asked
        // about rather than assuming the song holds one tempo.
        const double step = 1.0 / static_cast<double>(divisions);
        const double here = quartersAtFrame(frame);
        const double spacing = frameAtQuarters(here + step) - frameAtQuarters(here);
        if (spacing > 0.0)
            return spacing;
        // A degenerate map; fall through to the single tempo below.
    }

    if (bpm <= 0.0)
        return 0.0;
    const double secondsPerBeat = 60.0 / bpm;
    return secondsPerBeat * framesPerSecond / static_cast<double>(divisions);
}

double TimingGrid::snap(double frame) const
{
    if (!tempoMap.isEmpty() && framesPerSecond > 0.0)
    {
        const int divisions = std::max(1, divisionsPerBeat);
        const double step = 1.0 / static_cast<double>(divisions);
        const double here = quartersAtFrame(frame);
        return frameAtQuarters(std::round(here / step) * step);
    }

    const double step = framesPerDivisionAt(frame);
    if (step <= 0.0)
        return frame;
    return firstBeatFrame +
           std::round((frame - firstBeatFrame) / step) * step;
}

std::vector<int> findSyllableOnsets(const std::vector<float>& rms,
                                    const std::vector<float>& midiPitch,
                                    int startFrame,
                                    int endFrame,
                                    int minSegmentFrames,
                                    float dipRatio)
{
    std::vector<int> onsets;
    const int first = startFrame + minSegmentFrames;
    const int last = endFrame - minSegmentFrames;
    if (last <= first)
        return onsets;

    const auto voiced = [&](int f) {
        return f >= 0 && f < static_cast<int>(midiPitch.size()) &&
               midiPitch[static_cast<size_t>(f)] > 0.0f;
    };
    const auto level = [&](int f) {
        return f >= 0 && f < static_cast<int>(rms.size())
                   ? rms[static_cast<size_t>(f)] : 0.0f;
    };
    const auto peakIn = [&](int a, int b) {
        float peak = 0.0f;
        for (int f = std::max(a, startFrame); f < std::min(b, endFrame); ++f)
            peak = std::max(peak, level(f));
        return peak;
    };

    std::vector<int> candidates;

    // A consonant between vowels: voiced, then at least two unvoiced frames,
    // then voiced again. The boundary is where the voicing stopped.
    if (!midiPitch.empty())
    {
        for (int f = first; f < last; ++f)
        {
            if (!voiced(f - 1) || voiced(f))
                continue;
            int g = f;
            while (g < endFrame && !voiced(g))
                ++g;
            if (g - f >= 2 && g < endFrame)
                candidates.push_back(f);
            f = g;
        }
    }

    // An energy dip between two louder stretches.
    if (!rms.empty())
    {
        const int side = std::max(3, minSegmentFrames / 2);
        for (int f = first; f < last; ++f)
        {
            const float here = level(f);
            if (here > level(f - 1) || here > level(f + 1))
                continue; // not a local minimum
            const float before = peakIn(f - side, f);
            const float after = peakIn(f + 1, f + 1 + side);
            if (before <= 0.0f || after <= 0.0f)
                continue;
            if (here <= dipRatio * std::min(before, after))
                candidates.push_back(f);
        }
    }

    std::sort(candidates.begin(), candidates.end());
    int previous = startFrame;
    for (const int c : candidates)
    {
        if (c - previous < minSegmentFrames || endFrame - c < minSegmentFrames)
            continue;
        onsets.push_back(c);
        previous = c;
    }
    return onsets;
}

int rigidHeadFrames(const std::vector<float>& midiPitch,
                    int startFrame,
                    int endFrame,
                    int attackFrames,
                    double maxShare)
{
    const int length = endFrame - startFrame;
    if (length <= 2)
        return 0;
    int firstVoiced = startFrame;
    if (!midiPitch.empty())
    {
        firstVoiced = endFrame;
        for (int f = std::max(0, startFrame);
             f < endFrame && f < static_cast<int>(midiPitch.size()); ++f)
        {
            if (midiPitch[static_cast<size_t>(f)] > 0.0f)
            {
                firstVoiced = f;
                break;
            }
        }
        if (firstVoiced >= endFrame)
            firstVoiced = startFrame; // never voiced: protect just the attack
    }
    const int head = (firstVoiced - startFrame) + std::max(0, attackFrames);
    const int cap = static_cast<int>(std::floor(length * std::clamp(maxShare, 0.0, 1.0)));
    return std::clamp(head, 0, cap);
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
        target.rigidHeadFrames =
            rigidHeadFrames(midiPitch, note.startFrame, note.endFrame);

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
        target.rigidHeadFrames =
            rigidHeadFrames(midiPitch, note.startFrame, note.endFrame);

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
        // The guide note's own anchor first: a guide read from a track
        // carries one, worked out from that track's audio when the note was
        // built. Without it, find one here if there is audio to find it in,
        // and fall back to the start only for a guide that never had any.
        const int guideAnchor =
            guideNote.anchorFrame >= 0
                ? guideNote.anchorFrame
                : (guideRms.empty() && guideMidiPitch.empty()
                       ? guideNote.startFrame
                       : anchorFrameFor(guideRms, guideMidiPitch,
                                        guideNote.startFrame,
                                        guideNote.endFrame,
                                        guideNote.midiNote, anchor));

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
