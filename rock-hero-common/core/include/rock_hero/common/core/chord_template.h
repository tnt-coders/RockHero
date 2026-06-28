/*!
\file chord_template.h
\brief Reusable chord voicing referenced by chord events.
*/

#pragma once

#include <optional>
#include <string>
#include <vector>

namespace rock_hero::common::core
{

/*! \brief One struck string of a chord voicing. */
struct ChordVoicingString
{
    /*! \brief One-based string number; one is the highest-pitched string. */
    int string_number{0};

    /*! \brief Fret number; zero means an open string. */
    int fret{0};

    /*! \brief Fretting-hand finger (1-4); absent for open strings or unspecified fingering. */
    std::optional<int> finger;

    /*!
    \brief Compares two voicing strings by their stored fields.
    \param lhs Left-hand voicing string.
    \param rhs Right-hand voicing string.
    \return True when both store equal string, fret, and finger.
    */
    friend bool operator==(const ChordVoicingString& lhs, const ChordVoicingString& rhs) = default;
};

/*!
\brief A named, fingered chord voicing reused across chord events.

A ChordTemplate carries the full identity of a chord: its display name, the strings and frets it
sounds, and the fingering. Two templates are distinct when name, frets, or fingering differ, so the
same frets under two names (Am7 and C6) or two fingerings of one shape are separate templates. Chord
events reference a template by id; the template, not the event, owns the frets and fingering, so a
chord's pitches are never duplicated at the event. The id is the chord name, suffixed with -1/-2
ordinals when two templates in one arrangement share a name, and is regenerated whenever the
arrangement document is written.
*/
struct ChordTemplate
{
    /*! \brief Reference id: the chord name, with an ordinal suffix on name collision. */
    std::string id;

    /*! \brief Non-empty display name shown for the chord and used to generate the reference id. */
    std::string name;

    /*! \brief Struck strings of the voicing. */
    std::vector<ChordVoicingString> voicing;

    /*!
    \brief Compares two chord templates by their stored fields.
    \param lhs Left-hand chord template.
    \param rhs Right-hand chord template.
    \return True when both store equal id, name, and voicing.
    */
    friend bool operator==(const ChordTemplate& lhs, const ChordTemplate& rhs) = default;
};

} // namespace rock_hero::common::core
