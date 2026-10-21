// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.

#include "TimingCorrectionPanel.h"

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
constexpr int kToggleHeight = 22;
constexpr int kButtonHeight = 24;
constexpr int kStatusRowHeight = 30;

constexpr int kLabelledRow = kLabelHeight + kRowHeight;
constexpr int kPreferredHeight =
    kCardPadY * 2 + kInnerPadY * 2 + kSectionLabelHeight + kRowGap +
    (kLabelledRow + kRowGap) * 2 +   // mode, grid
    (kLabelledRow + kRowGap) * 3 +   // strength, max shift, tolerance
    (kToggleHeight + kRowGap) * 2 +  // note length, anchor
    kButtonHeight + kRowGap + kStatusRowHeight;

void styleCaption(juce::Label& label)
{
    label.setFont(AppFont::getFont(12.0f));
    label.setColour(juce::Label::textColourId, APP_COLOR_TEXT_MUTED);
}

// Grid menu ids carry the divisions per beat, so the menu is the setting.
constexpr int kDivisionsForId[] = {0, 1, 2, 4, 8};
} // namespace

TimingCorrectionPanel::TimingCorrectionPanel()
{
    sectionLabel.setFont(AppFont::getBoldFont(16.0f));
    sectionLabel.setColour(juce::Label::textColourId, juce::Colour(0xFF9B9B9Bu));
    addAndMakeVisible(sectionLabel);

    styleCaption(modeLabel);
    modeBox.onChange = [this] { refreshEnablement(); };
    addAndMakeVisible(modeLabel);
    addAndMakeVisible(modeBox);

    styleCaption(gridLabel);
    addAndMakeVisible(gridLabel);
    addAndMakeVisible(gridBox);

    // Timing correction stretches audio, which is what costs quality, so it
    // starts gentler than the pitch correction does.
    styleCaption(strengthLabel);
    strengthSlider.setRange(0.0, 1.0, 0.01);
    strengthSlider.setValue(0.5, juce::dontSendNotification);
    addAndMakeVisible(strengthLabel);
    addAndMakeVisible(strengthSlider);

    styleCaption(maxShiftLabel);
    maxShiftSlider.setRange(0.0, 1.0, 0.005);
    maxShiftSlider.setValue(0.15, juce::dontSendNotification);
    addAndMakeVisible(maxShiftLabel);
    addAndMakeVisible(maxShiftSlider);

    // In frames at the analysis rate: about 12 ms each, so the default is
    // roughly 35 ms, below what anyone hears as early or late.
    styleCaption(toleranceLabel);
    toleranceSlider.setRange(0.0, 20.0, 0.5);
    toleranceSlider.setValue(3.0, juce::dontSendNotification);
    addAndMakeVisible(toleranceLabel);
    addAndMakeVisible(toleranceSlider);

    lengthToggle.setToggleState(true, juce::dontSendNotification);
    addAndMakeVisible(lengthToggle);

    anchorToggle.setToggleState(true, juce::dontSendNotification);
    addAndMakeVisible(anchorToggle);

    applyButton.setColour(juce::TextButton::buttonColourId, APP_COLOR_SURFACE_ALT);
    applyButton.setColour(juce::TextButton::buttonOnColourId,
                          APP_COLOR_SURFACE_RAISED);
    applyButton.setColour(juce::TextButton::textColourOffId,
                          APP_COLOR_TEXT_PRIMARY);
    applyButton.setColour(juce::TextButton::textColourOnId,
                          APP_COLOR_TEXT_PRIMARY);
    applyButton.onClick = [this] { runCorrection(); };
    addAndMakeVisible(applyButton);

    statusLabel.setFont(AppFont::getFont(12.0f));
    statusLabel.setColour(juce::Label::textColourId, APP_COLOR_TEXT_MUTED);
    statusLabel.setJustificationType(juce::Justification::topLeft);
    addAndMakeVisible(statusLabel);

    refreshLocalisedText();
    refreshEnablement();
    startTimerHz(4);
}

TimingCorrectionPanel::~TimingCorrectionPanel()
{
    stopTimer();
}

int TimingCorrectionPanel::getPreferredHeight() const
{
    return kPreferredHeight;
}

double TimingCorrectionPanel::framesPerSecond() const
{
    if (getFramesPerSecond)
    {
        const double fps = getFramesPerSecond();
        if (fps > 0.0)
            return fps;
    }
    return static_cast<double>(SAMPLE_RATE) / static_cast<double>(HOP_SIZE);
}

correction::TimingMode TimingCorrectionPanel::selectedMode() const
{
    return modeBox.getSelectedId() == 2 ? correction::TimingMode::Grid
                                        : correction::TimingMode::Guide;
}

void TimingCorrectionPanel::refreshLocalisedText()
{
    sectionLabel.setText(TR("timing.title"), juce::dontSendNotification);
    modeLabel.setText(TR("timing.mode"), juce::dontSendNotification);
    gridLabel.setText(TR("timing.grid"), juce::dontSendNotification);
    strengthLabel.setText(TR("timing.strength"), juce::dontSendNotification);
    maxShiftLabel.setText(TR("timing.max_shift"), juce::dontSendNotification);
    toleranceLabel.setText(TR("timing.tolerance"), juce::dontSendNotification);
    lengthToggle.setButtonText(TR("timing.note_length"));
    anchorToggle.setButtonText(TR("timing.anchor_peak"));
    applyButton.setButtonText(TR("timing.apply"));

    const int mode = modeBox.getSelectedId();
    modeBox.clear(juce::dontSendNotification);
    modeBox.addItem(TR("timing.mode_guide"), 1);
    modeBox.addItem(TR("timing.mode_grid"), 2);
    modeBox.setSelectedId(mode > 0 ? mode : 1, juce::dontSendNotification);

    const int grid = gridBox.getSelectedId();
    gridBox.clear(juce::dontSendNotification);
    gridBox.addItem(TR("timing.grid_quarter"), 1);
    gridBox.addItem(TR("timing.grid_eighth"), 2);
    gridBox.addItem(TR("timing.grid_sixteenth"), 3);
    gridBox.addItem(TR("timing.grid_thirtysecond"), 4);
    gridBox.setSelectedId(grid > 0 ? grid : 3, juce::dontSendNotification);
}

void TimingCorrectionPanel::timerCallback()
{
    const bool hasProject = currentProject() != nullptr;
    if (hasProject != hadProject)
    {
        hadProject = hasProject;
        refreshEnablement();
    }
}

void TimingCorrectionPanel::refreshEnablement()
{
    const bool grid = selectedMode() == correction::TimingMode::Grid;
    const bool haveGuide = getGuideNotes && !getGuideNotes().empty();
    const bool hostTempo = getHostBpm && getHostBpm() > 0.0;

    gridLabel.setEnabled(grid);
    gridBox.setEnabled(grid);
    toleranceLabel.setEnabled(grid);
    toleranceSlider.setEnabled(grid);

    const bool ready =
        currentProject() != nullptr && (grid ? hostTempo : haveGuide);
    applyButton.setEnabled(ready);

    if (!ready && currentProject() != nullptr)
        showStatus(grid ? TR("timing.status_no_tempo")
                        : TR("timing.status_no_guide"),
                   true);
}

void TimingCorrectionPanel::runCorrection()
{
    if (!onCorrectionRequested)
        return;

    correction::TimingCorrectionParams params;
    params.mode = selectedMode();
    params.anchor = anchorToggle.getToggleState()
                        ? correction::TimingAnchor::EnergyPeak
                        : correction::TimingAnchor::NoteStart;

    params.timing.framesPerSecond = framesPerSecond();
    params.timing.correctionStrength =
        static_cast<float>(strengthSlider.getValue());
    params.timing.maxShiftSeconds =
        static_cast<float>(maxShiftSlider.getValue());
    params.timing.correctNoteLength = lengthToggle.getToggleState();

    const int gridId = gridBox.getSelectedId();
    params.grid.framesPerSecond = framesPerSecond();
    params.grid.bpm = getHostBpm ? getHostBpm() : 0.0;
    params.grid.divisionsPerBeat =
        gridId >= 1 && gridId <= 4 ? kDivisionsForId[gridId] : 4;
    params.gridToleranceFrames = toleranceSlider.getValue();

    const auto guide = getGuideNotes ? getGuideNotes()
                                     : std::vector<correction::GuideNote>{};

    const auto report = onCorrectionRequested(guide, params);

    if (report.notesConsidered == 0)
    {
        showStatus(TR("timing.status_analyse_first"), true);
        return;
    }

    showStatus(TR("timing.status_done")
                   .replace("{moved}", juce::String(report.notesMoved))
                   .replace("{total}", juce::String(report.notesConsidered))
                   .replace("{shift}",
                            juce::String(report.largestShiftFrames /
                                             framesPerSecond() * 1000.0,
                                         0)),
               report.notesMoved == 0);
}

void TimingCorrectionPanel::showStatus(const juce::String& text, bool isWarning)
{
    statusLabel.setColour(juce::Label::textColourId,
                          isWarning ? APP_COLOR_ALERT_WARNING
                                    : APP_COLOR_TEXT_MUTED);
    statusLabel.setText(text, juce::dontSendNotification);
}

void TimingCorrectionPanel::paint(juce::Graphics& g)
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

void TimingCorrectionPanel::resized()
{
    auto card = getLocalBounds().reduced(kCardPadX, kCardPadY);
    card.setHeight(juce::jmin(card.getHeight(), kPreferredHeight - kCardPadY * 2));
    cardBounds = card;

    auto bounds = card.reduced(kInnerPadX, kInnerPadY);

    sectionLabel.setBounds(bounds.removeFromTop(kSectionLabelHeight));
    bounds.removeFromTop(kRowGap);

    const auto labelled = [&bounds](juce::Label& label, juce::Component& control)
    {
        label.setBounds(bounds.removeFromTop(kLabelHeight));
        control.setBounds(bounds.removeFromTop(kRowHeight));
        bounds.removeFromTop(kRowGap);
    };

    labelled(modeLabel, modeBox);
    labelled(gridLabel, gridBox);
    labelled(strengthLabel, strengthSlider);
    labelled(maxShiftLabel, maxShiftSlider);
    labelled(toleranceLabel, toleranceSlider);

    lengthToggle.setBounds(bounds.removeFromTop(kToggleHeight));
    bounds.removeFromTop(kRowGap);
    anchorToggle.setBounds(bounds.removeFromTop(kToggleHeight));
    bounds.removeFromTop(kRowGap);

    applyButton.setBounds(bounds.removeFromTop(kButtonHeight));
    bounds.removeFromTop(kRowGap);
    statusLabel.setBounds(bounds.removeFromTop(kStatusRowHeight));
}
