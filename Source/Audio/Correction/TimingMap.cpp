// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.

#include "TimingMap.h"

#include <algorithm>
#include <cmath>

namespace correction
{
namespace
{

struct Anchor
{
    double source = 0.0;
    double output = 0.0;
    /// True for a note's start. Where two anchors land on the same source
    /// frame the onset is the one that survives; see collapseSharedFrames.
    bool isOnset = false;
    /// Keeps exactly the shift of the anchor before it, whatever the rate
    /// bounds did to that one: the span between them is copied at rate 1.
    bool rigidWithPrevious = false;
};

/// How far a note may move, after strength, confidence and the ceiling.
double allowedShift(double wanted, const TimingParams& params, float confidence)
{
    const double strength =
        static_cast<double>(std::clamp(params.correctionStrength, 0.0f, 1.0f));
    const double trust =
        static_cast<double>(std::clamp(confidence, 0.0f, 1.0f));
    const double ceilingFrames =
        std::max(0.0, static_cast<double>(params.maxShiftSeconds)) *
        (params.framesPerSecond > 0.0 ? params.framesPerSecond : 1.0);

    return std::clamp(wanted * strength * trust, -ceilingFrames, ceilingFrames);
}

/// Fold anchors that share a source frame into one.
///
/// The note list is a partition of the take, so all through a legato phrase
/// the end of one note IS the start of the next: one frame carrying two
/// wishes. When the two notes move in opposite directions - routine under a
/// grid, where neighbours round to different lines - those wishes disagree,
/// and something has to give.
///
/// The onset wins. It is the audible event; a note's tail in legato is not
/// heard as a moment at all, it is heard as the approach to the next onset.
/// Letting the tail absorb the difference puts the stretching where nobody
/// listens. The alternative - keeping both anchors a hair apart - is what
/// this code used to do, and it silently threw the second note's onset away
/// and squashed the note instead, which is audible on every legato run.
void collapseSharedFrames(std::vector<Anchor>& anchors)
{
    if (anchors.size() < 2)
        return;

    std::vector<Anchor> kept;
    kept.reserve(anchors.size());

    for (size_t i = 0; i < anchors.size();)
    {
        size_t j = i + 1;
        while (j < anchors.size() && anchors[j].source == anchors[i].source)
            ++j;

        size_t chosen = i;
        for (size_t k = i; k < j; ++k)
        {
            if (anchors[k].isOnset)
            {
                chosen = k;
                break;
            }
        }

        kept.push_back(anchors[chosen]);
        i = j;
    }

    anchors.swap(kept);
}

/// Force the anchors to describe a map that only moves forwards and never
/// stretches or compresses harder than the rate bounds allow.
///
/// A single forward pass is enough for both: each anchor's output position is
/// clamped into the window its predecessor leaves open. An anchor that wanted
/// to go further than the window simply stops at its edge, which is the
/// behaviour worth having - the budget is spent on the notes that come first
/// rather than on whichever note happened to ask for the most.
void enforceRateBounds(std::vector<Anchor>& anchors, const TimingParams& params)
{
    const double minRate = std::max(1e-3, params.minRate);
    const double maxRate = std::max(minRate, params.maxRate);

    for (size_t i = 1; i < anchors.size(); ++i)
    {
        const double sourceStep = anchors[i].source - anchors[i - 1].source;
        if (sourceStep <= 0.0)
        {
            // Degenerate or inverted source spacing: keep the map moving by a
            // hair so interpolation stays well defined.
            anchors[i].source = anchors[i - 1].source + 1e-6;
            anchors[i].output = anchors[i - 1].output + 1e-6;
            continue;
        }

        if (anchors[i].rigidWithPrevious)
        {
            anchors[i].output = anchors[i - 1].output + sourceStep;
            continue;
        }

        const double minOutputStep = sourceStep / maxRate;
        const double maxOutputStep = sourceStep / minRate;

        const double wantedStep = anchors[i].output - anchors[i - 1].output;
        const double clampedStep =
            std::clamp(wantedStep, minOutputStep, maxOutputStep);

        anchors[i].output = anchors[i - 1].output + clampedStep;
    }
}

} // namespace

TimingMap buildTimingMap(const std::vector<TimingTarget>& notes,
                         int sourceFrameCount,
                         const TimingParams& params)
{
    TimingMap map;
    if (sourceFrameCount <= 0)
        return map;

    map.noteStart.assign(notes.size(), 0.0);
    map.noteEnd.assign(notes.size(), 0.0);

    // --- anchors -----------------------------------------------------------
    // The take's start is pinned: audio before the first note should not
    // slide, and the map needs a defined value from frame 0 anyway.
    std::vector<Anchor> anchors;
    anchors.push_back({0.0, 0.0, false});

    for (const auto& note : notes)
    {
        const double start = static_cast<double>(note.sourceStartFrame);
        const double end = static_cast<double>(note.sourceEndFrame);
        if (end <= start)
            continue;

        double startShift = 0.0;
        double endShift = 0.0;

        const bool trusted =
            note.hasTarget && note.confidence >= params.minConfidence;

        if (trusted)
        {
            startShift = allowedShift(note.targetStartFrame - start, params,
                                      note.confidence);

            if (params.correctNoteLength)
                endShift = allowedShift(note.targetEndFrame - end, params,
                                        note.confidence);
            else
                endShift = startShift; // move the note whole, keep its length
        }

        anchors.push_back({start, start + startShift, true});
        // Consonant protection: a second anchor at the end of the head,
        // shifted by the same amount, makes the head a rigid copy. Only when
        // it leaves some of the note to stretch.
        const double head = static_cast<double>(std::max(0, note.rigidHeadFrames));
        if (head > 0.0 && start + head < end - 1.0)
            anchors.push_back({start + head, start + head + startShift, false, true});
        anchors.push_back({end, end + endShift, false});
    }

    // The take's end, so trailing audio keeps playing at its own rate.
    anchors.push_back({static_cast<double>(sourceFrameCount),
                       static_cast<double>(sourceFrameCount), false});

    std::stable_sort(
        anchors.begin(), anchors.end(),
        [](const Anchor& a, const Anchor& b) { return a.source < b.source; });

    collapseSharedFrames(anchors);
    enforceRateBounds(anchors, params);

    // --- interpolate -------------------------------------------------------
    const int outputFrames =
        std::max(1, static_cast<int>(std::ceil(anchors.back().output)));
    map.sourceFrame.assign(static_cast<size_t>(outputFrames), 0.0);

    size_t segment = 0;
    for (int i = 0; i < outputFrames; ++i)
    {
        const double out = static_cast<double>(i);

        while (segment + 2 < anchors.size() && anchors[segment + 1].output <= out)
            ++segment;

        const Anchor& a = anchors[segment];
        const Anchor& b = anchors[segment + 1];
        const double outputSpan = b.output - a.output;

        double source;
        if (outputSpan <= 0.0)
            source = a.source;
        else
            source = a.source +
                     (out - a.output) * (b.source - a.source) / outputSpan;

        map.sourceFrame[static_cast<size_t>(i)] =
            std::clamp(source, 0.0, static_cast<double>(sourceFrameCount));
    }

    // --- report where the notes landed -------------------------------------
    // Read back off the finished anchors rather than the requested shifts, so
    // the positions reflect whatever the rate budget actually allowed.
    const auto outputFor = [&anchors](double sourcePosition) {
        if (anchors.empty())
            return sourcePosition;
        if (sourcePosition <= anchors.front().source)
            return anchors.front().output;
        for (size_t i = 1; i < anchors.size(); ++i)
        {
            if (sourcePosition <= anchors[i].source)
            {
                const double span = anchors[i].source - anchors[i - 1].source;
                if (span <= 0.0)
                    return anchors[i].output;
                const double t = (sourcePosition - anchors[i - 1].source) / span;
                return anchors[i - 1].output +
                       t * (anchors[i].output - anchors[i - 1].output);
            }
        }
        return anchors.back().output;
    };

    for (size_t i = 0; i < notes.size(); ++i)
    {
        map.noteStart[i] = outputFor(static_cast<double>(notes[i].sourceStartFrame));
        map.noteEnd[i] = outputFor(static_cast<double>(notes[i].sourceEndFrame));
    }

    return map;
}

} // namespace correction
