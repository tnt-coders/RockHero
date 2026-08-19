#include "highway/structural_art.h"

#include <juce_graphics/juce_graphics.h>

namespace rock_hero::common::ui
{

// Decodes the PNG bytes and enforces the structural scheme's no-alpha rule for every caller.
std::expected<juce::Image, StructuralArtError> decodeStructuralArtPng(
    const std::span<const std::byte> png_bytes)
{
    if (png_bytes.empty())
    {
        return std::unexpected(StructuralArtError::UndecodableImage);
    }
    juce::MemoryInputStream stream{png_bytes.data(), png_bytes.size(), false};
    const juce::Image decoded = juce::PNGImageFormat{}.decodeImage(stream);
    if (decoded.isNull())
    {
        return std::unexpected(StructuralArtError::UndecodableImage);
    }
    // Whether the decoded image has an alpha channel is NOT the same question as whether the file
    // did, so it cannot be the test. On macOS JUCE decodes every PNG through CoreImage, which
    // cannot produce a 24-bit image and so always hands back ARGB with opaque alpha
    // (juce_CoreGraphicsContext_mac.mm juce_loadWithCoreImage); asking hasAlphaChannel() there
    // rejects every alpha-free PNG, including the shipped assets. Both of JUCE's decode paths
    // record the file's real alpha state in this property for exactly this purpose, so it is the
    // portable answer. An opaque alpha channel premultiplies by 1 and leaves the structural
    // channels intact, which is why measuring the macOS ARGB image is still bit-exact.
    if (static_cast<bool>(decoded.getProperties()->getWithDefault("originalImageHadAlpha", false)))
    {
        return std::unexpected(StructuralArtError::AlphaBearingImage);
    }
    return decoded;
}

} // namespace rock_hero::common::ui
