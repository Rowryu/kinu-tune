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
#include "IMainView.h"
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

    /// Other tracks that could serve as the guide. In ARA every instance of
    /// the plug-in shares one document, so a track with the plug-in on it has
    /// been analysed and its notes are readable from here - which is how
    /// Melodyne's multi-track editing works, and what makes a sung guide
    /// possible. Empty outside ARA, and the card then offers only a file.
    std::function<std::vector<MainViewTrackEntry>()> listGuideTracks;

    /// The chosen track's notes, already on THIS region's frame grid. The
    /// plug-in layer does the conversion because it knows both placements.
    std::function<std::vector<correction::GuideNote>(const juce::String&)>
        buildGuideFromTrack;

    /// The host's tempo, when there is a host. Saves the user typing a
    /// number the DAW already knows; zero or unset means "no host tempo".
    std::function<double()> getHostBpm;

    /// Re-read the track list. Called when the provider is installed and
    /// whenever the set of tracks may have changed.
    void refreshGuideSources();

    /// The guide Apply would use, for the timing card to share.
    std::vector<correction::GuideNote> getActiveGuideNotes() const
    {
        return activeGuideNotes();
    }

    /// The guide track menu, read-only. Exposed so a test can check when the
    /// card offers one and what it says when it cannot; nothing else should
    /// reach in here.
    const juce::ComboBox& getGuideTrackMenu() const { return guideTrackBox; }

private:
    void timerCallback() override;
    void chooseFile();
    void applyPlacement();
    void runCorrection();
    void refreshTrackMenu();
    void adoptHostTempo();
    void refreshGuideTrackMenu();
    void adoptTrackGuide();
    void refreshEnablement();
    void showStatus(const juce::String& text, bool isWarning);

    Project* currentProject() const
    {
        return getCurrentProject ? getCurrentProject() : nullptr;
    }

    double framesPerSecond() const;

    // Where the guide comes from. A card can hold a loaded file and a chosen
    // track at once; this says which one Apply will use, so switching back
    // and forth does not make the user set the other up again.
    enum class Source { None, MidiFile, Track };
    Source source = Source::None;

    correction::LoadedGuide guide;          ///< the MIDI file, when loaded
    std::vector<correction::GuideNote> trackGuide; ///< the track, when chosen
    juce::String guideTrackKey;
    std::vector<MainViewTrackEntry> guideTrackOptions;
    std::unique_ptr<juce::FileChooser> fileChooser;

    /// The notes Apply will correct against, whichever source is active.
    const std::vector<correction::GuideNote>& activeGuideNotes() const
    {
        return source == Source::Track ? trackGuide : guide.notes;
    }

    juce::Label sectionLabel;
    juce::Label guideTrackLabel;
    juce::ComboBox guideTrackBox;
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
    juce::ToggleButton snapGuideToggle;
    juce::Label maxShiftLabel;
    StyledSlider maxShiftSlider;

    juce::TextButton applyButton;
    juce::Label statusLabel;

    juce::Rectangle<int> cardBounds;
    bool hadProject = false;
    // Set once the user moves the tempo slider, after which the host no
    // longer overwrites it.
    bool projectBpmTouched = false;

    LocalisationWatcher languageWatcher{[this] { refreshLocalisedText(); }};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiGuidePanel)
};
