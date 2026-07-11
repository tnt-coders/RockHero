#include "surface/game_window.h"

#include <rock_hero/common/core/shared/logger.h>

// The shell provides a plain portable main(), so SDL must not rewrite the entry point; this is
// the app-provided-main pattern SDL_main.h documents (and the gate spike proved).
#define SDL_MAIN_HANDLED
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <string>
#include <utility>

namespace rock_hero::game::ui
{

namespace
{

// Formats an SDL failure as "<step>: <SDL diagnostic>" so the typed error keeps the SDL detail.
[[nodiscard]] std::string sdlErrorMessage(const std::string& step)
{
    return step + ": " + SDL_GetError();
}

} // namespace

// Fills the default diagnostic for each stable failure reason.
GameWindowError::GameWindowError(const GameWindowErrorCode error_code)
    : code{error_code}
{
    switch (error_code)
    {
        case GameWindowErrorCode::VideoInitializationFailed:
        {
            message = "SDL video subsystem failed to initialize";
            break;
        }
        case GameWindowErrorCode::WindowCreationFailed:
        {
            message = "Game window could not be created";
            break;
        }
        case GameWindowErrorCode::NativeHandleUnavailable:
        {
            message = "Game window exposes no native window handle";
            break;
        }
    }
}

// Carries a contextual diagnostic alongside the stable failure reason.
GameWindowError::GameWindowError(const GameWindowErrorCode error_code, std::string message_text)
    : code{error_code}
    , message{std::move(message_text)}
{}

// Initializes SDL video and creates the game window, translating each failed step into the typed
// error for it. SDL_WINDOW_HIGH_PIXEL_DENSITY is inert on Windows today but makes the
// pixel-size-driven resize path real on platforms where points and pixels differ (0a memo:
// portability preserved by choices).
std::expected<GameWindow, GameWindowError> GameWindow::create(
    const std::string& title, const PixelSize size)
{
    // SDL_MAIN_HANDLED main: tell SDL the app owns the entry point before any other SDL call.
    SDL_SetMainReady();

    // Video only for now; gamepad input is a purely additive SDL_InitSubSystem when the menu
    // work (plan 26) has a consumer for it.
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        return std::unexpected{GameWindowError{
            GameWindowErrorCode::VideoInitializationFailed,
            sdlErrorMessage("SDL_Init(SDL_INIT_VIDEO)"),
        }};
    }

    SDL_Window* const window = SDL_CreateWindow(
        title.c_str(),
        static_cast<int>(size.width),
        static_cast<int>(size.height),
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (window == nullptr)
    {
        SDL_Quit();
        return std::unexpected{GameWindowError{
            GameWindowErrorCode::WindowCreationFailed,
            sdlErrorMessage("SDL_CreateWindow"),
        }};
    }

    // The documented SDL3 property for the Win32 HWND; other platforms will branch here when the
    // portability work lands.
    void* const native_handle = SDL_GetPointerProperty(
        SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
    if (native_handle == nullptr)
    {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return std::unexpected{GameWindowError{GameWindowErrorCode::NativeHandleUnavailable}};
    }

    return GameWindow{window, native_handle};
}

// Adopts the created window; ownership tracking rides on the window pointer.
GameWindow::GameWindow(SDL_Window* window, void* native_window_handle) noexcept
    : m_window{window}
    , m_native_window_handle{native_window_handle}
{}

// The window pointer is the teardown token: only the last owner destroys the window and shuts
// SDL video down (SDL_Quit pairs with the SDL_Init in create()).
GameWindow::~GameWindow()
{
    if (m_window != nullptr)
    {
        SDL_DestroyWindow(m_window);
        SDL_Quit();
    }
}

// Transfers ownership; the source keeps null members so its destructor tears nothing down.
GameWindow::GameWindow(GameWindow&& other) noexcept
    : m_window{std::exchange(other.m_window, nullptr)}
    , m_native_window_handle{std::exchange(other.m_native_window_handle, nullptr)}
{}

// Returns the handle captured at creation; SDL keeps it valid for the window's lifetime.
void* GameWindow::nativeWindowHandle() const noexcept
{
    return m_native_window_handle;
}

// Queries the drawable size in physical pixels — the unit bgfx backbuffers are sized in, distinct
// from window coordinates on high-density displays. The SDL result is deliberately unchecked: a
// failure (implausible for a live window) leaves 0x0, which bgfx::reset clamps to a valid size.
PixelSize GameWindow::pixelSize() const
{
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(m_window, &width, &height);
    return PixelSize{
        .width = static_cast<std::uint32_t>(width),
        .height = static_cast<std::uint32_t>(height),
    };
}

// Drains the SDL event queue and reduces it to the frame-relevant signals. Pixel-size changes
// re-query the window rather than trusting event payloads: SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED
// is the event a DPI change routes through, and window-coordinate payloads only match pixels at
// scale 1.0 (a current-Windows accident the gate record flags under S2).
GameWindowEvents GameWindow::pollEvents()
{
    GameWindowEvents events;

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
        switch (event.type)
        {
            case SDL_EVENT_QUIT:
            {
                // Under loop model L2 the JUCE pump swallows WM_QUIT (no JUCEApplication
                // instance), so SDL's quit event is the shell's only quit signal by design.
                events.quit_requested = true;
                break;
            }
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            {
                events.pixel_size_changed = pixelSize();
                break;
            }
            case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
            {
                // DPI-change behavior was never exercised by the gate spike (S2 caveat, single
                // monitor); record the first real occurrence so a future multi-monitor run
                // produces evidence instead of a silent blind spot.
                RH_LOG_WARNING(
                    "game.surface",
                    "display scale changed to {}; DPI path is unexercised (S2)",
                    SDL_GetWindowDisplayScale(m_window));
                break;
            }
            default:
            {
                break;
            }
        }
    }

    return events;
}

} // namespace rock_hero::game::ui
