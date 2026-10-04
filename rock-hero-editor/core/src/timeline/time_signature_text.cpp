#include <rock_hero/editor/core/timeline/time_signature_text.h>
#include <string>

namespace rock_hero::editor::core
{

std::string timeSignatureText(const common::core::TimeSignature meter)
{
    return std::to_string(meter.numerator) + "/" + std::to_string(meter.denominator);
}

} // namespace rock_hero::editor::core
