// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.

#pragma once

#include "../Audio/Correction/GuideSource.h"
#include "../Audio/Correction/GuidedPitchCorrection.h"
#include "../JuceHeader.h"
#include "../Models/Project.h"
#include "../Utils/Localization.h"
#include "Components/StyledWidgets.h"
#include "Workspace/PanelContent.h"

#include <functional>
#include <memory>

// Inspector card for correcting pitch against a MIDI guide.
//
// The user picks a MIDI file holding the intended melody; each sung note is
// matched to a guide note and its centre moved towards it, leaving vibrato,
// scoops and the rest of the phrasing untouched. The work happens in
// correction::applyGuidedPitchCorrection via onCorrectionRequested, so this
// card owns the loaded guide and the settings and nothing else.
//
// The tempo row exists because a MIDI export does not always carry the
// project's tempo, and a guide written at the wrong one drifts further out
// of time the longer it runs. Nothing in the file says it is wrong, so the
// user has to be able to say so.
class MidiGuidePanel : public juce::Component,
                       public PanelContent,
                       private juce::Timer
{
public:
    MidiGuidePanel();
    ~MidiGuidePanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    int getPreferredHeight() const override;

    void refreshLocalisedText();

    /// Asked on every refresh rather than cached, so a project replaced by a
    /// file load or an ARA rebind is never read through a stale pointer.
    std::function<Project*()> getCurrentProject;

    /// Applies the correction and returns what it did. The panel supplies
    /// the guide and the settings; the host decides how to record undo.
    std::function<correction::GuidedPitchReport(
        const std::vector<correction::GuideNote>&,
        const correction::PitchTargetParams&)>
        onCorrectionRequested;

    /// Frames per second of the analysis grid, for placing the guide.
    std::function<double()> getFramesPerSecond;

private:
    void timerCallback() override;
    void chooseFile();
    void applyPlacement();
    void runCorrection();
    void refreshTrackMenu();
    void refreshEnablement();
    void showStatus(const juce::String& text, bool isWarning);

    Project* currentProject() const
    {
        return getCurrentProject ? getCurrentProject() : nullptr;
    }

    double framesPerSecond() const;

    correction::LoadedGuide guide;
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::Label sectionLabel;
    juce::TextButton loadButton;
    juce::Label fileLabel;

    juce::Label trackLabel;
    juce::ComboBox trackBox;

    juce::Label projectBpmLabel;
    StyledSlider projectBpmSlider;
    juce::Label midiBpmLabel;
    StyledSlider midiBpmSlider;
    juce::Label offsetLabel;
    StyledSlider offsetSlider;

    juce::Label strengthLabel;
    StyledSlider strengthSlider;
    juce::Label maxShiftLabel;
    StyledSlider maxShiftSlider;

    juce::TextButton applyButton;
    juce::Label statusLabel;

    juce::Rectangle<int> cardBounds;
    bool hadProject = false;

    LocalisationWatcher languageWatcher{[this] { refreshLocalisedText(); }};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiGuidePanel)
};
