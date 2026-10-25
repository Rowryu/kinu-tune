// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.

#include "ChordEstimation.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>

namespace correction
{
namespace
{

constexpr double kPi = 3.14159265358979323846;

void fft(std::vector<std::complex<double>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double angle = -2.0 * kPi / static_cast<double>(len);
        const std::complex<double> wlen(std::cos(angle), std::sin(angle));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<double> w(1.0, 0.0);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k];
                const auto v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

struct Template
{
    int root;
    ChordQuality quality;
    unsigned mask;
    float bias;
};

unsigned rotate(unsigned relative, int root)
{
    unsigned out = 0;
    for (int i = 0; i < 12; ++i)
        if (relative & (1u << i))
            out |= 1u << ((i + root) % 12);
    return out;
}

std::vector<Template> templates()
{
    struct Shape { ChordQuality quality; unsigned mask; float bias; };
    const Shape shapes[] = {
        {ChordQuality::Major,      (1u << 0) | (1u << 4) | (1u << 7), 1.0f},
        {ChordQuality::Minor,      (1u << 0) | (1u << 3) | (1u << 7), 1.0f},
        {ChordQuality::Dominant7,  (1u << 0) | (1u << 4) | (1u << 7) | (1u << 10), 0.96f},
        {ChordQuality::Minor7,     (1u << 0) | (1u << 3) | (1u << 7) | (1u << 10), 0.96f},
        {ChordQuality::Diminished, (1u << 0) | (1u << 3) | (1u << 6), 0.92f},
        {ChordQuality::Sus4,       (1u << 0) | (1u << 5) | (1u << 7), 0.92f},
    };
    std::vector<Template> out;
    for (int root = 0; root < 12; ++root)
        for (const auto& s : shapes)
            out.push_back({root, s.quality, rotate(s.mask, root), s.bias});
    return out;
}

float similarity(const Chroma& c, unsigned mask)
{
    float dot = 0.0f, norm = 0.0f;
    int tones = 0;
    for (int i = 0; i < 12; ++i)
    {
        norm += c[static_cast<size_t>(i)] * c[static_cast<size_t>(i)];
        if (mask & (1u << i))
        {
            dot += c[static_cast<size_t>(i)];
            ++tones;
        }
    }
    if (norm <= 1e-12f || tones == 0)
        return 0.0f;
    return dot / (std::sqrt(norm) * std::sqrt(static_cast<float>(tones)));
}

} // namespace

std::vector<Chroma> chromagram(const std::vector<float>& mono,
                               double sampleRate,
                               double hopSamples,
                               int frameCount,
                               int stride)
{
    std::vector<Chroma> out(static_cast<size_t>(std::max(0, frameCount)), Chroma{});
    if (mono.empty() || sampleRate <= 0.0 || hopSamples <= 0.0 || frameCount <= 0)
        return out;
    stride = std::max(1, stride);

    // ~93 ms at 44.1 kHz: fine enough to separate semitones down to ~65 Hz.
    size_t size = 1;
    while (static_cast<double>(size) < sampleRate * 0.09)
        size <<= 1;
    std::vector<double> window(size);
    for (size_t i = 0; i < size; ++i)
        window[i] = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) /
                                         static_cast<double>(size - 1));

    // Bin -> pitch class, for 65 Hz .. 2.1 kHz: the range chords live in.
    std::vector<int> binClass(size / 2, -1);
    for (size_t b = 1; b < size / 2; ++b)
    {
        const double freq = static_cast<double>(b) * sampleRate / static_cast<double>(size);
        if (freq < 65.0 || freq > 2100.0)
            continue;
        const double midi = 69.0 + 12.0 * std::log2(freq / 440.0);
        binClass[b] = ((static_cast<int>(std::lround(midi)) % 12) + 12) % 12;
    }

    std::vector<std::complex<double>> buffer(size);
    Chroma held{};
    for (int frame = 0; frame < frameCount; ++frame)
    {
        if (frame % stride != 0)
        {
            out[static_cast<size_t>(frame)] = held;
            continue;
        }
        const double centre = (static_cast<double>(frame) + 0.5) * hopSamples;
        const long long first = static_cast<long long>(centre) - static_cast<long long>(size / 2);
        for (size_t i = 0; i < size; ++i)
        {
            const long long s = first + static_cast<long long>(i);
            const double v = (s >= 0 && s < static_cast<long long>(mono.size()))
                                 ? mono[static_cast<size_t>(s)] : 0.0;
            buffer[i] = {v * window[i], 0.0};
        }
        fft(buffer);
        Chroma c{};
        double energy = 0.0;
        for (size_t b = 1; b < size / 2; ++b)
        {
            if (binClass[b] < 0)
                continue;
            const double mag = std::abs(buffer[b]);
            c[static_cast<size_t>(binClass[b])] += static_cast<float>(mag);
            energy += mag * mag;
        }
        // Quiet frames carry no chord; leave them at zero so "no chord" wins.
        if (energy < 1e-6)
            c.fill(0.0f);
        held = c;
        out[static_cast<size_t>(frame)] = c;
    }
    return out;
}

std::vector<ChordSpan> estimateChords(const std::vector<Chroma>& chroma,
                                      double framesPerSecond,
                                      double minChordSeconds)
{
    std::vector<ChordSpan> spans;
    const int n = static_cast<int>(chroma.size());
    if (n == 0)
        return spans;

    const auto chords = templates();
    const int states = static_cast<int>(chords.size()) + 1; // last = no chord
    const int none = states - 1;
    constexpr float kNoChordScore = 0.55f;
    // Per frame at the full analysis rate; about half a second of a better
    // match is needed to justify a change.
    const float switchPenalty =
        static_cast<float>(0.5 * std::max(1.0, framesPerSecond) * 0.12);

    std::vector<float> score(static_cast<size_t>(states), 0.0f);
    std::vector<float> next(static_cast<size_t>(states));
    std::vector<int> back(static_cast<size_t>(n) * static_cast<size_t>(states), 0);

    const auto emission = [&](int frame, int state) {
        if (state == none)
            return kNoChordScore;
        const auto& t = chords[static_cast<size_t>(state)];
        return similarity(chroma[static_cast<size_t>(frame)], t.mask) * t.bias;
    };

    for (int s = 0; s < states; ++s)
        score[static_cast<size_t>(s)] = emission(0, s);

    for (int f = 1; f < n; ++f)
    {
        int best = 0;
        for (int s = 1; s < states; ++s)
            if (score[static_cast<size_t>(s)] > score[static_cast<size_t>(best)])
                best = s;
        for (int s = 0; s < states; ++s)
        {
            const float stay = score[static_cast<size_t>(s)];
            const float jump = score[static_cast<size_t>(best)] - switchPenalty;
            const bool fromSelf = stay >= jump;
            next[static_cast<size_t>(s)] = (fromSelf ? stay : jump) + emission(f, s);
            back[static_cast<size_t>(f) * static_cast<size_t>(states) + static_cast<size_t>(s)] =
                fromSelf ? s : best;
        }
        // Keep the numbers small; only differences matter.
        const float top = *std::max_element(next.begin(), next.end());
        for (int s = 0; s < states; ++s)
            score[static_cast<size_t>(s)] = next[static_cast<size_t>(s)] - top;
    }

    std::vector<int> path(static_cast<size_t>(n));
    int state = static_cast<int>(std::max_element(score.begin(), score.end()) - score.begin());
    for (int f = n - 1; f >= 0; --f)
    {
        path[static_cast<size_t>(f)] = state;
        state = back[static_cast<size_t>(f) * static_cast<size_t>(states) + static_cast<size_t>(state)];
    }

    for (int f = 0; f < n;)
    {
        int g = f;
        while (g < n && path[static_cast<size_t>(g)] == path[static_cast<size_t>(f)])
            ++g;
        ChordSpan span;
        span.startFrame = f;
        span.endFrame = g;
        const int s = path[static_cast<size_t>(f)];
        if (s != none)
        {
            const auto& t = chords[static_cast<size_t>(s)];
            span.root = t.root;
            span.quality = t.quality;
            span.toneMask = t.mask;
        }
        spans.push_back(span);
        f = g;
    }

    // Fold spans too short to be a chord into the longer neighbour.
    const int minFrames = static_cast<int>(std::lround(minChordSeconds * framesPerSecond));
    bool changed = true;
    while (changed && spans.size() > 1)
    {
        changed = false;
        for (size_t i = 0; i < spans.size(); ++i)
        {
            if (spans[i].endFrame - spans[i].startFrame >= minFrames)
                continue;
            size_t into;
            if (i == 0)
                into = 1;
            else if (i + 1 == spans.size())
                into = i - 1;
            else
                into = (spans[i - 1].endFrame - spans[i - 1].startFrame) >=
                               (spans[i + 1].endFrame - spans[i + 1].startFrame)
                           ? i - 1 : i + 1;
            spans[into].startFrame = std::min(spans[into].startFrame, spans[i].startFrame);
            spans[into].endFrame = std::max(spans[into].endFrame, spans[i].endFrame);
            spans.erase(spans.begin() + static_cast<long>(i));
            changed = true;
            break;
        }
    }
    // Merge neighbours that ended up as the same chord.
    for (size_t i = 1; i < spans.size();)
    {
        if (spans[i].root == spans[i - 1].root && spans[i].toneMask == spans[i - 1].toneMask)
        {
            spans[i - 1].endFrame = spans[i].endFrame;
            spans.erase(spans.begin() + static_cast<long>(i));
        }
        else
            ++i;
    }
    return spans;
}

unsigned chordMaskFor(const std::vector<ChordSpan>& chords, int startFrame, int endFrame)
{
    unsigned best = 0;
    int bestOverlap = 0;
    for (const auto& c : chords)
    {
        const int overlap = std::min(endFrame, c.endFrame) - std::max(startFrame, c.startFrame);
        if (overlap > bestOverlap)
        {
            bestOverlap = overlap;
            best = c.root >= 0 ? c.toneMask : 0u;
        }
    }
    return best;
}

int chordAwareTarget(int melodyMidi, int diatonicTarget, unsigned chordMask, unsigned scaleMask)
{
    const auto pc = [](int midi) { return ((midi % 12) + 12) % 12; };
    if (chordMask == 0 || (chordMask & (1u << pc(diatonicTarget))))
        return diatonicTarget;

    const int side = diatonicTarget >= melodyMidi ? 1 : -1;
    int best = diatonicTarget;
    int bestCost = std::numeric_limits<int>::max();
    for (int c = diatonicTarget - 2; c <= diatonicTarget + 2; ++c)
    {
        if (!(chordMask & (1u << pc(c))))
            continue;
        if ((c - melodyMidi) * side < 3) // same side, at least a minor third away
            continue;
        const bool inKey = scaleMask == 0 || (scaleMask & (1u << pc(c)));
        const int cost = std::abs(c - diatonicTarget) * 2 + (inKey ? 0 : 3);
        if (cost < bestCost)
        {
            bestCost = cost;
            best = c;
        }
    }
    return best;
}

} // namespace correction
