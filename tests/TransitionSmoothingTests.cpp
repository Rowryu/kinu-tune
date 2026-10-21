// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.

// Choosing how long each note-to-note glide should be. The smoothing itself
// is PitchToolOperations::smoothBoundary; this only decides the lengths.

#include "../Source/Audio/Correction/TransitionSmoothing.h"
#include "TestAssert.h"

#include <algorithm>
#include <cmath>
#include <iostream>

using namespace correction;

namespace
{

constexpr double kFps = 86.1328125;

TransitionNote note(int start, int end, float midi, bool rest = false)
{
    TransitionNote n;
    n.startFrame = start;
    n.endFrame = end;
    n.midiNote = midi;
    n.isRest = rest;
    return n;
}

const NoteTransition* find(const std::vector<NoteTransition>& v, int index)
{
    for (const auto& t : v)
        if (t.noteIndex == index)
            return &t;
    return nullptr;
}

TransitionParams defaults()
{
    TransitionParams p;
    p.framesPerSecond = kFps;
    p.amount = 1.0f;
    return p;
}

} // namespace

int main()
{
    // --- a join produces one shared glide -----------------------------------
    {
        // Two long notes a tone apart, touching.
        const std::vector<TransitionNote> notes = {note(0, 200, 60.0f),
                                                   note(200, 400, 62.0f)};
        const auto plan = planTransitions(notes, defaults());

        CHECK(plan.size() == 2);
        const auto* first = find(plan, 0);
        const auto* second = find(plan, 1);
        CHECK(first != nullptr && second != nullptr);
        CHECK(first->rightFrames > 0);
        CHECK(second->leftFrames > 0);
        // Both sides describe the same glide.
        CHECK(first->rightFrames == second->leftFrames);
        CHECK(first->leftFrames == 0);
        CHECK(second->rightFrames == 0);
    }

    // --- a wider interval glides for longer ---------------------------------
    {
        const std::vector<TransitionNote> step = {note(0, 400, 60.0f),
                                                  note(400, 800, 61.0f)};
        const std::vector<TransitionNote> fifth = {note(0, 400, 60.0f),
                                                   note(400, 800, 67.0f)};

        const auto stepPlan = planTransitions(step, defaults());
        const auto fifthPlan = planTransitions(fifth, defaults());

        CHECK(find(stepPlan, 0)->rightFrames < find(fifthPlan, 0)->rightFrames);
    }

    // --- a leap is left as a leap -------------------------------------------
    {
        // Thirteen semitones, past the default ceiling of an octave.
        const std::vector<TransitionNote> notes = {note(0, 400, 60.0f),
                                                   note(400, 800, 73.0f)};
        CHECK(planTransitions(notes, defaults()).empty());
    }

    // --- a repeated pitch has nothing to glide across -----------------------
    {
        const std::vector<TransitionNote> notes = {note(0, 200, 60.0f),
                                                   note(200, 400, 60.0f)};
        CHECK(planTransitions(notes, defaults()).empty());
    }

    // --- a rest breaks the phrase -------------------------------------------
    {
        const std::vector<TransitionNote> notes = {note(0, 200, 60.0f),
                                                   note(200, 260, 0.0f, true),
                                                   note(260, 460, 62.0f)};
        CHECK(planTransitions(notes, defaults()).empty());
    }

    // --- so does a gap -------------------------------------------------------
    {
        // Twenty frames of silence between them: a breath, not a glide.
        const std::vector<TransitionNote> notes = {note(0, 200, 60.0f),
                                                   note(220, 420, 62.0f)};
        CHECK(planTransitions(notes, defaults()).empty());

        // One frame apart is rounding, not silence.
        const std::vector<TransitionNote> touching = {note(0, 200, 60.0f),
                                                      note(201, 400, 62.0f)};
        CHECK(!planTransitions(touching, defaults()).empty());
    }

    // --- a short note caps the glide ----------------------------------------
    {
        auto params = defaults();
        params.maxShareOfNote = 0.35;

        // A fifth, which would otherwise want a long glide, but the second
        // note is only 20 frames.
        const std::vector<TransitionNote> notes = {note(0, 400, 60.0f),
                                                   note(400, 420, 67.0f)};
        const auto plan = planTransitions(notes, params);
        CHECK(!plan.empty());
        // At most 35% of the shorter note.
        CHECK(find(plan, 0)->rightFrames <= 7);
        CHECK(find(plan, 0)->rightFrames > 0);
    }

    // --- amount scales, and zero disables ------------------------------------
    {
        const std::vector<TransitionNote> notes = {note(0, 400, 60.0f),
                                                   note(400, 800, 65.0f)};

        auto full = defaults();
        auto half = defaults();
        half.amount = 0.5f;
        auto off = defaults();
        off.amount = 0.0f;

        const auto fullPlan = planTransitions(notes, full);
        const auto halfPlan = planTransitions(notes, half);

        CHECK(planTransitions(notes, off).empty());
        CHECK(!halfPlan.empty());
        const int fullFrames = find(fullPlan, 0)->rightFrames;
        const int halfFrames = find(halfPlan, 0)->rightFrames;
        CHECK(std::abs(halfFrames * 2 - fullFrames) <= 2);
    }

    // --- a run of notes joins every pair -------------------------------------
    {
        std::vector<TransitionNote> notes;
        for (int i = 0; i < 5; ++i)
            notes.push_back(note(i * 100, (i + 1) * 100,
                                 60.0f + static_cast<float>(i)));

        const auto plan = planTransitions(notes, defaults());
        CHECK(plan.size() == 5);
        // The ends only have one neighbour each.
        CHECK(find(plan, 0)->leftFrames == 0);
        CHECK(find(plan, 4)->rightFrames == 0);
        // Every interior note is joined on both sides, and each join agrees.
        for (int i = 1; i < 4; ++i)
        {
            CHECK(find(plan, i)->leftFrames > 0);
            CHECK(find(plan, i)->rightFrames > 0);
            CHECK(find(plan, i)->leftFrames == find(plan, i - 1)->rightFrames);
        }
    }

    // --- degenerate inputs ---------------------------------------------------
    {
        CHECK(planTransitions({}, defaults()).empty());
        CHECK(planTransitions({note(0, 100, 60.0f)}, defaults()).empty());

        // Zero-length and inverted notes are skipped, not turned into glides.
        const std::vector<TransitionNote> bad = {note(0, 0, 60.0f),
                                                 note(0, 100, 62.0f),
                                                 note(200, 150, 64.0f)};
        for (const auto& t : planTransitions(bad, defaults()))
            CHECK(t.leftFrames >= 0 && t.rightFrames >= 0);

        auto broken = defaults();
        broken.framesPerSecond = 0.0;
        const std::vector<TransitionNote> ok = {note(0, 200, 60.0f),
                                                note(200, 400, 62.0f)};
        planTransitions(ok, broken); // must not divide by zero
        CHECK(true);
    }

    std::cout << "Transition smoothing tests passed\n";
}
