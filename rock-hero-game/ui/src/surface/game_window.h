/*!
\file game_window.h
\brief SDL3 game window: creation, native handle access, and per-frame event translation.
*/

#pragma once

#include <cstdint>
#include <expected>
#include <optional>
#include <string>

// SDL3's window type is an opaque C struct, so a forward declaration keeps SDL headers out of
// every consumer of this private header.
struct SDL_Window;

namespace rock_hero::game::ui
{

/*! \brief Window surface size in physical pixels (the unit bgfx backbuffers are sized in). */
struct PixelSize
{
    /*! \brief Horizontal size in pixels. */
    std::uint32_t width = 0;

    /*! \brief Vertical size in pixels. */
    std::uint32_t height = 0;
};

/*! \brief Stable reasons a game window can fail to come up. */
enum class GameWindowErrorCode : std::uint8_t
{
    /*! \brief SDL's video subsystem refused to initialize. */
    VideoInitializationFailed,

    /*! \brief SDL initialized but the window itself could not be created. */
    WindowCreationFailed,

    /*! \brief The created window exposed no native handle for the render device to target. */
    NativeHandleUnavailable,
};

/*! \brief Typed boundary error for game-window startup failures. */
struct [[nodiscard]] GameWindowError
{
    /*! \brief Stable failure reason for program branching. */
    GameWindowErrorCode code{};

    /*! \brief Human-readable diagnostic for UI display or logs. */
    std::string message;

    /*!
    \brief Creates an error with the default message for the code.
    \param error_code Stable failure reason.
    */
    explicit GameWindowError(GameWindowErrorCode error_code);

    /*!
    \brief Creates an error with a contextual message.
    \param error_code Stable failure reason.
    \param message_text Human-readable diagnostic.
    */
    GameWindowError(GameWindowErrorCode error_code, std::string message_text);
};

/*! \brief Frame-relevant events observed by one \ref GameWindow::pollEvents call. */
struct GameWindowEvents
{
    /*! \brief True when the user or system asked the game to quit. */
    bool quit_requested = false;

    /*! \brief Present when the window's pixel size changed; carries the new backbuffer size. */
    std::optional<PixelSize> pixel_size_changed;
};

/*!
\brief Owns the SDL video runtime and the single game window.

Move-only: the window (and with it SDL video ownership) transfers with the object, and the last
owner shuts SDL down. All members must be called on the thread that created the window (the
process main thread under loop model L2).
*/
class GameWindow
{
public:
    /*!
    \brief Initializes SDL video and creates the resizable, high-pixel-density game window.

    Also marks the process entry point as app-provided (SDL_MAIN_HANDLED main), so this must be
    reached from a plain main() rather than an SDL-generated entry point.

    \param title Window title text.
    \param size Initial window size in logical window coordinates.
    \return The created window, or a typed error naming the startup step that failed.
    */
    [[nodiscard]] static std::expected<GameWindow, GameWindowError> create(
        const std::string& title, PixelSize size);

    /*! \brief Destroys the window and shuts SDL video down when this object still owns them. */
    ~GameWindow();

    /*!
    \brief Transfers window and SDL ownership; the source is left empty and tears nothing down.
    \param other Window losing ownership.
    */
    GameWindow(GameWindow&& other) noexcept;

    GameWindow(const GameWindow&) = delete;
    GameWindow& operator=(const GameWindow&) = delete;
    GameWindow& operator=(GameWindow&&) = delete;

    /*!
    \brief Returns the native platform handle the render device targets (HWND on Windows).
    \return Native window handle captured at creation.
    */
    [[nodiscard]] void* nativeWindowHandle() const noexcept;

    /*!
    \brief Queries the window's current drawable size in physical pixels.
    \return Current backbuffer-relevant size.
    */
    [[nodiscard]] PixelSize pixelSize() const;

    /*!
    \brief Drains all pending SDL events and translates the frame-relevant ones.

    Call exactly once per frame, before building the frame, so input and size changes are as
    fresh as possible.

    \return Quit and resize signals observed this frame.
    */
    [[nodiscard]] GameWindowEvents pollEvents();

private:
    // Adopts an already-created window; only create() calls this.
    explicit GameWindow(SDL_Window* window, void* native_window_handle) noexcept;

    // Owning pointer doubling as the teardown token: non-null means this object still owns the
    // window and the SDL video subsystem shutdown.
    SDL_Window* m_window = nullptr;

    // Native handle captured once at creation; SDL owns the underlying window object.
    void* m_native_window_handle = nullptr;
};

} // namespace rock_hero::game::ui
