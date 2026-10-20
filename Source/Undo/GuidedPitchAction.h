// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.
#pragma once

#include "../Audio/Correction/GuidedPitchCorrection.h"
#include "../Models/Project.h"
#include "UndoableAction.h"

#include <functional>
#include <utility>
#include <vector>

/**
 * Undo record for one run of MIDI-guide pitch correction.
 *
 * Stores each moved note's pitch and correction reference before and after,
 * the way PitchToolAction stores parameters rather than whole pitch curves.
 * The reference is part of the record because the inspector's Correct Pitch
 * slider reads it: restoring the pitch alone would leave the slider showing
 * a different value than before the run.
 */
class GuidedPitchAction : public UndoableAction
{
public:
    GuidedPitchAction(Project* project,
                      std::vector<correction::GuidedPitchEdit> edits,
                      std::function<void()> onChanged = nullptr)
        : project(project),
          edits(std::move(edits)),
          onChanged(std::move(onChanged))
    {
    }

    void undo() override
    {
        if (project == nullptr)
            return;
        correction::revertGuidedPitchCorrection(*project, edits);
        if (onChanged)
            onChanged();
    }

    void redo() override
    {
        if (project == nullptr)
            return;
        correction::reapplyGuidedPitchCorrection(*project, edits);
        if (onChanged)
            onChanged();
    }

    juce::String getName() const override { return "MIDI Guide Pitch Correction"; }

private:
    Project* project = nullptr;
    std::vector<correction::GuidedPitchEdit> edits;
    std::function<void()> onChanged;
};
