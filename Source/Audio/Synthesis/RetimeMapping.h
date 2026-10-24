// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.
#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace retime
{

/**
 * Frames at the start of a note to keep at their own rate when the note is
 * stretched or squeezed: up to the first voiced frame (the consonant) plus a
 * short attack, capped so most of the note is still free to take the change.
 *
 * Stretching a "t" or "k" smears it until it stops sounding like one; a
 * listener does not notice a sustained vowel held a little longer. So when a
 * retimed note changes length, the change goes to the vowel.
 */
inline int headFrames(const std::vector<float>& f0, int sourceStart,
                      int sourceEnd, int destinationLength,
                      int attackFrames = 3, double maxShare = 0.4)
{
    const int sourceLength = sourceEnd - sourceStart;
    if (sourceLength <= 2 || destinationLength <= 2)
        return 0;
    int firstVoiced = sourceStart;
    for (int f = std::max(0, sourceStart);
         f < sourceEnd && f < static_cast<int>(f0.size()); ++f)
    {
        if (f0[static_cast<size_t>(f)] > 0.0f)
        {
            firstVoiced = f;
            break;
        }
    }
    const int head = firstVoiced - sourceStart + std::max(0, attackFrames);
    const int cap = static_cast<int>(std::floor(
        std::min(sourceLength, destinationLength) * std::clamp(maxShare, 0.0, 1.0)));
    return std::clamp(head, 0, cap);
}

/**
 * Source position read for output `frame` of a note moved from
 * [sourceStart, sourceEnd) to [destinationStart, destinationEnd).
 *
 * The first `head` frames are copied one to one; the rest is mapped linearly,
 * with the same half-frame centring the plain stretch has always used. With
 * head = 0 this is exactly the old linear stretch.
 */
inline double sourcePosition(int frame, int sourceStart, int sourceEnd,
                             int destinationStart, int destinationEnd,
                             int head)
{
    const int sourceLength = sourceEnd - sourceStart;
    const int destinationLength = destinationEnd - destinationStart;
    head = std::clamp(head, 0, std::max(0, std::min(sourceLength, destinationLength) - 1));
    const int offset = frame - destinationStart;
    if (offset < head)
        return static_cast<double>(sourceStart + offset);

    const int restSource = sourceLength - head;
    const int restDestination = destinationLength - head;
    if (restSource <= 0 || restDestination <= 0)
        return static_cast<double>(sourceStart + head);
    const double normalized =
        (static_cast<double>(offset - head) + 0.5) / static_cast<double>(restDestination);
    return static_cast<double>(sourceStart + head) + normalized * restSource - 0.5;
}

} // namespace retime
