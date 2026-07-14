/*!
\file i_library_package_describer.h
\brief Port that peeks one package's description for the library scan.
*/

#pragma once

#include <expected>
#include <filesystem>
#include <rock_hero/common/core/package/package_description.h>
#include <rock_hero/common/core/package/song_package_error.h>

namespace rock_hero::game::core
{

/*!
\brief Describes one native song package without extracting it.

A thin seam over common/core's readRockSongPackageDescription so the scan engine is testable with a
fake describer. It preserves the Phase-1 typed SongPackageError all the way to the engine — no
stringifying between stages — so the engine can turn a hard read failure into a warning entry with
the real diagnostic (docs/design/architectural-principles.md "Typed Boundary Errors").
*/
class ILibraryPackageDescriber
{
public:
    /*! \brief Destroys the package-describer interface. */
    virtual ~ILibraryPackageDescriber() = default;

    /*!
    \brief Peeks one package's description straight from its archive.
    \param package_path Native `.rock` package to describe.
    \return The package description, or the typed reason it could not be read.
    */
    [[nodiscard]] virtual std::expected<
        common::core::PackageDescription, common::core::SongPackageError>
    describe(const std::filesystem::path& package_path) = 0;

protected:
    /*! \brief Creates the package-describer interface. */
    ILibraryPackageDescriber() = default;

    /*! \brief Copies the package-describer interface. */
    ILibraryPackageDescriber(const ILibraryPackageDescriber&) = default;

    /*! \brief Moves the package-describer interface. */
    ILibraryPackageDescriber(ILibraryPackageDescriber&&) = default;

    /*!
    \brief Assigns the package-describer interface from another instance.
    \return Reference to this package-describer interface.
    */
    ILibraryPackageDescriber& operator=(const ILibraryPackageDescriber&) = default;

    /*!
    \brief Move-assigns the package-describer interface from another instance.
    \return Reference to this package-describer interface.
    */
    ILibraryPackageDescriber& operator=(ILibraryPackageDescriber&&) = default;
};

} // namespace rock_hero::game::core
