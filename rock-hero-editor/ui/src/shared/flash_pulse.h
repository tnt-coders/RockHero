/*!
\file flash_pulse.h
\brief The editor's one-shot flashes: the one pulse clock every flash runs on, the record of a
running flash, and the refusal flash's glow and its one replacement rule.
*/

#pragma once

#include <juce_graphics/juce_graphics.h>
#include <optional>
#include <utility>

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
\brief How a held mark rides a flash's clock: at full ink for a while, then fading linearly out.
*/
struct FlashHold
{
    /*! \brief How long the mark stands at full ink from the flash's start, in seconds. */
    double hold_seconds{};

    /*! \brief How long it then takes to fade to nothing, in seconds. */
    double fade_seconds{};
};

/*!
\brief How the tab lane's status line holds a refusal's sentence on the refusal flash's clock: long
enough to read at full ink, then a short fade. A sentence never rides the glow's pulses, which would
take it dark between them before it could be read.
*/
inline constexpr FlashHold g_refusal_line_hold{.hold_seconds = 2.5, .fade_seconds = 0.4};

/*!
\brief The pulse train of one running flash: lit as it starts, dark between pulses, and dark again
as it ends.

Every flash runs on one of these, so the pulse shape is stated once and each flash only names its
train. Its start is also the one clock a mark HELD on the flash reads (\ref heldLevel), so a held
mark and the pulsing glows of one flash can never drift apart. A flash that holds a mark carries the
hold, so \ref finished is the ONE answer to whether the flash still shows anything, which the
surface retiring the flash reads.
*/
class FlashPulse
{
public:
    /*!
    \brief Starts the pulse train now.
    \param train How many times the flash lights, over how long.
    \param hold How a mark held on the flash stands and fades, or empty where it holds none.
    */
    explicit FlashPulse(FlashTrain train, std::optional<FlashHold> hold = std::nullopt) noexcept;

    /*!
    \brief Reports whether the flash has nothing left to show, so the surface retires it.
    \return True once the train's whole duration has passed and, where the flash holds a mark, the
            hold and its fade have too.
    */
    [[nodiscard]] bool finished() const noexcept;

    /*!
    \brief Returns the flash's level now.
    \return Level in [0, 1]: 1 as the train starts, 0 between pulses and from the end on.
    */
    [[nodiscard]] float level() const noexcept;

    /*!
    \brief Returns the level of the mark held on this flash's clock instead of pulsing on its train.
    \return Level in [0, 1]: 1 through the hold, falling linearly to 0 across the fade; 0 for a
            flash that holds no mark.
    */
    [[nodiscard]] float heldLevel() const noexcept;

    /*!
    \brief Reports whether the mark held on this flash's clock has faded out.
    \return True once the hold and the fade have both passed, or for a flash that holds no mark.
    */
    [[nodiscard]] bool heldFinished() const noexcept;

private:
    // How long the flash has run, in seconds: the one clock its train and every hold read.
    [[nodiscard]] double elapsedSeconds() const noexcept;

    // How far the train has run, in [0, 1].
    [[nodiscard]] double progress() const noexcept;

    // The train's shape.
    FlashTrain m_train{};

    // How the mark held on the flash stands and fades, or empty where it holds none.
    std::optional<FlashHold> m_hold{};

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
\brief The area the refusal glow around a mark can paint, for a surface that repaints only there.
\param outline_bounds The bounds of the mark's outline.
\param extent The mark's size, as given to \ref paintRefusalGlow.
\return The outline's bounds grown by the glow's reach and a pixel of antialiasing.
*/
[[nodiscard]] juce::Rectangle<float> refusalGlowArea(
    juce::Rectangle<float> outline_bounds, float extent);

/*!
\brief Starts a refusal flash over \p refused, replacing whatever the surface's flash lit before.

THE one rule of every surface that flashes a refusal: the newest refusal REPLACES the surface's
flash — what it lights and what it says — and restarts its clock. Every surface a refusal reaches
therefore pulses in step from it, and a held key or a drag that keeps being refused, naming the same
thing each event, reads as a steady full glow that pulses out once the refusals stop. A refusal
never re-lights what an earlier, different refusal named, which would lie about what was just
refused; and one naming nothing on the surface clears it, since the newest refusal has nothing to
show there.

\tparam Refused The surface's record of what the flash lights; it answers empty().
\param flash The surface's refusal flash, empty while none runs.
\param refused What the new refusal names on the surface.
\param hold How a mark the record holds stands and fades (the tab lane's sentence), or empty where
       it holds none, so the flash retires with its train.
\return True when the surface has something to repaint: a flash ran before, or runs now.
*/
template <typename Refused>
[[nodiscard]] bool startRefusalFlash(
    std::optional<PulsedFlash<Refused>>& flash, Refused refused,
    const std::optional<FlashHold> hold = std::nullopt)
{
    const bool was_running = flash.has_value();
    if (refused.empty())
    {
        flash.reset();
        return was_running;
    }
    flash = PulsedFlash<Refused>{
        .subject = std::move(refused),
        .pulse = FlashPulse{g_refusal_flash_train, hold},
    };
    return true;
}

} // namespace rock_hero::editor::ui
