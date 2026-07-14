#include "rock_hero/common/audio/transport/transport_error.h"

#include <utility>

namespace rock_hero::common::audio
{

namespace
{

// Centralises default transport error messages so public errors stay consistent.
[[nodiscard]] std::string defaultTransportErrorMessage(TransportErrorCode code)
{
    switch (code)
    {
        case TransportErrorCode::SpeedNotSupported:
        {
            return "Playback speed is not supported";
        }
        case TransportErrorCode::LoopRegionTooShort:
        {
            return "Loop region is shorter than the supported minimum";
        }
    }

    return "Transport operation failed";
}

} // namespace

TransportError::TransportError(TransportErrorCode error_code)
    : TransportError(error_code, defaultTransportErrorMessage(error_code))
{}

TransportError::TransportError(TransportErrorCode error_code, std::string message_text)
    : code(error_code)
    , message(std::move(message_text))
{}

} // namespace rock_hero::common::audio
