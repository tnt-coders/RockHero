/*!
\file tone_designer_view_state.h
\brief Framework-free Tone Designer document state rendered by the signal-chain panel header.
*/

#pragma once

#include <filesystem>
#include <string>

namespace rock_hero::editor::core
{

/*! \brief Tone Designer document state for the signal-chain panel's header and button strip. */
struct ToneDesignerViewState
{
    /*! \brief True while the Tone Designer owns the live rig (no project open). */
    bool active{false};

    /*! \brief Document display name: the tone file's stem, or "Untitled" without an association. */
    std::string document_name;

    /*! \brief True when the document has edits past its file; drives the dirty marker. */
    bool dirty{false};

    /*! \brief True when Save can overwrite an associated file; untitled routes to Save As. */
    bool has_destination{false};

    /*! \brief Directory the tone-file choosers start in; empty falls back to the user home. */
    std::filesystem::path chooser_directory;

    /*!
    \brief Compares two Tone Designer view states by their stored values.
    \param lhs Left-hand view state.
    \param rhs Right-hand view state.
    \return True when both view states store equal values.
    */
    friend bool operator==(const ToneDesignerViewState& lhs, const ToneDesignerViewState& rhs) =
        default;
};

} // namespace rock_hero::editor::core
