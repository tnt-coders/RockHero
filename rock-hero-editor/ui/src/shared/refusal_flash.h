/*!
\file refusal_flash.h
\brief The refusal flash's one clock and one glow, shared by every surface that flashes a refused
element.
*/

#pragma once

#include <algorithm>
#include <juce_graphics/juce_graphics.h>
#include <optional>
#include <utility>
#include <vector>

namespace rock_hero::editor::ui
{

/*!
\brief The pulse train of one running refusal flash: lit at the refusal, dark between pulses, and
dark again as it ends.

Every surface that flashes a refused element runs its flash on one of these, so the pulse count
and timing are stated once. A surface that receives a further refusal while one runs keeps the
running pulse, so a held key or a drag reads as one train rather than restarting it.
*/
class RefusalPulse
{
public:
    /*! \brief Starts the pulse train now. */
    RefusalPulse() noexcept;

    /*!
    \brief Reports whether every pulse has run, so the surface retires its flash.
    \return True once the train's whole duration has passed.
    */
    [[nodiscard]] bool finished() const noexcept;

    /*!
    \brief Returns the glow's level now.
    \return Level in [0, 1]: 1 at the refusal, 0 between pulses and from the end on.
    */
    [[nodiscard]] float level() const noexcept;

private:
    // When the train began, in milliseconds on JUCE's high-resolution counter.
    double m_started_ms;
};

/*!
\brief Paints the refusal glow spreading outward from a mark's outline.

The glow is drawn in the theme's invalid ink at \p level and clipped to the OUTSIDE of the outline,
so it never tints the mark it surrounds.

\param g Graphics context to paint into.
\param outline The mark's closed outline: the shape its selection ring strokes.
\param extent The mark's size, which the glow's reach scales with.
\param level The running pulse's level (\ref RefusalPulse::level).
*/
void paintRefusalGlow(juce::Graphics& g, const juce::Path& outline, float extent, float level);

/*!
\brief One surface's running refusal flash: what glows, in the surface's own terms, and the pulse
it glows on.
\tparam Refused The surface's record of the refused elements.
*/
template <typename Refused> struct RefusalFlash
{
    /*! \brief Every refused element the flash has joined, in arrival order. */
    Refused refused;

    /*! \brief The pulse train the elements glow on. */
    RefusalPulse pulse;
};

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
\brief Starts a flash over \p refused, or joins the one still running.

A flash whose pulses have run counts as none, even where no frame has retired it yet (a surface off
screen), so a new refusal starts its own pulse instead of joining a finished one. A refusal arriving
while one runs joins it on the running pulse, so a held key or a drag reads as one train.

\param flash The surface's flash, empty while none runs.
\param refused What the new refusal names.
\param join Merges \p refused into the running flash's record.
*/
template <typename Refused, typename Join>
void flashOrJoinRefusal(
    std::optional<RefusalFlash<Refused>>& flash, Refused refused, const Join& join)
{
    if (flash.has_value() && !flash->pulse.finished())
    {
        join(flash->refused, refused);
        return;
    }
    flash = RefusalFlash<Refused>{.refused = std::move(refused), .pulse = {}};
}

/*!
\brief Retires a flash whose pulses have run.
\param flash The surface's flash, empty while none runs.
\return True when a flash was running this frame, so the surface repaints.
*/
template <typename Refused>
[[nodiscard]] bool retireFinishedRefusal(std::optional<RefusalFlash<Refused>>& flash)
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

} // namespace rock_hero::editor::ui
