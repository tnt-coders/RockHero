/*!
\file flash_pulse.h
\brief The editor's one-shot flashes: the one pulse clock every flash runs on, the record of a
running flash, and the refusal flash's glow and join.
*/

#pragma once

#include <algorithm>
#include <juce_graphics/juce_graphics.h>
#include <optional>
#include <utility>
#include <vector>

namespace rock_hero::editor::ui
{

/*! \brief The shape of one flash's pulse train: how many times it lights, over how long. */
struct FlashTrain
{
    /*! \brief How many times the flash lights; the last pulse fades out as the train ends. */
    double pulses{};

    /*! \brief How long the whole train runs, in seconds. */
    double seconds{};
};

/*!
\brief The refusal flash's train: two pulses over a little more than half a second, shared by every
surface that flashes a refused element.
*/
inline constexpr FlashTrain g_refusal_flash_train{.pulses = 2.0, .seconds = 0.6};

/*!
\brief The pulse train of one running flash: lit as it starts, dark between pulses, and dark again
as it ends.

Every flash runs on one of these, so the pulse shape is stated once and each flash only names its
train.
*/
class FlashPulse
{
public:
    /*!
    \brief Starts the pulse train now.
    \param train How many times the flash lights, over how long.
    */
    explicit FlashPulse(FlashTrain train) noexcept;

    /*!
    \brief Reports whether every pulse has run, so the surface retires its flash.
    \return True once the train's whole duration has passed.
    */
    [[nodiscard]] bool finished() const noexcept;

    /*!
    \brief Returns the flash's level now.
    \return Level in [0, 1]: 1 as the train starts, 0 between pulses and from the end on.
    */
    [[nodiscard]] float level() const noexcept;

private:
    // How far the train has run, in [0, 1].
    [[nodiscard]] double progress() const noexcept;

    // The train's shape.
    FlashTrain m_train{};

    // When the train began, in milliseconds on JUCE's high-resolution counter.
    double m_started_ms{};
};

/*!
\brief One surface's running flash: what lights, in the surface's own terms, and the pulse it
lights on.
\tparam Subject The surface's record of what the flash lights.
*/
template <typename Subject> struct PulsedFlash
{
    /*! \brief What the flash lights. */
    Subject subject{};

    /*! \brief The pulse train it lights on. */
    FlashPulse pulse;
};

/*!
\brief Retires a flash whose pulses have run.
\param flash The surface's flash, empty while none runs.
\return True when a flash was running this frame, so the surface repaints.
*/
template <typename Subject>
[[nodiscard]] bool retireFinishedFlash(std::optional<PulsedFlash<Subject>>& flash)
{
    if (!flash.has_value())
    {
        return false;
    }
    if (flash->pulse.finished())
    {
        flash.reset();
    }
    return true;
}

/*!
\brief Paints the refusal glow spreading outward from a mark's outline.

The glow is drawn in the theme's invalid ink at \p level and clipped to the OUTSIDE of the outline,
so it never tints the mark it surrounds.

\param g Graphics context to paint into.
\param outline The mark's closed outline: the shape its selection ring strokes.
\param extent The mark's size, which the glow's reach scales with.
\param level The running pulse's level (\ref FlashPulse::level).
*/
void paintRefusalGlow(juce::Graphics& g, const juce::Path& outline, float extent, float level);

/*!
\brief Adds each refused element not already in \p into, in arrival order.

A union, never an append: an element reported twice — by one press's two reports, or a held key's
repeats — would be stroked twice, and stacked strokes lose the pulse.

\param into The elements already glowing.
\param from The elements a further refusal names.
*/
template <typename Element>
void joinRefused(std::vector<Element>& into, const std::vector<Element>& from)
{
    for (const Element& element : from)
    {
        // A predicate, not a value find: under clang, the MSVC STL sends a value find over a
        // trivially comparable 16-byte element down a vectorized path that has no 16-byte case,
        // which fails to compile.
        if (std::ranges::none_of(into, [&element](const Element& held) { return held == element; }))
        {
            into.push_back(element);
        }
    }
}

/*!
\brief Starts a refusal flash over \p refused, or joins the one still running.

A flash whose pulses have run counts as none, even where no frame has retired it yet (a surface off
screen), so a new refusal starts its own pulse instead of joining a finished one. A refusal arriving
while one runs joins it on the running pulse, so a held key or a drag reads as one train.

\param flash The surface's refusal flash, empty while none runs.
\param refused What the new refusal names.
\param join Merges \p refused into the running flash's record.
*/
template <typename Refused, typename Join>
void flashOrJoinRefusal(
    std::optional<PulsedFlash<Refused>>& flash, Refused refused, const Join& join)
{
    if (flash.has_value() && !flash->pulse.finished())
    {
        join(flash->subject, refused);
        return;
    }
    flash = PulsedFlash<Refused>{
        .subject = std::move(refused),
        .pulse = FlashPulse{g_refusal_flash_train},
    };
}

} // namespace rock_hero::editor::ui
