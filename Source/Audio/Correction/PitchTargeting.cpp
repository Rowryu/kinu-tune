// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.
#include "PitchTargeting.h"

#include <algorithm>
#include <cmath>

namespace correction
{

std::vector<PitchTargetResult> computePitchTargets(
    const std::vector<SungNote>& sung,
    const std::vector<GuideNote>& guide,
    const std::vector<NoteMatch>& matches,
    const PitchTargetParams& params)
{
    std::vector<PitchTargetResult> results(sung.size());

    const float strength = std::clamp(params.correctionStrength, 0.0f, 1.0f);
    const float ceiling = std::max(params.maxShiftSemitones, 0.0f);

    for (size_t i = 0; i < sung.size(); ++i)
    {
        auto& out = results[i];
        out.sungIndex = static_cast<int>(i);
        out.correctedMidiNote = sung[i].midiNote;

        if (i >= matches.size())
            continue;

        const NoteMatch& match = matches[i];
        if (!match.isMatched())
            continue;
        if (match.confidence < params.minConfidence)
            continue;
        if (match.guideIndex >= static_cast<int>(guide.size()))
            continue;

        const float guideMidi =
            guide[static_cast<size_t>(match.guideIndex)].midiNote;
        const float target = params.snapGuideToSemitones
                                 ? std::round(guideMidi)
                                 : guideMidi;
        const float fullShift = target - sung[i].midiNote;

        // Strength is the user's intent; confidence only holds it back while
        // the match is still doubtful, and gets out of the way once it is
        // not. Scaling by raw confidence all the way up made a confident
        // match land short for no reason anyone could see or change.
        const float span = params.trustedConfidence - params.minConfidence;
        const float trust =
            span > 0.0f
                ? std::clamp((match.confidence - params.minConfidence) / span,
                             0.0f, 1.0f)
                : 1.0f;
        float shift = fullShift * strength * trust;
        shift = std::clamp(shift, -ceiling, ceiling);

        // Below the dead band the note is left exactly as it was sung,
        // rather than re-rendered for a move nobody can hear.
        if (std::abs(shift) < std::max(params.minShiftSemitones, 0.0f))
            continue;

        out.correctedMidiNote = sung[i].midiNote + shift;
        out.appliedShiftSemitones = shift;
        out.corrected = true;
    }

    return results;
}

} // namespace correction
