// Kinu Tune - Copyright (C) 2026 Keinoaza
// Part of Kinu Tune, a modified version of PitchNet. Licensed under the GNU
// Affero General Public License, version 3. See LICENSE and MODIFICATIONS.md.
#pragma once

#include "MidiGuideTypes.h"

#include <vector>

namespace correction
{

/// User-facing correction controls. Defaults favour a natural vocal over a
/// precise one, which is what the plug-in is for.
struct PitchTargetParams
{
    /// 0 = leave the pitch alone, 1 = move the note centre all the way to the
    /// guide. Partial values keep some of the singer's own intonation.
    float correctionStrength = 0.75f;

    /// How far from the guide a note may be and still be believed, in
    /// semitones. Beyond this the pairing is almost always a wrong match or a
    /// wrong note, and the note is left exactly as it was sung.
    ///
    /// This is a test on the match, not a clamp on the move: it is measured
    /// against the full distance to the guide, before strength scales it, so
    /// a low strength cannot smuggle a wrong match under the bar.
    ///
    /// A semitone is already generous for a tuning error. Singers land within
    /// a few tens of cents of what they meant; a note a whole tone out is a
    /// different note, and correcting towards it is how a correction comes
    /// back sounding processed.
    float maxShiftSemitones = 1.0f;

    /// Matches below this confidence are left uncorrected entirely.
    float minConfidence = 0.25f;

    /// At or above this, the match is believed and the user's strength
    /// decides the move on its own.
    ///
    /// Confidence used to scale the move across its whole range, which meant
    /// it quietly overrode the strength dial: alignment on real material
    /// averages around 0.8, so "correct all the way" moved a note four
    /// fifths of the way to the guide and left it a few cents out, every
    /// note, with no way to ask for more. Confidence is there to stop a
    /// doubtful match from confidently moving a note to the wrong pitch,
    /// which only needs it to act near the bottom of its range. Between
    /// minConfidence and this it ramps in; above it, strength is the dial.
    float trustedConfidence = 0.6f;

    /// Shifts smaller than this are not worth making, in semitones.
    ///
    /// A correction is not free: the note it touches gets re-rendered, and
    /// every engine costs something to pass through. The classic engine
    /// stretches the original waveform; the vocoder rebuilds the voice from
    /// an 80-band mel spectrogram, which cannot carry everything the
    /// recording held. Audio left alone keeps all of it, which is why
    /// Melodyne's untouched notes sound untouched.
    ///
    /// Three cents is comfortably under what can be heard on a sustained
    /// vowel - the difference limen for a complex tone in musical context is
    /// several times that - so this trades nothing audible for leaving a
    /// note that was already in tune exactly as it was sung. Without it a
    /// note two cents flat is "corrected" by a cent and a quarter and
    /// re-rendered for it, and correcting a whole take therefore re-renders
    /// the whole take whether or not it needed the help.
    float minShiftSemitones = 0.03f;

    /// Round the guide's note centre to the nearest semitone before aiming
    /// at it.
    ///
    /// A guide read from a MIDI file is already on exact semitones, so this
    /// changes nothing there. A guide read from another singer's track is
    /// not: its note centres carry that singer's own intonation, a few tens
    /// of cents sharp or flat, note by note. Aiming at them copies another
    /// person's tuning errors onto this take - the take is "corrected" to
    /// something that is itself out of tune - and the moves are larger than
    /// they need to be, so more notes clear the dead band and more of the
    /// take is re-rendered for it. The melody is what a sung guide is for;
    /// its tuning is not.
    bool snapGuideToSemitones = true;
};

/// What the correction decided for one sung note.
struct PitchTargetResult
{
    int sungIndex = -1;
    /// The note centre to write back into Note::midiNote. Equal to the
    /// original when nothing was applied.
    float correctedMidiNote = 60.0f;
    /// Signed semitones actually applied, after strength, confidence and the
    /// ceiling. Zero means the note was left alone.
    float appliedShiftSemitones = 0.0f;
    bool corrected = false;
};

/**
 * Decide the corrected note centre for every sung note.
 *
 * Only the note centre moves. deltaPitch - vibrato, scoops, falls, portamento
 * and the rest of the singer's phrasing - rides on top of it untouched, so a
 * corrected note keeps its shape and only changes where it sits.
 *
 * Correction is scaled by both the user's strength and the match confidence,
 * then clamped. A doubtful match therefore degrades to a gentle nudge rather
 * than a confident move to the wrong pitch.
 */
std::vector<PitchTargetResult> computePitchTargets(
    const std::vector<SungNote>& sung,
    const std::vector<GuideNote>& guide,
    const std::vector<NoteMatch>& matches,
    const PitchTargetParams& params = {});

} // namespace correction
