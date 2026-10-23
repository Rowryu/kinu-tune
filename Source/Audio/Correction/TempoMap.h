// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.
#pragma once

#include <vector>

namespace correction
{

/// One point where the song's timeline pins seconds to quarter notes.
struct TempoPoint
{
    double seconds = 0.0;
    double quarters = 0.0;
};

/**
 * The song's tempo, as the host describes it: a list of sync points with a
 * constant tempo between each neighbouring pair.
 *
 * This is ARA's own shape for a tempo map (kARAContentTypeTempoEntries), kept
 * free of the SDK so the arithmetic can be tested on its own. A song at one
 * tempo arrives as two points and behaves exactly like a single BPM; a song
 * with a tempo track arrives as one point per change, and that is the case a
 * single BPM cannot describe - it is right at the start of the song and drifts
 * further out with every bar after it.
 *
 * Outside the points the first and last segments' tempi carry on, so a region
 * that begins before the first sync point still gets a sensible grid rather
 * than a cliff.
 */
class TempoMap
{
public:
    TempoMap() = default;
    explicit TempoMap(std::vector<TempoPoint> points);

    /// True when there is nothing usable here and the caller should fall back
    /// to whatever single tempo it has.
    bool isEmpty() const { return points.size() < 2; }

    const std::vector<TempoPoint>& getPoints() const { return points; }

    /// Quarter-note position of a moment in the song.
    double quartersAt(double seconds) const;
    /// The moment a quarter-note position falls on.
    double secondsAt(double quarters) const;

private:
    /// Sorted by seconds, with points that would make time run backwards
    /// dropped. Fewer than two means unusable.
    std::vector<TempoPoint> points;
};

} // namespace correction
