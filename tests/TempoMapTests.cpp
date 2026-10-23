// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.

// Unit tests for the host tempo map and the grid built on it.
//
// The case that matters is a song with a tempo track. A grid built from one
// BPM reading is right where it was read and wrong everywhere else, and the
// error grows with every bar - which is what made BPM correction land on
// nothing musical in a song whose tempo moves.
//
// Free of JUCE and the Project model so it builds with a plain g++ call.

#include "../Source/Audio/Correction/TempoMap.h"
#include "../Source/Audio/Correction/TimingAnchors.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace correction;

namespace
{

int failures = 0;
int checks = 0;

void check(bool condition, const std::string& what)
{
    ++checks;
    if (!condition)
    {
        ++failures;
        std::printf("  FAIL: %s\n", what.c_str());
    }
}

void checkNear(double actual, double expected, double tolerance,
               const std::string& what)
{
    ++checks;
    if (std::abs(actual - expected) > tolerance)
    {
        ++failures;
        std::printf("  FAIL: %s (got %.4f, expected %.4f +/- %.4f)\n",
                    what.c_str(), actual, expected, tolerance);
    }
}

constexpr double kFps = 86.1328125;

/// The map a host sends for a song that holds one tempo.
TempoMap steady(double bpm, double lengthSeconds = 600.0)
{
    return TempoMap({{0.0, 0.0}, {lengthSeconds, lengthSeconds * bpm / 60.0}});
}

void testSteadyTempoMatchesTheArithmetic()
{
    std::printf("a steady tempo map agrees with the plain calculation\n");

    const auto map = steady(120.0);

    checkNear(map.quartersAt(0.0), 0.0, 1e-9, "the song starts on beat zero");
    checkNear(map.quartersAt(1.0), 2.0, 1e-9, "120 BPM is two beats a second");
    checkNear(map.secondsAt(8.0), 4.0, 1e-9, "and eight beats take four");
}

void testTempoChangeIsFollowed()
{
    std::printf("a tempo change is followed through the song\n");

    // 60 BPM for the first ten seconds (10 quarters), then 120.
    const TempoMap map({{0.0, 0.0}, {10.0, 10.0}, {20.0, 30.0}});

    checkNear(map.quartersAt(5.0), 5.0, 1e-9, "slow half: one beat a second");
    checkNear(map.quartersAt(10.0), 10.0, 1e-9, "the change point itself");
    checkNear(map.quartersAt(15.0), 20.0, 1e-9, "fast half: two a second");

    checkNear(map.secondsAt(20.0), 15.0, 1e-9, "and the inverse agrees");

    // Past the end the last tempo carries on rather than stopping dead.
    checkNear(map.quartersAt(25.0), 40.0, 1e-9, "the last tempo carries on");
    // Before the start, the first one does.
    checkNear(map.quartersAt(-2.0), -2.0, 1e-9, "so does the first, backwards");
}

void testDegenerateMapsAreRefused()
{
    std::printf("a map that cannot describe a timeline is refused\n");

    check(TempoMap(std::vector<TempoPoint>{}).isEmpty(), "no points at all");
    check(TempoMap({{0.0, 0.0}}).isEmpty(), "a single point is not a tempo");
    check(TempoMap({{0.0, 0.0}, {0.0, 4.0}}).isEmpty(),
          "two points at one moment are not a tempo");
    check(TempoMap({{5.0, 5.0}, {1.0, 9.0}}).isEmpty(),
          "music running backwards is dropped, leaving too little");

    const TempoMap good({{0.0, 0.0}, {10.0, 10.0}});
    check(!good.isEmpty(), "two honest points are enough");
}

void testGridWithoutAMapIsUnchanged()
{
    std::printf("with no map the grid is the old single-tempo one\n");

    TimingGrid grid;
    grid.framesPerSecond = kFps;
    grid.bpm = 120.0;
    grid.divisionsPerBeat = 4;    // sixteenths
    grid.firstBeatFrame = 0.0;

    const double step = 0.5 * kFps / 4.0; // 120 BPM: half a second a beat
    checkNear(grid.framesPerDivision(), step, 1e-6, "the spacing is unchanged");
    checkNear(grid.snap(step * 3.4), step * 3.0, 1e-6, "and so is the snap");
}

void testGridFollowsTheMapLateInTheSong()
{
    std::printf("with a map the grid is right late in the song too\n");

    // Four minutes in, after a stretch at 100 BPM, the song is at 200. A
    // grid built from "200 BPM from the top of the song" would put its lines
    // in a different place entirely by here, which is the bug.
    const TempoMap map({{0.0, 0.0},      // 100 BPM for two minutes
                        {120.0, 200.0},  // then 200 BPM
                        {360.0, 1000.0}});

    TimingGrid grid;
    grid.framesPerSecond = kFps;
    grid.divisionsPerBeat = 4;
    grid.tempoMap = map;
    grid.hostOffsetSeconds = 240.0; // this region begins four minutes in

    // At 200 BPM a sixteenth is 0.075s.
    const double sixteenth = 0.075 * kFps;
    checkNear(grid.framesPerDivisionAt(0.0), sixteenth, 1e-3,
              "the spacing is the song's local one");

    // Frame zero of the region is 240s in, which the map puts at quarter
    // 200 + (240-120)/60*200 = 600 - a whole number of beats, so on a line.
    checkNear(grid.snap(0.0), 0.0, 1e-3, "a note on a line stays put");

    // A note a third of a division late rounds back to that line.
    checkNear(grid.snap(sixteenth / 3.0), 0.0, 1e-3, "a near miss rounds back");
    // Two thirds late rounds on to the next one.
    checkNear(grid.snap(sixteenth * 2.0 / 3.0), sixteenth, 1e-3,
              "a wide miss rounds forward");

    // Snapping twice is snapping once: a line is already on the grid.
    checkNear(grid.snap(grid.snap(sixteenth * 10.4)),
              grid.snap(sixteenth * 10.4), 1e-6, "snapping is idempotent");

    // And the grid this replaces could not have been right here. The old one
    // took a single BPM off the transport, which reports the tempo under the
    // playhead - sitting in the song's first section, that is 100, and a 100
    // BPM grid has lines twice as far apart as the song does here. No offset
    // fixes a spacing that is wrong by a factor of two.
    TimingGrid naive;
    naive.framesPerSecond = kFps;
    naive.bpm = 100.0;
    naive.divisionsPerBeat = 4;
    checkNear(naive.framesPerDivision(), sixteenth * 2.0, 1e-3,
              "the old grid's lines really were twice as far apart");
}

void testGridSnapIsMonotonic()
{
    std::printf("the grid never folds back on itself\n");

    const TempoMap map({{0.0, 0.0}, {30.0, 50.0}, {60.0, 140.0}});

    TimingGrid grid;
    grid.framesPerSecond = kFps;
    grid.divisionsPerBeat = 4;
    grid.tempoMap = map;
    grid.hostOffsetSeconds = 10.0;

    double previous = grid.snap(0.0);
    bool monotonic = true;
    for (int frame = 1; frame < 3000; ++frame)
    {
        const double here = grid.snap(static_cast<double>(frame));
        if (here < previous - 1e-6)
            monotonic = false;
        previous = here;
    }
    check(monotonic, "later notes never snap earlier than earlier ones");
}

void testDegenerateMapFallsBackToBpm()
{
    std::printf("an unusable map falls back to the single tempo\n");

    TimingGrid grid;
    grid.framesPerSecond = kFps;
    grid.bpm = 120.0;
    grid.divisionsPerBeat = 4;
    grid.tempoMap = TempoMap({{0.0, 0.0}}); // not enough to be a tempo

    const double step = 0.5 * kFps / 4.0;
    checkNear(grid.framesPerDivision(), step, 1e-6, "the BPM is used instead");
    checkNear(grid.snap(step * 2.4), step * 2.0, 1e-6, "and snapping works");
}

} // namespace

int main()
{
    std::printf("=== tempo map tests ===\n\n");

    testSteadyTempoMatchesTheArithmetic();
    testTempoChangeIsFollowed();
    testDegenerateMapsAreRefused();
    testGridWithoutAMapIsUnchanged();
    testGridFollowsTheMapLateInTheSong();
    testGridSnapIsMonotonic();
    testDegenerateMapFallsBackToBpm();

    std::printf("\n%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
