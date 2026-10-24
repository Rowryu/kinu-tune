#include "TimingHandler.h"

#include "../../PianoRollComponent.h"
#include "../../../Utils/Constants.h"
#include "../../../Utils/TimingRegionUtils.h"
#include "../../../Utils/UI/Theme.h"
#include "../../Components/AppFont.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

TimingHandler::TimingHandler(PianoRollComponent& owner)
    : InteractionHandler(owner) {}

std::vector<TimingHandler::Boundary> TimingHandler::buildBoundaries() const
{
  std::vector<Note*> notes;
  if (!owner_.project)
    return {};

  for (auto& note : owner_.project->getNotes())
    if (!note.isRest())
      notes.push_back(&note);
  std::sort(notes.begin(), notes.end(), [](const Note* a, const Note* b)
  {
    return a->getStartFrame() < b->getStartFrame();
  });

  std::vector<Boundary> result;
  auto addEdge = [&](float frame, Note* left, Note* right)
  {
    auto existing = std::find_if(result.begin(), result.end(),
                                 [frame](const Boundary& b)
                                 { return std::abs(b.frame - frame) < 0.001f; });
    if (existing == result.end())
    {
      result.push_back({frame, left, right, nullptr, 0.0f, 0.0f});
      return;
    }
    if (left)
      existing->left = left;
    if (right)
      existing->right = right;
  };

  for (auto* note : notes)
  {
    addEdge(note->getVisualStartFrame(), nullptr, note);
    addEdge(note->getVisualEndFrame(), note, nullptr);
  }

  for (auto& boundary : result)
  {
    std::vector<Note*> drawnNotes;
    if (boundary.left)
      drawnNotes.push_back(boundary.left);
    if (boundary.right)
      drawnNotes.push_back(boundary.right);

    if (!boundary.left && boundary.right)
    {
      for (auto* note : notes)
        if (note->getVisualEndFrame() < boundary.frame &&
            (!boundary.restCompanion ||
             note->getVisualEndFrame() >
                 boundary.restCompanion->getVisualEndFrame()))
          boundary.restCompanion = note;
    }
    else if (boundary.left && !boundary.right)
    {
      for (auto* note : notes)
        if (note->getVisualStartFrame() > boundary.frame &&
            (!boundary.restCompanion ||
             note->getVisualStartFrame() <
                 boundary.restCompanion->getVisualStartFrame()))
          boundary.restCompanion = note;
    }

    float top = std::numeric_limits<float>::max();
    float bottom = std::numeric_limits<float>::lowest();
    for (auto* note : drawnNotes)
    {
      const float y = owner_.midiToY(note->getAdjustedMidiNote());
      top = std::min(top, y);
      bottom = std::max(bottom, y + owner_.pixelsPerSemitone);
    }
    boundary.top = top;
    boundary.bottom = bottom;
  }

  std::sort(result.begin(), result.end(), [](const Boundary& a,
                                             const Boundary& b)
  { return a.frame < b.frame; });
  return result;
}

int TimingHandler::findBoundary(float worldX, float,
                                const std::vector<Boundary>& boundaries) const
{
  constexpr float hitPadding = 6.0f;
  int best = -1;
  float bestDistance = hitPadding + 1.0f;
  for (size_t i = 0; i < boundaries.size(); ++i)
  {
    const auto& boundary = boundaries[i];
    const float x = static_cast<float>(framesToSeconds(boundary.frame) *
                                       owner_.pixelsPerSecond);
    const float distance = std::abs(worldX - x);
    if (distance <= hitPadding && distance < bestDistance)
    {
      best = static_cast<int>(i);
      bestDistance = distance;
    }
  }
  return best;
}

std::vector<Note*> TimingHandler::collectAffectedNotes(
    const Boundary& boundary) const
{
  std::vector<Note*> notes;
  for (auto* note : {boundary.left, boundary.right, boundary.restCompanion})
    if (note && std::find(notes.begin(), notes.end(), note) == notes.end())
      notes.push_back(note);
  return notes;
}

std::vector<Note*> TimingHandler::collectAffectedNotes(
    const std::vector<Boundary>& boundaries) const
{
  std::vector<Note*> notes;
  for (const auto& boundary : boundaries)
    for (auto* note : collectAffectedNotes(boundary))
      if (std::find(notes.begin(), notes.end(), note) == notes.end())
        notes.push_back(note);
  return notes;
}

std::vector<NoteTimingState> TimingHandler::captureAffected(
    const std::vector<Boundary>& boundaries) const
{
  std::vector<NoteTimingState> states;
  for (auto* note : collectAffectedNotes(boundaries))
    states.push_back(NoteTimingState::capture(*note));
  return states;
}

std::pair<float, float> TimingHandler::getGroupDeltaRange(
    const std::vector<Boundary>& boundaries) const
{
  if (!owner_.project || boundaries.empty())
    return {0.0f, 0.0f};

  const auto movesStart = [&](const Note* note)
  {
    return std::any_of(boundaries.begin(), boundaries.end(),
                       [note](const Boundary& boundary)
                       { return boundary.right == note; });
  };
  const auto movesEnd = [&](const Note* note)
  {
    return std::any_of(boundaries.begin(), boundaries.end(),
                       [note](const Boundary& boundary)
                       { return boundary.left == note; });
  };

  const float frameCount = static_cast<float>(
      owner_.project->getAudioData().getNumFrames());
  float minimum = std::numeric_limits<float>::lowest();
  float maximum = std::numeric_limits<float>::max();

  for (const auto& boundary : boundaries)
  {
    minimum = std::max(minimum, -boundary.frame);
    maximum = std::min(maximum, frameCount - boundary.frame);

    if (boundary.left && !movesStart(boundary.left))
      minimum = std::max(
          minimum,
          boundary.left->getVisualStartFrame() + 1.0f - boundary.frame);
    if (boundary.right && !movesEnd(boundary.right))
      maximum = std::min(
          maximum,
          boundary.right->getVisualEndFrame() - 1.0f - boundary.frame);

    // A boundary next to a rest may approach the neighboring region/note, but
    // never cross it or remove the required one-frame rest. If both sides are
    // selected, their shared translation preserves the rest length.
    if (!boundary.left && boundary.restCompanion &&
        !movesEnd(boundary.restCompanion))
      minimum = std::max(
          minimum,
          boundary.restCompanion->getVisualEndFrame() + 1.0f -
              boundary.frame);
    if (!boundary.right && boundary.restCompanion &&
        !movesStart(boundary.restCompanion))
      maximum = std::min(
          maximum,
          boundary.restCompanion->getVisualStartFrame() - 1.0f -
              boundary.frame);

    // The unpitched audio attached to a region's first/last note moves without
    // changing length. Constraints between two selected regions cancel out;
    // only the project edge or an unselected neighboring region limits them.
    if (boundary.right &&
        timingRegions::isFirstNote(*owner_.project, *boundary.right))
    {
      const auto region =
          timingRegions::getSourceRegion(*owner_.project, *boundary.right);
      const float leadLength = static_cast<float>(
          boundary.right->getSrcStartFrame() - region.start);
      float earliestRegionStart = 0.0f;
      if (region.index > 0)
      {
        const auto previousRegion =
            timingRegions::regionAt(*owner_.project, region.index - 1);
        const auto* previousLast =
            timingRegions::lastNote(*owner_.project, previousRegion);
        if (!previousLast || !movesEnd(previousLast))
          earliestRegionStart =
              timingRegions::visualEnd(*owner_.project, previousRegion) +
              1.0f;
      }
      minimum = std::max(
          minimum,
          earliestRegionStart + leadLength - boundary.frame);
    }

    if (boundary.left &&
        timingRegions::isLastNote(*owner_.project, *boundary.left))
    {
      const auto region =
          timingRegions::getSourceRegion(*owner_.project, *boundary.left);
      const float tailLength = static_cast<float>(
          region.end - boundary.left->getSrcEndFrame());
      float latestRegionEnd = frameCount;
      if (region.index + 1 < timingRegions::regionCount(*owner_.project))
      {
        const auto nextRegion =
            timingRegions::regionAt(*owner_.project, region.index + 1);
        const auto* nextFirst =
            timingRegions::firstNote(*owner_.project, nextRegion);
        if (!nextFirst || !movesStart(nextFirst))
          latestRegionEnd =
              timingRegions::visualStart(*owner_.project, nextRegion) -
              1.0f;
      }
      maximum = std::min(
          maximum,
          latestRegionEnd - tailLength - boundary.frame);
    }
  }

  return minimum <= maximum ? std::pair<float, float>{minimum, maximum}
                            : std::pair<float, float>{0.0f, 0.0f};
}

void TimingHandler::applyPreviewDelta(float delta)
{
  previewDelta = std::clamp(delta, minimumDelta, maximumDelta);
  for (const auto& boundary : dragBoundaries)
  {
    const float frame = boundary.frame + previewDelta;
    if (boundary.left)
      boundary.left->setTimingPreviewEndFrame(frame);
    if (boundary.right)
      boundary.right->setTimingPreviewStartFrame(frame);
  }
  owner_.repaint();
}

void TimingHandler::clearAffectedPreviews()
{
  for (auto* note : collectAffectedNotes(dragBoundaries))
    note->clearTimingPreview();
}

TimingHandler::BoundaryKey TimingHandler::keyFor(const Boundary& boundary)
{
  return {boundary.left, boundary.right};
}

bool TimingHandler::keysMatch(const BoundaryKey& a, const BoundaryKey& b)
{
  return a.left == b.left && a.right == b.right;
}

bool TimingHandler::beginMove(Note* note, float worldX)
{
  const auto boundaries = buildBoundaries();
  selectedBoundaries.clear();
  int startIndex = -1;
  for (size_t i = 0; i < boundaries.size(); ++i)
  {
    if (boundaries[i].right == note || boundaries[i].left == note)
    {
      addSelected(boundaries[i]);
      if (boundaries[i].right == note)
        startIndex = static_cast<int>(i);
    }
  }
  if (startIndex < 0 || selectedBoundaries.size() < 2)
  {
    selectedBoundaries.clear();
    return false;
  }
  activeBoundary = boundaries[static_cast<size_t>(startIndex)];
  dragBoundaries = resolveSelection(boundaries);
  before = captureAffected(dragBoundaries);
  const auto deltaRange = getGroupDeltaRange(dragBoundaries);
  minimumDelta = deltaRange.first;
  maximumDelta = deltaRange.second;
  previewDelta = 0.0f;
  dragAnchorFrame = worldX / std::max(1.0f, owner_.pixelsPerSecond) *
                    SAMPLE_RATE / static_cast<float>(HOP_SIZE);
  dragging = true;
  return true;
}

bool TimingHandler::isSelected(const Boundary& boundary) const
{
  const auto key = keyFor(boundary);
  return std::any_of(selectedBoundaries.begin(), selectedBoundaries.end(),
                     [&](const BoundaryKey& selected)
                     { return keysMatch(selected, key); });
}

void TimingHandler::addSelected(const Boundary& boundary)
{
  if (!isSelected(boundary))
    selectedBoundaries.push_back(keyFor(boundary));
}

void TimingHandler::removeSelected(const Boundary& boundary)
{
  const auto key = keyFor(boundary);
  selectedBoundaries.erase(
      std::remove_if(selectedBoundaries.begin(), selectedBoundaries.end(),
                     [&](const BoundaryKey& selected)
                     { return keysMatch(selected, key); }),
      selectedBoundaries.end());
}

std::vector<TimingHandler::Boundary> TimingHandler::resolveSelection(
    const std::vector<Boundary>& boundaries) const
{
  std::vector<Boundary> result;
  for (const auto& boundary : boundaries)
    if (isSelected(boundary))
      result.push_back(boundary);
  return result;
}

void TimingHandler::updateMarqueeSelection(float worldX)
{
  selectionCurrentX = worldX;
  selectedBoundaries = selectionBaseline;
  const float left = std::min(selectionStartX, selectionCurrentX);
  const float right = std::max(selectionStartX, selectionCurrentX);
  for (const auto& boundary : buildBoundaries())
  {
    const float x = static_cast<float>(framesToSeconds(boundary.frame) *
                                       owner_.pixelsPerSecond);
    if (x >= left && x <= right)
      addSelected(boundary);
  }
  owner_.repaint();
}

bool TimingHandler::mouseDown(const juce::MouseEvent& e, float worldX,
                              float worldY)
{
  if (!owner_.isCanvasPoint(e))
    return false;

  const auto boundaries = buildBoundaries();
  const int index = findBoundary(worldX, worldY, boundaries);
  if (index < 0 && !e.mods.isShiftDown())
  {
    if (auto* note = owner_.findNoteAt(worldX, worldY))
    {
      bodyNote = note;
      bodyDragStartX = worldX;
      pitchDragStartY = worldY;
      pitchDragSemitones = 0;
      return true;
    }
  }
  if (index < 0)
  {
    selecting = true;
    selectionStartX = selectionCurrentX = worldX;
    selectionBaseline = e.mods.isShiftDown()
                            ? selectedBoundaries
                            : std::vector<BoundaryKey>{};
    selectedBoundaries = selectionBaseline;
    owner_.repaint();
    return true;
  }

  activeBoundary = boundaries[static_cast<size_t>(index)];
  if (e.mods.isShiftDown() && isSelected(activeBoundary))
  {
    removeSelected(activeBoundary);
    owner_.repaint();
    return true;
  }

  if (!e.mods.isShiftDown() && !isSelected(activeBoundary))
    selectedBoundaries.clear();
  addSelected(activeBoundary);
  dragBoundaries = resolveSelection(boundaries);
  before = captureAffected(dragBoundaries);
  const auto deltaRange = getGroupDeltaRange(dragBoundaries);
  minimumDelta = deltaRange.first;
  maximumDelta = deltaRange.second;
  previewDelta = 0.0f;
  dragAnchorFrame = activeBoundary.frame;
  dragging = true;
  owner_.repaint();
  return true;
}

bool TimingHandler::mouseDrag(const juce::MouseEvent& e, float worldX, float worldY)
{
  if (bodyNote)
  {
    const float dx = worldX - bodyDragStartX;
    const float dy = worldY - pitchDragStartY;
    if (std::max(std::abs(dx), std::abs(dy)) < 4.0f)
      return true;
    auto* note = bodyNote;
    bodyNote = nullptr;
    if (std::abs(dx) > std::abs(dy))
    {
      if (!beginMove(note, bodyDragStartX))
        return true;
    }
    else
      pitchNote = note;
  }
  if (pitchNote)
  {
    const int semitones = static_cast<int>(std::lround(
        (pitchDragStartY - worldY) / std::max(1.0f, owner_.pixelsPerSemitone)));
    if (semitones != pitchDragSemitones)
    {
      pitchDragSemitones = semitones;
      owner_.repaint();
    }
    return true;
  }
  if (selecting)
  {
    updateMarqueeSelection(worldX);
    return true;
  }
  if (!dragging || !owner_.project)
    return false;

  const float pps = std::max(1.0f, owner_.pixelsPerSecond);
  const float framesPerSecond = SAMPLE_RATE / static_cast<float>(HOP_SIZE);
  const float frame = worldX / pps * framesPerSecond;
  float delta = frame - dragAnchorFrame;

  // Like a warp marker with snap on: the dragged edge (or the moved note's
  // start) lands on the timeline grid when grid snapping is enabled. Holding
  // Shift while dragging places it freely, for the cases that have to be
  // placed by ear. Not Alt: Alt-click splits a note and Alt-drag on the
  // canvas is vertical zoom, and both are taken before this handler runs.
  if (!e.mods.isShiftDown())
  {
    const float edgeX = (activeBoundary.frame + delta) / framesPerSecond * pps;
    const double snappedTime =
        owner_.snapTimeToTimelineGrid(owner_.xToTime(edgeX));
    const float snappedX = owner_.timeToX(snappedTime);
    delta = snappedX / pps * framesPerSecond - activeBoundary.frame;
  }
  applyPreviewDelta(delta);
  if (owner_.onPitchEdited)
    owner_.onPitchEdited();
  return true;
}

bool TimingHandler::mouseUp(const juce::MouseEvent&, float, float)
{
  if (bodyNote)
  {
    bodyNote = nullptr; // a click on a body without a drag does nothing
    return true;
  }
  if (pitchNote)
  {
    auto* note = pitchNote;
    const int semitones = pitchDragSemitones;
    pitchNote = nullptr;
    pitchDragSemitones = 0;
    if (semitones != 0)
      owner_.shiftNotesBySemitones(
          {note}, [semitones](const Note&) -> std::optional<float> {
            return static_cast<float>(semitones);
          });
    owner_.repaint();
    return true;
  }
  if (selecting)
  {
    selecting = false;
    selectionBaseline.clear();
    owner_.repaint();
    return true;
  }
  if (!dragging || !owner_.project)
    return false;

  dragging = false;
  const float snappedDelta = std::clamp(
      static_cast<float>(std::lround(previewDelta)), minimumDelta,
      maximumDelta);
  clearAffectedPreviews();
  for (const auto& boundary : dragBoundaries)
  {
    const int committedFrame = static_cast<int>(
        std::lround(boundary.frame + snappedDelta));
    if (boundary.left)
      boundary.left->setEndFrame(committedFrame);
    if (boundary.right)
      boundary.right->setStartFrame(committedFrame);
  }
  auto after = captureAffected(dragBoundaries);
  bool changed = before.size() == after.size();
  if (changed)
  {
    changed = false;
    for (size_t i = 0; i < before.size(); ++i)
      if (before[i].startFrame != after[i].startFrame ||
          before[i].endFrame != after[i].endFrame)
      {
        changed = true;
        break;
      }
  }

  if (changed)
  {
    // Preview commits stage the snapped frames directly on the notes. Restore
    // the captured state first so applyNoteTimingStates can distinguish notes
    // that actually moved from unchanged rest companions. Otherwise a prior
    // inward region edit can clear audio written by this edit.
    for (const auto& state : before)
    {
      if (!state.note)
        continue;
      state.note->setStartFrame(state.startFrame);
      state.note->setEndFrame(state.endFrame);
    }
    applyNoteTimingStates(*owner_.project, after);
    owner_.invalidateBasePitchCache();
    if (owner_.undoManager)
      owner_.undoManager->addAction(std::make_unique<TimingAction>(
          *owner_.project, before, after));
  }
  if (changed && owner_.onPitchEditFinished)
    owner_.onPitchEditFinished();
  before.clear();
  dragBoundaries.clear();
  owner_.repaint();
  return true;
}

void TimingHandler::mouseMove(const juce::MouseEvent& e, float worldX,
                              float worldY)
{
  if (!owner_.isCanvasPoint(e))
  {
    if (hoveredBoundaryFrame >= 0.0f)
    {
      hoveredBoundaryFrame = -1.0f;
      owner_.repaint();
    }
    owner_.setMouseCursor(juce::MouseCursor::NormalCursor);
    return;
  }

  const auto boundaries = buildBoundaries();
  const int index = findBoundary(worldX, worldY, boundaries);
  const bool overBody = index < 0 && owner_.findNoteAt(worldX, worldY) != nullptr;
  const float nextHover = index >= 0
                              ? boundaries[static_cast<size_t>(index)].frame
                              : -1.0f;
  if (std::abs(nextHover - hoveredBoundaryFrame) > 0.001f)
  {
    hoveredBoundaryFrame = nextHover;
    owner_.repaint();
  }
  owner_.setMouseCursor(index >= 0 ? juce::MouseCursor::LeftRightResizeCursor
                        : overBody  ? juce::MouseCursor::DraggingHandCursor
                                    : juce::MouseCursor::NormalCursor);
}

void TimingHandler::draw(juce::Graphics& g)
{
  const float canvasHeight =
      (MAX_MIDI_NOTE - MIN_MIDI_NOTE + 1) * owner_.pixelsPerSemitone;
  for (const auto& boundary : buildBoundaries())
  {
    const float x = static_cast<float>(framesToSeconds(boundary.frame) *
                                       owner_.pixelsPerSecond);
    const bool selected = isSelected(boundary);
    const bool hovered = std::abs(boundary.frame -
                                  hoveredBoundaryFrame) < 0.001f;
    const bool highlighted = hovered || (dragging && selected);
    g.setColour(highlighted
                    ? juce::Colour(0xFFFFFFFFu)
                    : selected ? juce::Colour(0xFFFFFFFFu)
                               : APP_COLOR_TEXT_PRIMARY.withAlpha(0.72f));
    const float width = highlighted || selected ? 2.0f : 1.0f;
    g.fillRect(x - width * 0.5f, 0.0f, width, canvasHeight);
  }

  if (pitchNote && pitchDragSemitones != 0)
  {
    const float left = static_cast<float>(
        framesToSeconds(pitchNote->getStartFrame()) * owner_.pixelsPerSecond);
    const float right = static_cast<float>(
        framesToSeconds(pitchNote->getEndFrame()) * owner_.pixelsPerSecond);
    const float top = owner_.midiToY(pitchNote->getAdjustedMidiNote() +
                                     static_cast<float>(pitchDragSemitones));
    g.setColour(juce::Colours::white.withAlpha(0.18f));
    g.fillRect(left, top, std::max(2.0f, right - left), owner_.pixelsPerSemitone);
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.drawRect(left, top, std::max(2.0f, right - left), owner_.pixelsPerSemitone, 1.0f);
    g.setFont(AppFont::getFont(12));
    g.drawText((pitchDragSemitones > 0 ? "+" : "") + juce::String(pitchDragSemitones),
               juce::Rectangle<float>(left, top - 16.0f, 48.0f, 14.0f),
               juce::Justification::centredLeft);
  }

  if (selecting)
  {
    const float left = std::min(selectionStartX, selectionCurrentX);
    const float width = std::abs(selectionCurrentX - selectionStartX);
    g.setColour(juce::Colours::white.withAlpha(0.12f));
    g.fillRect(left, 0.0f, width, canvasHeight);
    g.setColour(juce::Colours::white.withAlpha(0.75f));
    g.drawRect(left, 0.0f, width, canvasHeight, 1.0f);
  }
}

void TimingHandler::cancel()
{
  if (dragging && owner_.project)
    clearAffectedPreviews();
  dragging = false;
  selecting = false;
  pitchNote = nullptr;
  bodyNote = nullptr;
  pitchDragSemitones = 0;
  before.clear();
  dragBoundaries.clear();
  selectedBoundaries.clear();
  selectionBaseline.clear();
  hoveredBoundaryFrame = -1.0f;
  owner_.repaint();
}
