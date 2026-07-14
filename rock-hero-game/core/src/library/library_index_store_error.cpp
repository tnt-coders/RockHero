#include "library/library_index_store_error.h"

#include <utility>

namespace rock_hero::game::core
{

namespace
{

// Default display message for each stable code.
[[nodiscard]] std::string defaultMessage(const LibraryIndexStoreErrorCode error_code)
{
    switch (error_code)
    {
        case LibraryIndexStoreErrorCode::CouldNotWriteIndex:
            return "The library index file could not be written";
    }
    return "Unknown library index persistence error";
}

} // namespace

LibraryIndexStoreError::LibraryIndexStoreError(const LibraryIndexStoreErrorCode error_code)
    : code(error_code)
    , message(defaultMessage(error_code))
{}

LibraryIndexStoreError::LibraryIndexStoreError(
    const LibraryIndexStoreErrorCode error_code, std::string message_text)
    : code(error_code)
    , message(std::move(message_text))
{}

} // namespace rock_hero::game::core
