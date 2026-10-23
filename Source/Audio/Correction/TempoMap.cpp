// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.

#include "TempoMap.h"

#include <algorithm>
#include <cmath>

namespace correction
{
namespace
{

/// Linear interpolation between two sync points, in either direction.
double across(double from, double a0, double a1, double b0, double b1)
{
    const double span = a1 - a0;
    if (span <= 0.0)
        return b0;
    return b0 + (from - a0) * (b1 - b0) / span;
}

} // namespace

TempoMap::TempoMap(std::vector<TempoPoint> given)
{
    std::stable_sort(given.begin(), given.end(),
                     [](const TempoPoint& a, const TempoPoint& b) {
                         return a.seconds < b.seconds;
                     });

    // A host should never send these, but a map where time or music runs
    // backwards would make the grid non-monotonic and every snap a guess.
    // Dropping the offending point leaves a coarser map rather than a wrong
    // one, which is the better of the two failures.
    points.reserve(given.size());
    for (const auto& point : given)
    {
        if (!points.empty() && (point.seconds <= points.back().seconds ||
                                point.quarters <= points.back().quarters))
            continue;
        points.push_back(point);
    }

    if (points.size() < 2)
        points.clear();
}

double TempoMap::quartersAt(double seconds) const
{
    if (points.size() < 2)
        return 0.0;

    if (seconds <= points.front().seconds)
        return across(seconds, points[0].seconds, points[1].seconds,
                      points[0].quarters, points[1].quarters);

    for (size_t i = 1; i < points.size(); ++i)
    {
        if (seconds <= points[i].seconds)
            return across(seconds, points[i - 1].seconds, points[i].seconds,
                          points[i - 1].quarters, points[i].quarters);
    }

    const size_t last = points.size() - 1;
    return across(seconds, points[last - 1].seconds, points[last].seconds,
                  points[last - 1].quarters, points[last].quarters);
}

double TempoMap::secondsAt(double quarters) const
{
    if (points.size() < 2)
        return 0.0;

    if (quarters <= points.front().quarters)
        return across(quarters, points[0].quarters, points[1].quarters,
                      points[0].seconds, points[1].seconds);

    for (size_t i = 1; i < points.size(); ++i)
    {
        if (quarters <= points[i].quarters)
            return across(quarters, points[i - 1].quarters, points[i].quarters,
                          points[i - 1].seconds, points[i].seconds);
    }

    const size_t last = points.size() - 1;
    return across(quarters, points[last - 1].quarters, points[last].quarters,
                  points[last - 1].seconds, points[last].seconds);
}

} // namespace correction
