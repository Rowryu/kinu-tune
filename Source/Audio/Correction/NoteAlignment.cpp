// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.
#include "NoteAlignment.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace correction
{
namespace
{

constexpr float kInfinityCost = std::numeric_limits<float>::max() / 4.0f;

/// Moves the path can make, recorded so the matrix can be walked back.
enum class Move : unsigned char
{
    None,
    Match,     ///< consume one sung note and one guide note
    SkipSung,  ///< consume one sung note, leaving it unmatched
    SkipGuide  ///< consume one guide note, leaving it unsung
};

} // namespace

float pairCost(const SungNote& sung,
               const GuideNote& guide,
               const AlignmentParams& params)
{
    const float pitchDistance =
        std::min(std::abs(sung.midiNote - guide.midiNote),
                 params.maxPitchCostSemitones);

    const double startDeltaFrames =
        static_cast<double>(sung.startFrame) - static_cast<double>(guide.startFrame);
    const double fps = params.framesPerSecond > 0.0 ? params.framesPerSecond : 1.0;
    const float timeDistance =
        std::min(static_cast<float>(std::abs(startDeltaFrames) / fps),
                 params.maxTimeCostSeconds);

    return params.pitchWeight * pitchDistance + params.timeWeight * timeDistance;
}

std::vector<NoteMatch> alignNotesToGuide(const std::vector<SungNote>& sung,
                                         const std::vector<GuideNote>& guide,
                                         const AlignmentParams& params)
{
    const int n = static_cast<int>(sung.size());
    const int m = static_cast<int>(guide.size());

    std::vector<NoteMatch> result(static_cast<size_t>(std::max(n, 0)));
    for (int i = 0; i < n; ++i)
        result[static_cast<size_t>(i)].sungIndex = i;

    if (n == 0 || m == 0)
        return result;

    // cost[i][j] = best total cost of consuming the first i sung notes and
    // the first j guide notes. One extra row and column hold the empty
    // prefixes, which is what lets the path start with a skip.
    const int rows = n + 1;
    const int cols = m + 1;
    std::vector<float> cost(static_cast<size_t>(rows * cols), kInfinityCost);
    std::vector<Move> move(static_cast<size_t>(rows * cols), Move::None);

    const auto at = [cols](int i, int j) { return static_cast<size_t>(i * cols + j); };

    cost[at(0, 0)] = 0.0f;
    for (int i = 1; i <= n; ++i)
    {
        cost[at(i, 0)] = cost[at(i - 1, 0)] + params.skipCost;
        move[at(i, 0)] = Move::SkipSung;
    }
    for (int j = 1; j <= m; ++j)
    {
        cost[at(0, j)] = cost[at(0, j - 1)] + params.skipCost;
        move[at(0, j)] = Move::SkipGuide;
    }

    for (int i = 1; i <= n; ++i)
    {
        for (int j = 1; j <= m; ++j)
        {
            const float matchTotal =
                cost[at(i - 1, j - 1)] +
                pairCost(sung[static_cast<size_t>(i - 1)],
                         guide[static_cast<size_t>(j - 1)], params);
            const float skipSungTotal = cost[at(i - 1, j)] + params.skipCost;
            const float skipGuideTotal = cost[at(i, j - 1)] + params.skipCost;

            float best = matchTotal;
            Move bestMove = Move::Match;
            if (skipSungTotal < best)
            {
                best = skipSungTotal;
                bestMove = Move::SkipSung;
            }
            if (skipGuideTotal < best)
            {
                best = skipGuideTotal;
                bestMove = Move::SkipGuide;
            }

            cost[at(i, j)] = best;
            move[at(i, j)] = bestMove;
        }
    }

    // Walk the path back from the corner, recording only the matches.
    int i = n;
    int j = m;
    while (i > 0 || j > 0)
    {
        const Move step = move[at(i, j)];
        if (step == Move::Match)
        {
            const int sungIndex = i - 1;
            const int guideIndex = j - 1;
            const float c = pairCost(sung[static_cast<size_t>(sungIndex)],
                                     guide[static_cast<size_t>(guideIndex)], params);

            auto& entry = result[static_cast<size_t>(sungIndex)];
            entry.guideIndex = guideIndex;
            const float span = params.zeroConfidenceCost > 0.0f
                                   ? params.zeroConfidenceCost
                                   : 1.0f;
            entry.confidence = std::clamp(1.0f - c / span, 0.0f, 1.0f);

            --i;
            --j;
        }
        else if (step == Move::SkipSung)
        {
            --i;
        }
        else if (step == Move::SkipGuide)
        {
            --j;
        }
        else
        {
            break; // Move::None only occurs at (0,0).
        }
    }

    return result;
}

} // namespace correction
