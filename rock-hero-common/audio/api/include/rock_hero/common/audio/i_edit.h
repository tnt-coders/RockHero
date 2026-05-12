/*!
\file i_edit.h
\brief Tracktion-free future edit-command port.
*/

#pragma once

namespace rock_hero::common::audio
{

/*!
\brief Future project-owned facade for undoable editor edit commands.

This placeholder is intentionally empty until the editor has real undoable/redoable commands for
chart, tone, automation, or arrangement mutation. Do not add project loading, audio preparation,
transport, playback setup, or other non-edit workflow operations here.
*/
class IEdit
{
public:
    /*! \brief Destroys the edit-command interface. */
    virtual ~IEdit() = default;

protected:
    /*! \brief Creates the edit-command interface. */
    IEdit() = default;

    /*! \brief Copies the edit-command interface. */
    IEdit(const IEdit&) = default;

    /*! \brief Moves the edit-command interface. */
    IEdit(IEdit&&) = default;

    /*!
    \brief Assigns the edit-command interface from another interface.
    \return Reference to this edit-command interface.
    */
    IEdit& operator=(const IEdit&) = default;

    /*!
    \brief Move-assigns the edit-command interface from another interface.
    \return Reference to this edit-command interface.
    */
    IEdit& operator=(IEdit&&) = default;
};

} // namespace rock_hero::common::audio
