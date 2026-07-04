/*!
\file grid_spacing_selector.h
\brief Timeline grid-size control with note-value presets and free fraction entry.
*/

#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <rock_hero/common/core/fraction.h>

namespace rock_hero::editor::ui
{

/*!
\brief A labeled combo box that selects the timeline grid size in note values.

Interaction is entirely listener-based; this widget deals only in note values (fractions of a
whole note), which are the product-wide grid unit, and forwards emitted selections through
Listener without conversion or bounds policy. Presets cover the power-of-two note values through
1/128, and the editable text box accepts free fractions such as 3/16 or 1/12. Text that does not
parse as a positive fraction reverts to the currently applied value without emitting; entries the
controller rejects revert the same way, because the applied value only changes through
setNoteValue.
*/
class GridSpacingSelector : public juce::Component
{
public:
    /*! \brief Receives note-value selections emitted by the combo box. */
    class Listener
    {
    public:
        /*! \brief Destroys the local listener interface. */
        virtual ~Listener() = default;

        /*!
        \brief Handles a chosen grid note value.
        \param note_value Selected note value as a fraction of a whole note.
        */
        virtual void onGridNoteValueChosen(common::core::Fraction note_value) = 0;

    protected:
        /*! \brief Creates the local listener interface. */
        Listener() = default;

        /*! \brief Copies the local listener interface. */
        Listener(const Listener&) = default;

        /*! \brief Moves the local listener interface. */
        Listener(Listener&&) = default;

        /*!
        \brief Assigns the local listener interface from another interface.
        \return Reference to this local listener interface.
        */
        Listener& operator=(const Listener&) = default;

        /*!
        \brief Move-assigns the local listener interface from another interface.
        \return Reference to this local listener interface.
        */
        Listener& operator=(Listener&&) = default;
    };

    /*!
    \brief Creates the grid caption and editable note-value combo box.
    \param listener Parent listener that receives chosen note values.
    */
    explicit GridSpacingSelector(Listener& listener);

    /*!
    \brief Shows the currently applied note value without emitting a selection.
    \param note_value Note value to display as a fraction of a whole note.
    */
    void setNoteValue(common::core::Fraction note_value);

    /*! \brief Lays out the caption and combo box within the component bounds. */
    void resized() override;

private:
    // Parses committed combo text and either emits a note value or reverts the display.
    void handleSelectionCommitted();

    // Rewrites the combo text from the applied note value without change notifications.
    void refreshDisplayedNoteValue();

    // Parent listener that owns note-value conversion and grid policy.
    Listener& m_listener;

    // Note value currently applied by the owner; the display reverts here on invalid entry.
    common::core::Fraction m_note_value{1, 4};

    // Static "Grid" caption drawn left of the combo box.
    juce::Label m_caption;

    // Editable combo with power-of-two presets and free fraction entry.
    juce::ComboBox m_note_value_box;
};

} // namespace rock_hero::editor::ui
