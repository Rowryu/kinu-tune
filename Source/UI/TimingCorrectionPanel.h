// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.

#pragma once

#include "../Audio/Correction/TimingCorrection.h"
#include "../Audio/Correction/TransitionSmoothing.h"
#include "../JuceHeader.h"
#include "../Models/Project.h"
#include "../Utils/Localization.h"
#include "Components/StyledWidgets.h"
#include "Workspace/PanelContent.h"

#include <functional>
#include <vector>

// Inspector card for automatic timing correction.
//
// Moves notes in time towards a guide or the host's grid. What gets lined up
// is the note's energy peak rather than its first frame: a syllable opening
// on a consonant starts well before the beat it lands on, so placing note
// starts on a grid drags the phrase late.
//
// The guide comes from the MIDI guide card above, so this one holds no guide
// of its own - a single place to set it up, used by both corrections.
class TimingCorrectionPanel : public juce::Component,
                              public PanelContent,
                              private juce::Timer
{
public:
    TimingCorrectionPanel();
    ~TimingCorrectionPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    int getPreferredHeight() const override;

    void refreshLocalisedText();

    std::function<Project*()> getCurrentProject;

    /// The guide the MIDI guide card currently holds, empty when it has none.
    std::function<std::vector<correction::GuideNote>()> getGuideNotes;

    /// The host's tempo, for grid mode. A single number, so it can only
    /// describe a song that holds one tempo; see getHostTempoMap.
    std::function<double()> getHostBpm;

    /// The song's whole tempo map, when the host hands one over. This is what
    /// grid mode uses when it is there: a song with a tempo track has no
    /// single BPM, and a grid built from one is right where it was read and
    /// further out with every bar after it.
    std::function<std::vector<correction::TempoPoint>()> getHostTempoMap;

    /// Host time, in seconds, of this region's frame zero. Grid mode needs
    /// it: the host's beats are on the song's timeline, and the notes are
    /// numbered from the start of their own audio.
    std::function<double()> getProjectHostOffsetSeconds;

    /// Runs the correction and reports what it did.
    std::function<correction::TimingReport(
        const std::vector<correction::GuideNote>&,
        const correction::TimingCorrectionParams&)>
        onCorrectionRequested;

    std::function<double()> getFramesPerSecond;

    /// Smooth the glide between notes. Returns how many notes changed.
    std::function<int(const correction::TransitionParams&)> onSmoothingRequested;

    /// Re-read what this card depends on and update it if anything moved.
    /// The timer calls this; it is public so a test can drive it without a
    /// message loop.
    void pollForChanges();

    /// Apply, read-only, for the tests that cover when the card is usable.
    const juce::TextButton& getApplyButton() const { return applyButton; }

    /// The status line's text, for those same tests.
    juce::String getStatusText() const { return statusLabel.getText(); }

private:
    void timerCallback() override;
    void runCorrection();
    void runSmoothing();
    void refreshEnablement();
    void showStatus(const juce::String& text, bool isWarning);

    Project* currentProject() const
    {
        return getCurrentProject ? getCurrentProject() : nullptr;
    }

    correction::TimingMode selectedMode() const;
    double framesPerSecond() const;

    juce::Label sectionLabel;

    juce::Label modeLabel;
    juce::ComboBox modeBox;

    juce::Label gridLabel;
    juce::ComboBox gridBox;

    juce::Label strengthLabel;
    StyledSlider strengthSlider;
    juce::Label maxShiftLabel;
    StyledSlider maxShiftSlider;
    juce::Label toleranceLabel;
    StyledSlider toleranceSlider;

    StyledToggleButton lengthToggle;
    StyledToggleButton anchorToggle;
    StyledToggleButton syllableToggle; // grid: split joined syllables first

    juce::Label smoothLabel;
    StyledSlider smoothSlider;
    juce::TextButton smoothButton;

    juce::TextButton applyButton;
    juce::Label statusLabel;

    juce::Rectangle<int> cardBounds;
    bool hadProject = false;
    // The guide arrives from the other card, after this one is built, so
    // its absence has to be noticed rather than assumed to be permanent.
    size_t hadGuideCount = 0;
    // Whether the status line currently holds this card's own "cannot run"
    // warning, so it can be taken down once that stops being true.
    bool statusIsNotReady = false;

    LocalisationWatcher languageWatcher{[this] { refreshLocalisedText(); }};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimingCorrectionPanel)
};
