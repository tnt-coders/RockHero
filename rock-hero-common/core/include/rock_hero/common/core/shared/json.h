/*!
\file json.h
\brief Shared JUCE JSON helpers for headless Rock Hero package and import code.
*/

#pragma once

#include <cstdint>
#include <expected>
#include <initializer_list>
#include <juce_core/juce_core.h>
#include <optional>
#include <string>
#include <string_view>

namespace rock_hero::common::core
{

/*!
\brief Non-instantiable JSON helper collection.

This intentionally mirrors JUCE's `juce::JSON` and `juce::JSONUtils` style instead of adding a
`common::core::json` namespace. The type grouping keeps call sites at `Json::...` while preserving
the current public namespace depth.
*/
struct Json
{
    /*! \brief Prevents construction; all behavior is exposed as static helpers. */
    Json() = delete;

    /*! \brief JSON helper failure reason. */
    enum class ErrorCode : std::uint8_t
    {
        /*! \brief JSON text could not be parsed. */
        ParseFailed,
    };

    /*! \brief Recoverable JSON helper error with stable code and display diagnostic. */
    struct [[nodiscard]] Error
    {
        /*!
        \brief Creates an error with the default message for the supplied code.
        \param error_code Stable failure code.
        */
        explicit Error(ErrorCode error_code);

        /*!
        \brief Creates an error with an explicit diagnostic message.
        \param error_code Stable failure code.
        \param message_text Display or log diagnostic.
        */
        Error(ErrorCode error_code, std::string message_text);

        /*! \brief Stable failure code. */
        ErrorCode code{};

        /*! \brief Display or log diagnostic. */
        std::string message;
    };

    /*! \brief Pairs a JSON object property name with the value written for that property. */
    struct Property
    {
        /*!
        \brief Creates a JSON object property pair.
        \param property_name Object property name.
        \param property_value Object property value.
        */
        Property(const char* property_name, juce::var property_value);

        /*! \brief Object property name. */
        const char* name{};

        /*! \brief Object property value. */
        juce::var value;
    };

    /*!
    \brief Converts UTF-8 property text into the JUCE identifier used for object lookup.
    \param key Property name text.
    \return JUCE identifier for the supplied property name.
    */
    [[nodiscard]] static juce::Identifier identifier(std::string_view key);

    /*!
    \brief Reads a JSON property value from an object-like JUCE value.
    \param object Object to read from.
    \param property_name Property name to read.
    \return Property value, or JUCE's void value when the property is absent.
    */
    [[nodiscard]] static const juce::var& value(
        const juce::var& object, std::string_view property_name);

    /*!
    \brief Stores a UTF-8 string in a JUCE JSON value.
    \param text Text to store.
    \return JUCE string value.
    */
    [[nodiscard]] static juce::var makeString(const std::string& text);

    /*!
    \brief Creates an empty JUCE JSON array value.
    \return JUCE array value.
    */
    [[nodiscard]] static juce::var makeArray();

    /*!
    \brief Creates a JUCE JSON object value with the supplied properties.
    \param properties Object properties to set.
    \return JUCE object value.
    */
    [[nodiscard]] static juce::var makeObject(std::initializer_list<Property> properties);

    /*!
    \brief Parses a JUCE string as JSON while preserving the parser diagnostic.
    \param text JSON document text.
    \return Parsed JUCE value, or a JSON error carrying the parser diagnostic.
    */
    [[nodiscard]] static std::expected<juce::var, Error> parseDocument(const juce::String& text);

    /*!
    \brief Parses UTF-8 JSON text while tolerating a UTF-8 byte-order mark.
    \param text UTF-8 JSON document text.
    \return Parsed JUCE value, or a JSON error carrying the parser diagnostic.
    */
    [[nodiscard]] static std::expected<juce::var, Error> parseUtf8Document(std::string_view text);

    /*!
    \brief Attempts to read a string property from a JSON object.

    Returns nullopt when the property is absent or not a string so the caller can report a
    domain-specific error. Use readOptionalString when an absent value should fall back to a
    default instead.

    \param object Object to read from.
    \param property_name Property name to read.
    \return String value, or empty optional when the property is absent or not a string.
    */
    [[nodiscard]] static std::optional<std::string> tryReadString(
        const juce::var& object, std::string_view property_name);

    /*!
    \brief Attempts to read a numeric property from a JSON object as a double.

    Accepts both integer and double JSON values so loudness fields written as `-16` and `-16.0`
    deserialize identically. Returns nullopt when the property is absent or not numeric so the
    caller can report a domain-specific error.

    \param object Object to read from.
    \param property_name Property name to read.
    \return Numeric value, or empty optional when the property is absent or not numeric.
    */
    [[nodiscard]] static std::optional<double> tryReadDouble(
        const juce::var& object, std::string_view property_name);

    /*!
    \brief Attempts to read an integer property from a JSON object as a 64-bit signed integer.

    Rejects floating-point values to keep callers like size_bytes from silently truncating.
    Returns nullopt when the property is absent or not an integer.

    \param object Object to read from.
    \param property_name Property name to read.
    \return Integer value, or empty optional when the property is absent or not an integer.
    */
    [[nodiscard]] static std::optional<std::int64_t> tryReadInt64(
        const juce::var& object, std::string_view property_name);

    /*!
    \brief Reads an optional string property from a JSON object.
    \param object Object to read from.
    \param property_name Property name to read.
    \param fallback Value returned when the property is absent or not a string.
    \return String value or fallback.
    */
    [[nodiscard]] static std::string readOptionalString(
        const juce::var& object, std::string_view property_name, const std::string& fallback = {});

    /*!
    \brief Reads an optional boolean property from a JSON object.
    \param object Object to read from.
    \param property_name Property name to read.
    \param fallback Value returned when the property is absent or not a boolean.
    \return Boolean value or fallback.
    */
    [[nodiscard]] static bool readOptionalBool(
        const juce::var& object, std::string_view property_name, bool fallback = false);

    /*!
    \brief Reads an optional integer property from a JSON object.
    \param object Object to read from.
    \param property_name Property name to read.
    \param fallback Value returned when the property is absent or not an integer.
    \return Integer value or fallback.
    */
    [[nodiscard]] static int readOptionalInt(
        const juce::var& object, std::string_view property_name, int fallback);

    /*!
    \brief Reads an optional numeric property from a JSON object as a double.

    Accepts both integer and double JSON values so gain fields written as `0` and `0.0`
    deserialize identically.

    \param object Object to read from.
    \param property_name Property name to read.
    \param fallback Value returned when the property is absent or not numeric.
    \return Numeric value or fallback.
    */
    [[nodiscard]] static double readOptionalDouble(
        const juce::var& object, std::string_view property_name, double fallback);
};

} // namespace rock_hero::common::core
