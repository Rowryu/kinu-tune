// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.

#include "NoteCentre.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace correction
{

float perceivedCentreMidi(const std::vector<float>& f0Hz,
                          const std::vector<float>& rms,
                          const NoteCentreParams& params)
{
    const size_t n = f0Hz.size();
    std::vector<float> midi(n, -1.0f);
    for (size_t i = 0; i < n; ++i)
        if (f0Hz[i] > 0.0f)
            midi[i] = 69.0f + 12.0f * std::log2(f0Hz[i] / 440.0f);

    float peak = 0.0f;
    for (size_t i = 0; i < n; ++i)
        if (midi[i] >= 0.0f && i < rms.size())
            peak = std::max(peak, rms[i]);

    std::vector<std::pair<float, float>> weighted; // (midi, weight)
    weighted.reserve(n);
    for (size_t i = 0; i < n; ++i)
    {
        if (midi[i] < 0.0f)
            continue;
        const float level = i < rms.size() ? rms[i] : 0.0f;
        float weight = peak > 0.0f ? level / peak : 1.0f;

        float slope = 0.0f;
        int sides = 0;
        if (i > 0 && midi[i - 1] >= 0.0f)
        {
            slope += std::abs(midi[i] - midi[i - 1]);
            ++sides;
        }
        if (i + 1 < n && midi[i + 1] >= 0.0f)
        {
            slope += std::abs(midi[i + 1] - midi[i]);
            ++sides;
        }
        if (sides > 0)
            slope /= static_cast<float>(sides);

        const bool steep = slope > params.steepSemitonesPerFrame;
        const bool quiet = peak > 0.0f && level < params.quietShareOfPeak * peak;
        if (steep && quiet)
            weight *= params.transitionWeight;
        else if (steep || quiet)
            weight *= 0.5f;

        weighted.emplace_back(midi[i], std::max(weight, 1e-6f));
    }

    if (weighted.empty())
        return -1.0f;

    std::sort(weighted.begin(), weighted.end());
    double total = 0.0;
    for (const auto& w : weighted)
        total += w.second;
    double running = 0.0;
    for (const auto& w : weighted)
    {
        running += w.second;
        if (running >= 0.5 * total)
            return w.first;
    }
    return weighted.back().first;
}

} // namespace correction
