/*!
\file package_id.h
\brief Stable package object ID helpers.
*/

#pragma once

#include <string>
#include <string_view>

namespace rock_hero::common::core
{

/*!
\brief Generates a fresh stable Rock Hero package object ID.

The returned value is a canonical lowercase dashed RFC 4122 version 4 UUID string.
*/
[[nodiscard]] std::string generatePackageId();

/*!
\brief Builds the canonical package-relative tone document reference for a tone ID.
\param tone_id Canonical package ID that identifies the tone document.
\return Package-relative `tones/<tone-id>/tone.json` reference.
*/
[[nodiscard]] std::string toneDocumentRefForToneId(std::string_view tone_id);

/*!
\brief Reports whether an ID is canonical lowercase dashed UUIDv4 text.
\param id Candidate package ID text.
\return True when the ID is exactly the spelling Rock Hero persists.
*/
[[nodiscard]] bool isCanonicalPackageId(std::string_view id) noexcept;

/*!
\brief Reports whether a tone document reference uses Rock Hero's canonical tone layout.
\param tone_document_ref Package-relative tone document reference.
\return True when the reference is exactly `tones/<uuid>/tone.json`.
*/
[[nodiscard]] bool isCanonicalToneDocumentRef(std::string_view tone_document_ref) noexcept;

} // namespace rock_hero::common::core
