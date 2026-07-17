/*!
\file chart_selection.h
\brief Headless chart selection and caret state for the tablature editing surface.
*/

#pragma once

#include <compare>
#include <cstddef>
#include <rock_hero/common/core/chart/chart.h>
#include <vector>

namespace rock_hero::editor::core
{

/*!
\brief Stable identity of one chart note: its musical onset plus its string.

Unique by chart validation (one note per (position, string)) and stable across unrelated edits,
unlike projection indices, which shift whenever an earlier note is inserted or removed.
*/
struct ChartNoteKey
{
    /*! \brief Musical onset position. */
    common::core::GridPosition position{};

    /*! \brief One-based string, counted from the lowest-pitched string. */
    int string{1};

    /*!
    \brief Orders two note keys by (position, string), the chart's own note order.
    \param lhs Left-hand key.
    \param rhs Right-hand key.
    \return Ordering of lhs relative to rhs.
    */
    friend constexpr std::strong_ordering operator<=>(
        const ChartNoteKey& lhs, const ChartNoteKey& rhs) noexcept = default;

    /*!
    \brief Compares two note keys for equal value.
    \param lhs Left-hand key.
    \param rhs Right-hand key.
    \return True when both keys store equal values.
    */
    friend constexpr bool operator==(const ChartNoteKey& lhs, const ChartNoteKey& rhs) noexcept =
        default;
};

/*! \brief Editing caret: a grid-snapped musical position on one string lane. */
struct ChartCaret
{
    /*! \brief Musical position of the caret column. */
    common::core::GridPosition position{};

    /*! \brief One-based chart string lane the caret sits on. */
    int string{1};
};

/*!
\brief Selected chart notes plus the editing caret, kept sorted in chart note order.

The container enforces the sorted-unique invariant on every mutation so key-to-index resolution
against the chart's (position, string)-sorted note stream stays a linear merge.
*/
class ChartSelection
{
public:
    /*! \brief Removes every selected note. */
    void clear() noexcept;

    /*!
    \brief Replaces the whole selection with one note.
    \param key Note becoming the sole selection.
    */
    void replaceWith(const ChartNoteKey& key);

    /*!
    \brief Adds one note to the selection (Shift-extend); already-selected notes stay selected.
    \param key Note to add.
    */
    void add(const ChartNoteKey& key);

    /*!
    \brief Toggles one note's membership (Ctrl-click).
    \param key Note whose membership flips.
    */
    void toggle(const ChartNoteKey& key);

    /*!
    \brief Replaces or extends the selection with a batch of notes (marquee commit).
    \param keys Notes the marquee boxed, in any order.
    \param extend True to add to the existing selection instead of replacing it.
    */
    void applyBox(const std::vector<ChartNoteKey>& keys, bool extend);

    /*!
    \brief Reports whether a note is selected.
    \param key Note to look up.
    \return True when the note is in the selection.
    */
    [[nodiscard]] bool contains(const ChartNoteKey& key) const noexcept;

    /*!
    \brief The selected notes in ascending chart note order.
    \return Sorted unique selected note keys.
    */
    [[nodiscard]] const std::vector<ChartNoteKey>& notes() const noexcept;

    /*!
    \brief Reports whether nothing is selected.
    \return True when the selection is empty.
    */
    [[nodiscard]] bool empty() const noexcept;

private:
    // Sorted unique by (position, string); every mutation preserves the invariant.
    std::vector<ChartNoteKey> m_notes{};
};

/*!
\brief Resolves selected note keys to indices in the chart's sorted note stream.

Chart notes are sorted by (position, string) and the tab projection preserves that order one to
one, so the returned indices address the projection's notes directly. Keys that no longer match
a chart note resolve to nothing and are skipped.

\param notes Chart note stream sorted by (position, string).
\param selection Selection whose keys are resolved.
\return Ascending projection indices of the selected notes that still exist.
*/
[[nodiscard]] std::vector<std::size_t> selectedNoteIndices(
    const std::vector<common::core::ChartNote>& notes, const ChartSelection& selection);

} // namespace rock_hero::editor::core
