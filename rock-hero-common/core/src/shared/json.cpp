#include "shared/json.h"

#include <utility>

namespace rock_hero::common::core
{

// Supplies a generic JSON parse diagnostic when a caller only needs the stable code.
Json::Error::Error(Json::ErrorCode error_code)
    : Error(error_code, "Could not parse JSON document")
{}

// Stores parser detail without making message text part of the branchable error contract.
Json::Error::Error(Json::ErrorCode error_code, std::string message_text)
    : code(error_code)
    , message(std::move(message_text))
{}

// Moves a JUCE value into a property pair so initializer-list object construction stays concise.
Json::Property::Property(const char* property_name, juce::var property_value)
    : name(property_name)
    , value(std::move(property_value))
{}

// Preserves UTF-8 property names when converting to JUCE's identifier-based object lookup.
juce::Identifier Json::identifier(std::string_view key)
{
    return juce::Identifier{juce::String::fromUTF8(key.data(), static_cast<int>(key.size()))};
}

// Centralizes absent-property behavior so callers get JUCE's normal void value.
const juce::var& Json::value(const juce::var& object, std::string_view property_name)
{
    return object[identifier(property_name)];
}

// Supports imported formats that vary JSON property capitalization across versions.
const juce::var* Json::findValue(
    const juce::var& object, std::initializer_list<std::string_view> property_names)
{
    if (!object.isObject())
    {
        return nullptr;
    }

    for (const std::string_view property_name : property_names)
    {
        const juce::Identifier property_identifier = identifier(property_name);
        if (object.hasProperty(property_identifier))
        {
            return &object[property_identifier];
        }
    }

    return nullptr;
}

// Converts project-owned UTF-8 strings into JUCE string values before serialization.
juce::var Json::makeString(const std::string& text)
{
    return juce::var{juce::String::fromUTF8(text.c_str())};
}

// Creates a mutable JUCE array value for callers that append entries incrementally.
juce::var Json::makeArray()
{
    return juce::var{juce::Array<juce::var>{}};
}

// Creates object JSON without repeating DynamicObject allocation and property assignment.
juce::var Json::makeObject(std::initializer_list<Json::Property> properties)
{
    juce::var object{new juce::DynamicObject{}};
    juce::DynamicObject* const dynamic_object = object.getDynamicObject();
    for (const Json::Property& property : properties)
    {
        dynamic_object->setProperty(identifier(property.name), property.value);
    }

    return object;
}

// Preserves JUCE's parse diagnostic while returning the project-owned JSON error type.
std::expected<juce::var, Json::Error> Json::parseDocument(const juce::String& text)
{
    juce::var document;
    const juce::Result result = juce::JSON::parse(text, document);
    if (result.failed())
    {
        return std::unexpected{Error{
            ErrorCode::ParseFailed,
            result.getErrorMessage().toStdString(),
        }};
    }

    return document;
}

// Handles the BOM variant produced by some external JSON sources before delegating to JUCE.
std::expected<juce::var, Json::Error> Json::parseUtf8Document(std::string_view text)
{
    constexpr std::string_view utf8_bom{"\xEF\xBB\xBF"};
    if (text.starts_with(utf8_bom))
    {
        text.remove_prefix(utf8_bom.size());
    }

    return parseDocument(juce::String::fromUTF8(text.data(), static_cast<int>(text.size())));
}

// Treats missing and malformed strings the same so callers can own domain error messages.
std::optional<std::string> Json::tryReadString(
    const juce::var& object, std::string_view property_name)
{
    const juce::var& property_value = value(object, property_name);
    if (!property_value.isString())
    {
        return std::nullopt;
    }

    return property_value.toString().toStdString();
}

// Keeps optional string fields lenient for draft package metadata and plugin descriptors.
std::string Json::readOptionalString(
    const juce::var& object, std::string_view property_name, const std::string& fallback)
{
    const juce::var& property_value = value(object, property_name);
    if (!property_value.isString())
    {
        return fallback;
    }

    return property_value.toString().toStdString();
}

// Keeps optional booleans lenient for metadata fields that can be absent in older documents.
bool Json::readOptionalBool(const juce::var& object, std::string_view property_name, bool fallback)
{
    const juce::var& property_value = value(object, property_name);
    return property_value.isBool() ? static_cast<bool>(property_value) : fallback;
}

// Reads integer fields only from integer JSON values so accidental floats do not get truncated.
int Json::readOptionalInt(const juce::var& object, std::string_view property_name, int fallback)
{
    const juce::var& property_value = value(object, property_name);
    if (!property_value.isInt() && !property_value.isInt64())
    {
        return fallback;
    }

    return static_cast<int>(property_value);
}

// Accepts both integer and double JSON so authors can write "-16" or "-16.0" interchangeably.
std::optional<double> Json::tryReadDouble(const juce::var& object, std::string_view property_name)
{
    const juce::var& property_value = value(object, property_name);
    if (!property_value.isDouble() && !property_value.isInt() && !property_value.isInt64())
    {
        return std::nullopt;
    }

    return static_cast<double>(property_value);
}

// Keeps optional double fields lenient for gain and other numeric metadata that can be absent in
// older tone documents.
double Json::readOptionalDouble(
    const juce::var& object, std::string_view property_name, double fallback)
{
    return tryReadDouble(object, property_name).value_or(fallback);
}

// Reject floats so size_bytes-style fields cannot silently truncate; callers report absence
// through the nullopt return.
std::optional<std::int64_t> Json::tryReadInt64(
    const juce::var& object, std::string_view property_name)
{
    const juce::var& property_value = value(object, property_name);
    if (!property_value.isInt() && !property_value.isInt64())
    {
        return std::nullopt;
    }

    return static_cast<std::int64_t>(property_value);
}

} // namespace rock_hero::common::core
