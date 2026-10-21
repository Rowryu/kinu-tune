// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.

#include "MidiGuidePanel.h"

#include <cmath>
#include <cstdint>
#include <vector>

#include "../Utils/Constants.h"
#include "../Utils/UI/Theme.h"
#include "Components/AppFont.h"

namespace
{
constexpr int kCardPadX = 5;
constexpr int kCardPadY = 6;
constexpr int kInnerPadX = 10;
constexpr int kInnerPadY = 8;
constexpr int kRowGap = 5;
constexpr int kSectionLabelHeight = 20;
constexpr int kRowHeight = 22;
constexpr int kLabelHeight = 16;
constexpr int kButtonHeight = 24;
constexpr int kStatusRowHeight = 30;

// Section label, load row, file name, track label + menu, three placement
// rows, two setting rows, apply button, status.
constexpr int kLabelledRow = kLabelHeight + kRowHeight;
constexpr int kPreferredHeight =
    kCardPadY * 2 + kInnerPadY * 2 + kSectionLabelHeight + kRowGap +
    kButtonHeight + kRowGap + kLabelHeight + kRowGap +
    kLabelledRow + kRowGap +            // guide track
    kLabelledRow + kRowGap +            // midi file track
    (kLabelledRow + kRowGap) * 3 +      // project bpm, midi bpm, offset
    (kLabelledRow + kRowGap) * 2 +      // strength, max shift
    kButtonHeight + kRowGap + kStatusRowHeight;

void styleCaption(juce::Label& label)
{
    label.setFont(AppFont::getFont(12.0f));
    label.setColour(juce::Label::textColourId, APP_COLOR_TEXT_MUTED);
}

void styleButton(juce::TextButton& button)
{
    button.setColour(juce::TextButton::buttonColourId, APP_COLOR_SURFACE_ALT);
    button.setColour(juce::TextButton::buttonOnColourId, APP_COLOR_SURFACE_RAISED);
    button.setColour(juce::TextButton::textColourOffId, APP_COLOR_TEXT_PRIMARY);
    button.setColour(juce::TextButton::textColourOnId, APP_COLOR_TEXT_PRIMARY);
}
} // namespace

MidiGuidePanel::MidiGuidePanel()
{
    sectionLabel.setFont(AppFont::getBoldFont(16.0f));
    sectionLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF9B9B9Bu));
    addAndMakeVisible(sectionLabel);

    styleCaption(guideTrackLabel);
    addAndMakeVisible(guideTrackLabel);
    guideTrackBox.onChange = [this]
    {
        const int id = guideTrackBox.getSelectedId();
        if (id <= 1)
        {
            guideTrackKey = {};
            trackGuide.clear();
            if (source == Source::Track)
                source = guide.isLoaded() ? Source::MidiFile : Source::None;
            applyPlacement();
            return;
        }

        const size_t index = static_cast<size_t>(id - 2);
        if (index >= guideTrackOptions.size())
            return;
        guideTrackKey = guideTrackOptions[index].key;
        adoptTrackGuide();
    };
    addAndMakeVisible(guideTrackBox);

    styleButton(loadButton);
    loadButton.onClick = [this] { chooseFile(); };
    addAndMakeVisible(loadButton);

    fileLabel.setFont(AppFont::getFont(12.0f));
    fileLabel.setColour(juce::Label::textColourId, APP_COLOR_TEXT_MUTED);
    addAndMakeVisible(fileLabel);

    styleCaption(trackLabel);
    addAndMakeVisible(trackLabel);
    trackBox.onChange = [this]
    {
        const int id = trackBox.getSelectedId();
        guide.selectedTrack = id <= 1 ? -1 : id - 2;
        applyPlacement();
    };
    addAndMakeVisible(trackBox);

    // The project tempo is only ever used as a ratio against the file's, so
    // the range just has to cover what people actually work at.
    styleCaption(projectBpmLabel);
    projectBpmSlider.setRange(20.0, 400.0, 0.01);
    projectBpmSlider.setValue(120.0, juce::dontSendNotification);
    projectBpmSlider.onValueChange = [this]
    {
        projectBpmTouched = true;
        guide.placement.projectBpm = projectBpmSlider.getValue();
        applyPlacement();
    };
    addAndMakeVisible(projectBpmLabel);
    addAndMakeVisible(projectBpmSlider);

    // Zero means "trust the file", which is the common case; it is the
    // bottom of the range so the control reads as off until it is needed.
    styleCaption(midiBpmLabel);
    midiBpmSlider.setRange(0.0, 400.0, 0.01);
    midiBpmSlider.setValue(0.0, juce::dontSendNotification);
    midiBpmSlider.onValueChange = [this]
    {
        guide.placement.midiFileBpm = midiBpmSlider.getValue();
        applyPlacement();
    };
    addAndMakeVisible(midiBpmLabel);
    addAndMakeVisible(midiBpmSlider);

    styleCaption(offsetLabel);
    offsetSlider.setRange(-60.0, 60.0, 0.001);
    offsetSlider.setValue(0.0, juce::dontSendNotification);
    offsetSlider.onValueChange = [this]
    {
        guide.placement.offsetSeconds = offsetSlider.getValue();
        applyPlacement();
    };
    addAndMakeVisible(offsetLabel);
    addAndMakeVisible(offsetSlider);

    // Measured against real material, a strength near 1 reproduced a
    // correction the user had accepted to within about ten cents. The
    // default stays below that: this plug-in exists to keep a performance
    // sounding sung, and pinning every note to the guide is the thing it is
    // meant to avoid. It is one drag away for anyone who wants it.
    styleCaption(strengthLabel);
    strengthSlider.setRange(0.0, 1.0, 0.01);
    strengthSlider.setValue(0.75, juce::dontSendNotification);
    addAndMakeVisible(strengthLabel);
    addAndMakeVisible(strengthSlider);

    styleCaption(maxShiftLabel);
    maxShiftSlider.setRange(0.0, 12.0, 0.1);
    maxShiftSlider.setValue(2.0, juce::dontSendNotification);
    addAndMakeVisible(maxShiftLabel);
    addAndMakeVisible(maxShiftSlider);

    styleButton(applyButton);
    applyButton.onClick = [this] { runCorrection(); };
    addAndMakeVisible(applyButton);

    statusLabel.setFont(AppFont::getFont(12.0f));
    statusLabel.setColour(juce::Label::textColourId, APP_COLOR_TEXT_MUTED);
    statusLabel.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(statusLabel);

    refreshLocalisedText();
    refreshTrackMenu();
    adoptHostTempo();
    refreshEnablement();

    // The project is replaced by file loads and ARA rebinds, so the card
    // polls rather than being told.
    startTimerHz(4);
}

MidiGuidePanel::~MidiGuidePanel()
{
    stopTimer();
}

int MidiGuidePanel::getPreferredHeight() const
{
    return kPreferredHeight;
}

double MidiGuidePanel::framesPerSecond() const
{
    if (getFramesPerSecond)
    {
        const double fps = getFramesPerSecond();
        if (fps > 0.0)
            return fps;
    }
    return static_cast<double>(SAMPLE_RATE) / static_cast<double>(HOP_SIZE);
}

void MidiGuidePanel::refreshLocalisedText()
{
    sectionLabel.setText(TR("midiguide.title"), juce::dontSendNotification);
    guideTrackLabel.setText(TR("midiguide.guide_track"), juce::dontSendNotification);
    loadButton.setButtonText(TR("midiguide.load"));
    trackLabel.setText(TR("midiguide.track"), juce::dontSendNotification);
    projectBpmLabel.setText(TR("midiguide.project_bpm"), juce::dontSendNotification);
    midiBpmLabel.setText(TR("midiguide.midi_bpm"), juce::dontSendNotification);
    offsetLabel.setText(TR("midiguide.offset"), juce::dontSendNotification);
    strengthLabel.setText(TR("midiguide.strength"), juce::dontSendNotification);
    maxShiftLabel.setText(TR("midiguide.max_shift"), juce::dontSendNotification);
    applyButton.setButtonText(TR("midiguide.apply"));

    if (!guide.isLoaded())
        fileLabel.setText(TR("midiguide.no_file"), juce::dontSendNotification);

    refreshTrackMenu();
    refreshGuideTrackMenu();
}

void MidiGuidePanel::refreshGuideSources()
{
    refreshGuideTrackMenu();
    adoptHostTempo();
    refreshEnablement();
}

void MidiGuidePanel::adoptHostTempo()
{
    if (!getHostBpm)
        return;

    const double bpm = getHostBpm();
    if (bpm <= 0.0)
        return;

    // Only while the user has not set it themselves. Typing a tempo is how
    // someone corrects a project whose tempo map the plug-in cannot see, and
    // overwriting that from the host would undo it on the next refresh.
    if (projectBpmTouched)
        return;

    if (std::abs(projectBpmSlider.getValue() - bpm) < 0.001)
        return;

    projectBpmSlider.setValue(bpm, juce::dontSendNotification);
    guide.placement.projectBpm = bpm;
    if (guide.isLoaded())
        applyPlacement();
}

void MidiGuidePanel::refreshGuideTrackMenu()
{
    guideTrackOptions = listGuideTracks ? listGuideTracks()
                                        : std::vector<MainViewTrackEntry>{};

    const auto previousKey = guideTrackKey;
    guideTrackBox.clear(juce::dontSendNotification);
    guideTrackBox.addItem(TR("midiguide.guide_track_none"), 1);

    int wanted = 1;
    for (size_t i = 0; i < guideTrackOptions.size(); ++i)
    {
        const auto& track = guideTrackOptions[i];
        const auto name = track.name.isNotEmpty()
                              ? track.name
                              : TR("midiguide.track_n") + " " + juce::String(i + 1);
        guideTrackBox.addItem(name, static_cast<int>(i) + 2);
        if (track.key == previousKey)
            wanted = static_cast<int>(i) + 2;
    }

    // A chosen track that is gone (region closed, plug-in removed) must not
    // leave a stale guide behind.
    if (wanted == 1 && previousKey.isNotEmpty())
    {
        guideTrackKey = {};
        trackGuide.clear();
        if (source == Source::Track)
            source = guide.isLoaded() ? Source::MidiFile : Source::None;
    }

    guideTrackBox.setSelectedId(wanted, juce::dontSendNotification);
    guideTrackLabel.setVisible(!guideTrackOptions.empty());
    guideTrackBox.setVisible(!guideTrackOptions.empty());
}

void MidiGuidePanel::adoptTrackGuide()
{
    trackGuide.clear();
    if (buildGuideFromTrack && guideTrackKey.isNotEmpty())
        trackGuide = buildGuideFromTrack(guideTrackKey);

    source = trackGuide.empty() ? Source::None : Source::Track;
    refreshEnablement();

    if (trackGuide.empty())
    {
        showStatus(TR("midiguide.status_track_empty"), true);
        return;
    }

    const double fps = framesPerSecond();
    showStatus(TR("midiguide.status_loaded")
                   .replace("{notes}", juce::String(trackGuide.size()))
                   .replace("{from}",
                            juce::String(trackGuide.front().startFrame / fps, 2))
                   .replace("{to}",
                            juce::String(trackGuide.back().endFrame / fps, 2)),
               false);
}

void MidiGuidePanel::timerCallback()
{
    const bool hasProject = currentProject() != nullptr;
    if (hasProject != hadProject)
    {
        hadProject = hasProject;
        refreshEnablement();
    }

    adoptHostTempo();

    // Tracks appear as the host creates regions and the analysis finishes, so
    // the list is polled rather than pushed. Rebuild only when the set
    // actually changed, or the menu would close itself under the user.
    if (listGuideTracks)
    {
        const auto tracks = listGuideTracks();
        bool changed = tracks.size() != guideTrackOptions.size();
        for (size_t i = 0; !changed && i < tracks.size(); ++i)
            changed = tracks[i].key != guideTrackOptions[i].key ||
                      tracks[i].name != guideTrackOptions[i].name;
        if (changed && !guideTrackBox.isPopupActive())
            refreshGuideTrackMenu();
    }
}

void MidiGuidePanel::refreshEnablement()
{
    const bool loaded = guide.isLoaded();
    const bool usingFile = source == Source::MidiFile;
    const bool haveGuide = !activeGuideNotes().empty();
    const bool ready = haveGuide && currentProject() != nullptr;

    // Tempo and offset place a FILE against the audio. A guide read from
    // another track is already on the host timeline, so those controls would
    // do nothing and are disabled rather than left looking live.
    trackBox.setEnabled(usingFile && guide.midi.tracksWithNotes.size() > 1);
    projectBpmSlider.setEnabled(usingFile && loaded);
    midiBpmSlider.setEnabled(usingFile && loaded);
    offsetSlider.setEnabled(usingFile && loaded);

    strengthSlider.setEnabled(haveGuide);
    maxShiftSlider.setEnabled(haveGuide);
    applyButton.setEnabled(ready);
}

void MidiGuidePanel::refreshTrackMenu()
{
    const int previous = trackBox.getSelectedId();
    trackBox.clear(juce::dontSendNotification);
    trackBox.addItem(TR("midiguide.track_all"), 1);
    for (int track : guide.midi.tracksWithNotes)
        trackBox.addItem(TR("midiguide.track_n") + " " + juce::String(track + 1),
                         track + 2);

    const int wanted =
        guide.selectedTrack < 0 ? 1 : guide.selectedTrack + 2;
    trackBox.setSelectedId(trackBox.indexOfItemId(wanted) >= 0 ? wanted
                                                               : previous > 0 ? previous : 1,
                           juce::dontSendNotification);
}

void MidiGuidePanel::chooseFile()
{
    if (fileChooser != nullptr)
        return;

    fileChooser = std::make_unique<juce::FileChooser>(
        TR("midiguide.choose_file"), juce::File{}, "*.mid;*.midi");

    const auto flags = juce::FileBrowserComponent::openMode |
                       juce::FileBrowserComponent::canSelectFiles;

    juce::Component::SafePointer<MidiGuidePanel> safeThis(this);
    fileChooser->launchAsync(
        flags,
        [safeThis](const juce::FileChooser& fc)
        {
            if (safeThis == nullptr)
                return;

            const auto file = fc.getResult();
            safeThis->fileChooser.reset(); // let the next dialog open

            if (!file.existsAsFile())
                return;

            // Read through JUCE rather than by path. std::ifstream takes a
            // narrow path in the system code page on Windows, so a UTF-8
            // path - which is what juce::String gives - cannot open a file
            // whose name is not ASCII. A Japanese file name failed here.
            juce::MemoryBlock block;
            if (!file.loadFileAsData(block) || block.getSize() == 0)
            {
                safeThis->showStatus(TR("midiguide.load_failed") + " " +
                                         TR("midiguide.load_unreadable"),
                                     true);
                return;
            }

            const auto* raw = static_cast<const std::uint8_t*>(block.getData());
            const std::vector<std::uint8_t> bytes(raw, raw + block.getSize());

            std::string error;
            correction::LoadedGuide loaded;
            if (!correction::loadGuideFromBytes(
                    bytes, file.getFileName().toStdString(), loaded, error))
            {
                safeThis->showStatus(TR("midiguide.load_failed") + " " +
                                         juce::String(error),
                                     true);
                return;
            }

            // Keep the placement the user has already dialled in: reloading
            // a re-exported file should not silently discard a tempo they
            // had to work out by ear.
            loaded.placement = safeThis->guide.placement;
            safeThis->guide = std::move(loaded);

            safeThis->fileLabel.setText(
                juce::String::fromUTF8(safeThis->guide.fileName.c_str()),
                juce::dontSendNotification);
            safeThis->source = Source::MidiFile;
            safeThis->refreshTrackMenu();
            safeThis->applyPlacement();
        });
}

void MidiGuidePanel::applyPlacement()
{
    if (source == Source::Track)
    {
        adoptTrackGuide();
        return;
    }

    if (!guide.isLoaded())
    {
        refreshEnablement();
        return;
    }

    correction::rebuildGuideNotes(guide, framesPerSecond());
    refreshEnablement();

    if (guide.notes.empty())
    {
        showStatus(TR("midiguide.status_no_notes"), true);
        return;
    }

    const double fps = framesPerSecond();
    const double first = guide.notes.front().startFrame / fps;
    const double last = guide.notes.back().endFrame / fps;

    auto* project = currentProject();
    const int audioFrames = project ? project->getAudioData().getNumFrames() : 0;

    juce::String text =
        TR("midiguide.status_loaded")
            .replace("{notes}", juce::String(guide.notes.size()))
            .replace("{from}", juce::String(first, 2))
            .replace("{to}", juce::String(last, 2));

    // A guide that lands outside the take is the symptom of a wrong tempo or
    // offset, and it is worth saying so before the user presses Apply and
    // sees nothing happen.
    bool warn = false;
    if (audioFrames > 0 && (last < 0.0 || first > audioFrames / fps))
    {
        text = TR("midiguide.status_outside");
        warn = true;
    }

    showStatus(text, warn);
}

void MidiGuidePanel::runCorrection()
{
    if (!onCorrectionRequested || activeGuideNotes().empty())
        return;

    correction::PitchTargetParams params;
    params.correctionStrength = static_cast<float>(strengthSlider.getValue());
    params.maxShiftSemitones = static_cast<float>(maxShiftSlider.getValue());

    const auto report = onCorrectionRequested(activeGuideNotes(), params);

    if (report.notesConsidered == 0)
    {
        showStatus(TR("midiguide.status_analyse_first"), true);
        return;
    }

    showStatus(TR("midiguide.status_corrected")
                   .replace("{corrected}", juce::String(report.notesCorrected))
                   .replace("{matched}", juce::String(report.notesMatched))
                   .replace("{total}", juce::String(report.notesConsidered)),
               report.notesCorrected == 0);
}

void MidiGuidePanel::showStatus(const juce::String& text, bool isWarning)
{
    statusLabel.setColour(juce::Label::textColourId,
                          isWarning ? APP_COLOR_ALERT_WARNING : APP_COLOR_TEXT_MUTED);
    statusLabel.setText(text, juce::dontSendNotification);
}

void MidiGuidePanel::paint(juce::Graphics& g)
{
    if (cardBounds.isEmpty())
        return;

    constexpr float radius = 8.0f;
    auto rect = cardBounds.toFloat();
    g.setColour(juce::Colour(0xFF171717u));
    g.fillRoundedRectangle(rect, radius);
    g.setColour(APP_COLOR_BORDER.withAlpha(0.4f));
    g.drawRoundedRectangle(rect.reduced(0.5f), radius, 0.75f);
}

void MidiGuidePanel::resized()
{
    auto card = getLocalBounds().reduced(kCardPadX, kCardPadY);
    card.setHeight(juce::jmin(card.getHeight(), kPreferredHeight - kCardPadY * 2));
    cardBounds = card;

    auto bounds = card.reduced(kInnerPadX, kInnerPadY);

    sectionLabel.setBounds(bounds.removeFromTop(kSectionLabelHeight));
    bounds.removeFromTop(kRowGap);

    if (guideTrackBox.isVisible())
    {
        guideTrackLabel.setBounds(bounds.removeFromTop(kLabelHeight));
        guideTrackBox.setBounds(bounds.removeFromTop(kRowHeight));
        bounds.removeFromTop(kRowGap);
    }

    loadButton.setBounds(bounds.removeFromTop(kButtonHeight));
    bounds.removeFromTop(kRowGap);
    fileLabel.setBounds(bounds.removeFromTop(kLabelHeight));
    bounds.removeFromTop(kRowGap);

    const auto labelled = [&bounds](juce::Label& label, juce::Component& control)
    {
        label.setBounds(bounds.removeFromTop(kLabelHeight));
        control.setBounds(bounds.removeFromTop(kRowHeight));
        bounds.removeFromTop(kRowGap);
    };

    labelled(trackLabel, trackBox);
    labelled(projectBpmLabel, projectBpmSlider);
    labelled(midiBpmLabel, midiBpmSlider);
    labelled(offsetLabel, offsetSlider);
    labelled(strengthLabel, strengthSlider);
    labelled(maxShiftLabel, maxShiftSlider);

    applyButton.setBounds(bounds.removeFromTop(kButtonHeight));
    bounds.removeFromTop(kRowGap);
    statusLabel.setBounds(bounds.removeFromTop(kStatusRowHeight));
}
