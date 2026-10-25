// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.

// Chord estimation from audio and chord-aware harmony. Plain g++, no JUCE.

#include "../Source/Audio/Correction/ChordEstimation.h"

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace correction;

namespace
{
int failures = 0, checks = 0;
void check(bool ok, const std::string& what)
{
    ++checks;
    if (!ok) { ++failures; std::printf("  FAIL: %s\n", what.c_str()); }
}
constexpr double kRate = 44100.0;
constexpr double kHop = 512.0;
double hz(int midi) { return 440.0 * std::pow(2.0, (midi - 69) / 12.0); }

void addChord(std::vector<float>& audio, double fromSec, double toSec, std::vector<int> notes)
{
    const size_t a = static_cast<size_t>(fromSec * kRate), b = static_cast<size_t>(toSec * kRate);
    if (audio.size() < b) audio.resize(b, 0.0f);
    for (int n : notes)
        for (int octave : {0, 12}) // a little body, like a pad plus its octave
            for (size_t i = a; i < b; ++i)
                audio[i] += static_cast<float>(0.1 * std::sin(2.0 * 3.14159265358979 * hz(n + octave) * i / kRate));
}
const unsigned C = 1u << 0, E = 1u << 4, G = 1u << 7, A = 1u << 9, F = 1u << 5, Dn = 1u << 2, B = 1u << 11;
const unsigned kCMajorScale = C | Dn | E | F | G | A | B;
}

int main()
{
    std::printf("=== chord estimation tests ===\n");

    // C major for 2 s, A minor for 2 s, F major for 2 s.
    std::vector<float> audio;
    addChord(audio, 0.0, 2.0, {48, 52, 55});
    addChord(audio, 2.0, 4.0, {45, 52, 57, 60});
    addChord(audio, 4.0, 6.0, {41, 53, 57, 60});
    const double fps = kRate / kHop;
    const int frames = static_cast<int>(audio.size() / kHop);
    const auto chroma = chromagram(audio, kRate, kHop, frames);
    const auto chords = estimateChords(chroma, fps);

    const auto maskAt = [&](double sec) {
        const int f = static_cast<int>(sec * fps);
        return chordMaskFor(chords, f, f + 10);
    };
    check(maskAt(1.0) == (C | E | G), "C major found");
    check(maskAt(3.0) == (A | C | E), "A minor found");
    check(maskAt(5.0) == (F | A | C), "F major found");
    check(chords.size() <= 5, "no flicker between chords");

    // Silence is no chord.
    std::vector<float> quiet(static_cast<size_t>(kRate), 0.0f);
    const auto silent = estimateChords(chromagram(quiet, kRate, kHop, 80), fps);
    check(chordMaskFor(silent, 0, 80) == 0, "silence has no chord");

    // Harmony: melody E4 (64) over A minor, diatonic third above is G4 (67),
    // which is not in Am -> moves to A4 (69).
    check(chordAwareTarget(64, 67, A | C | E, kCMajorScale) == 69, "G over Am becomes A");
    // A chord tone stays.
    check(chordAwareTarget(64, 67, C | E | G, kCMajorScale) == 67, "G over C stays");
    // Third below D4 (62) is B3 (59); over C it moves to C? No - C4 is 60,
    // a second from D. Must stay at least a minor third away: G3 (55) is too
    // far (4 st), so A3? Not a chord tone. Nothing fits within 2: keep B3.
    check(chordAwareTarget(62, 59, C | E | G, kCMajorScale) == 59, "no tone fits: keep diatonic");
    // Third below F4 (65) is D4 (62); over C major, C4 (60) is a chord tone,
    // a fourth below F, on the same side.
    check(chordAwareTarget(65, 62, C | E | G, kCMajorScale) == 60, "D under F over C becomes C");
    // No chord: unchanged.
    check(chordAwareTarget(64, 67, 0, kCMajorScale) == 67, "no chord: unchanged");

    std::printf("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
