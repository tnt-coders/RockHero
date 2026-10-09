#include "timeline/time_signature_text.h"

#include <string>

namespace rock_hero::common::core
{

std::string timeSignatureText(const TimeSignature meter)
{
    return std::to_string(meter.numerator) + "/" + std::to_string(meter.denominator);
}

} // namespace rock_hero::common::core
