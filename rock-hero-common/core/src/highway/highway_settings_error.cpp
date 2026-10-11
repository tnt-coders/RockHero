#include "highway/highway_settings_error.h"

#include <utility>

namespace rock_hero::common::core
{

HighwaySettingsError::HighwaySettingsError(
    HighwaySettingsErrorCode error_code, std::string message_text)
    : code(error_code)
    , message(std::move(message_text))
{}

} // namespace rock_hero::common::core
