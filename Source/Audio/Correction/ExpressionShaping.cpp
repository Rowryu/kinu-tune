// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.

#include "ExpressionShaping.h"

#include <algorithm>
#include <cmath>

namespace correction
{
namespace
{

/// One moving-average pass over a window that stays symmetric about the
/// sample, shrinking near the ends rather than repeating the edge value.
/// Clamping instead (repeating the first or last sample to fill the window)
/// biases the average towards that sample; a symmetric window invents no
/// samples and keeps the pass zero-phase.
std::vector<float> movingAveragePass(const std::vector<float>& input,
                                     int windowFrames)
{
    const int n = static_cast<int>(input.size());
    std::vector<float> output(static_cast<size_t>(n), 0.0f);
    if (n == 0)
        return output;

    const int half = std::max(1, windowFrames / 2);

    for (int i = 0; i < n; ++i)
    {
        // The widest symmetric window this sample can carry.
        const int reach = std::min({half, i, n - 1 - i});

        double sum = 0.0;
        for (int k = i - reach; k <= i + reach; ++k)
            sum += static_cast<double>(input[static_cast<size_t>(k)]);

        output[static_cast<size_t>(i)] =
            static_cast<float>(sum / static_cast<double>(2 * reach + 1));
    }

    return output;
}

/// Linear trend of a span, as a slope per frame.
struct Trend
{
    double slope = 0.0;
    bool valid = false;
};

Trend fitTrend(const std::vector<float>& values, int from, int to)
{
    const int count = to - from;
    Trend trend;
    if (count < 2)
        return trend;

    double sumX = 0.0, sumY = 0.0, sumXY = 0.0, sumXX = 0.0;
    for (int i = from; i < to; ++i)
    {
        const double x = static_cast<double>(i);
        const double y = static_cast<double>(values[static_cast<size_t>(i)]);
        sumX += x;
        sumY += y;
        sumXY += x * y;
        sumXX += x * x;
    }

    const double n = static_cast<double>(count);
    const double denominator = n * sumXX - sumX * sumX;
    if (std::abs(denominator) < 1e-12)
        return trend;

    trend.slope = (n * sumXY - sumX * sumY) / denominator;
    trend.valid = true;
    return trend;
}

} // namespace

std::vector<float> zeroPhaseMovingAverage(const std::vector<float>& input,
                                          int windowFrames)
{
    // The window is centred, so a single pass is already zero-phase; the
    // second pass squares the response to steepen the roll-off between the
    // phrasing band and the vibrato band.
    return movingAveragePass(movingAveragePass(input, windowFrames),
                             windowFrames);
}

std::vector<float> shapeDeltaPitch(const std::vector<float>& deltaPitch,
                                   const std::vector<NoteSpan>& notes,
                                   const ExpressionParams& params)
{
    const int n = static_cast<int>(deltaPitch.size());
    if (n == 0)
        return {};

    const float expression = std::clamp(params.expressionPreservation, 0.0f, 1.0f);
    const float vibrato = std::clamp(params.vibratoPreservation, 0.0f, 1.0f);
    const float drift = std::clamp(params.pitchDriftCorrection, 0.0f, 1.0f);

    // Nothing asked for: return the curve untouched rather than pushing it
    // through a filter that would round off its peaks for no reason.
    if (expression == 1.0f && vibrato == 1.0f && drift == 0.0f)
        return deltaPitch;

    const double fps = params.framesPerSecond > 0.0 ? params.framesPerSecond : 1.0;
    const double cutoff = params.vibratoCutoffHz > 0.0f
                              ? static_cast<double>(params.vibratoCutoffHz)
                              : 1.0;
    const int window = std::max(3, static_cast<int>(std::lround(fps / cutoff)));

    // Start from the untouched curve: frames outside every note pass through,
    // because there is no note centre there for an offset to be relative to.
    std::vector<float> output = deltaPitch;

    for (const auto& note : notes)
    {
        const int start = std::max(0, note.startFrame);
        const int end = std::min(n, note.endFrame);
        const int length = end - start;
        if (length < 2)
            continue;

        // The split runs over the note alone, not across the take.
        //
        // deltaPitch is measured from each note's own centre, so it steps at
        // every note boundary. Filtering across a boundary smears that step
        // into the vibrato band, and the step then survives everything the
        // expression control does: measured on a 0.7 s scoop, flattening left
        // 0.56 of the original semitone behind, against 0.07 when the note is
        // filtered on its own. Per note, the step is never inside a window.
        //
        // The cost is at the note's first and last half-window (~170 ms at
        // the default cutoff), where the window has shrunk too far to resolve
        // vibrato and the movement there is treated as articulation. That is
        // the ambiguity itself, not an artefact: at a note's onset there is
        // no way to tell a scoop from the first half-cycle of a vibrato
        // without the onset information this stage does not take. Treating it
        // as articulation is the safer reading, since it leaves the attack
        // alone rather than filtering it.
        const std::vector<float> span(
            deltaPitch.begin() + start, deltaPitch.begin() + end);
        const auto slow = zeroPhaseMovingAverage(span, window);

        std::vector<float> slowShaped = slow;

        // Drift is the note's linear trend, fitted on its interior only. A
        // scoop at the onset or a fall at the release is expression, not
        // drift, and including it in the fit would let the correction flatten
        // exactly what it is meant to protect.
        if (drift > 0.0f && length >= 4)
        {
            const int margin = static_cast<int>(
                std::lround(static_cast<double>(length) *
                            std::clamp(params.edgeExclusionRatio, 0.0f, 0.45f)));
            const int from = margin;
            const int to = length - margin;

            if (to - from >= 2)
            {
                const Trend trend = fitTrend(slow, from, to);
                if (trend.valid)
                {
                    // Remove the tilt but not the level: where the note sits
                    // is the targeting stage's business, not this one's.
                    const double centre = 0.5 * static_cast<double>(length - 1);
                    for (int i = 0; i < length; ++i)
                    {
                        const double tilt =
                            trend.slope * (static_cast<double>(i) - centre);
                        slowShaped[static_cast<size_t>(i)] -=
                            static_cast<float>(tilt * static_cast<double>(drift));
                    }
                }
            }
        }

        for (int i = 0; i < length; ++i)
        {
            const float fast =
                span[static_cast<size_t>(i)] - slow[static_cast<size_t>(i)];
            output[static_cast<size_t>(start + i)] =
                slowShaped[static_cast<size_t>(i)] * expression + fast * vibrato;
        }
    }

    return output;
}

} // namespace correction
