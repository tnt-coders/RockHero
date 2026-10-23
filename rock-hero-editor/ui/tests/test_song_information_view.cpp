#include "song_information/song_information_view.h"

#include <catch2/catch_test_macros.hpp>
#include <juce_gui_basics/juce_gui_basics.h>
#include <rock_hero/common/core/song/song.h>
#include <rock_hero/editor/ui/testing/component_test_helpers.h>
#include <utility>
#include <vector>

namespace rock_hero::editor::ui
{

namespace
{

using common::core::SongMetadata;
using testing::clickButton;
using testing::findRequiredDescendant;

// The view with its two answers recorded, so a test reads exactly what each gesture handed on.
struct RecordedSongInformationView
{
    explicit RecordedSongInformationView(const SongMetadata& seed)
        : view{
              seed,
              [this](SongMetadata metadata) { submitted.push_back(std::move(metadata)); },
              [this] { ++cancels; }
          }
    {}

    // The field or button with the component id.
    template <class ComponentType> [[nodiscard]] ComponentType& find(juce::StringRef id)
    {
        return findRequiredDescendant<ComponentType>(view, id);
    }

    // Every submission, oldest first.
    std::vector<SongMetadata> submitted;

    // How many cancels the view reported.
    int cancels{0};

    // Declared last, so the records its callbacks write exist before it does.
    SongInformationView view;
};

} // namespace

// The fields open on the stored information, and a year of 0 (unknown) opens blank rather than as
// a literal zero.
TEST_CASE("Song information view shows its seed", "[ui][song-information]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordedSongInformationView known{
        SongMetadata{.title = "Title", .artist = "Artist", .album = "Album", .year = 1999}
    };
    CHECK(known.find<juce::TextEditor>("song_information_title").getText() == "Title");
    CHECK(known.find<juce::TextEditor>("song_information_artist").getText() == "Artist");
    CHECK(known.find<juce::TextEditor>("song_information_album").getText() == "Album");
    CHECK(known.find<juce::TextEditor>("song_information_year").getText() == "1999");

    RecordedSongInformationView unknown{SongMetadata{}};
    CHECK(unknown.find<juce::TextEditor>("song_information_year").getText().isEmpty());
}

// OK hands on the fields exactly as typed, once, with the year read from its digits and a blank
// year read as 0.
TEST_CASE("Song information OK submits the typed values", "[ui][song-information]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordedSongInformationView recorded{
        SongMetadata{.title = "Old", .artist = "", .album = "", .year = 1980}
    };
    recorded.find<juce::TextEditor>("song_information_title").setText(" New ");
    recorded.find<juce::TextEditor>("song_information_artist").setText("Band");
    recorded.find<juce::TextEditor>("song_information_year").setText("2001");

    clickButton(recorded.find<juce::TextButton>("song_information_ok"));
    REQUIRE(recorded.submitted.size() == 1);
    CHECK(
        recorded.submitted.front() ==
        SongMetadata{.title = " New ", .artist = "Band", .album = "", .year = 2001});
    CHECK(recorded.cancels == 0);

    recorded.find<juce::TextEditor>("song_information_year").setText("");
    clickButton(recorded.find<juce::TextButton>("song_information_ok"));
    REQUIRE(recorded.submitted.size() == 2);
    CHECK(recorded.submitted.back().year == 0);
}

// Cancel reports a cancel and nothing else: the dialog's values reach no one.
TEST_CASE("Song information Cancel only cancels", "[ui][song-information]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordedSongInformationView recorded{SongMetadata{}};
    recorded.find<juce::TextEditor>("song_information_title").setText("Typed");

    clickButton(recorded.find<juce::TextButton>("song_information_cancel"));
    CHECK(recorded.cancels == 1);
    CHECK(recorded.submitted.empty());
}

// The year takes typed digits only, at most four, so no sign and no letter ever reaches it.
TEST_CASE("Song information year field takes four digits only", "[ui][song-information]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordedSongInformationView recorded{SongMetadata{}};
    auto& year = recorded.find<juce::TextEditor>("song_information_year");

    year.insertTextAtCaret("-19a9");
    CHECK(year.getText() == "199");
    year.insertTextAtCaret("12");
    CHECK(year.getText() == "1991");
}

// Enter in a field is OK and Escape in a field is Cancel, so the dialog answers from the keyboard
// without reaching for a button.
TEST_CASE("Song information fields answer Enter and Escape", "[ui][song-information]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordedSongInformationView recorded{
        SongMetadata{.title = "Title", .artist = "", .album = "", .year = 0}
    };
    auto& title = recorded.find<juce::TextEditor>("song_information_title");
    auto& album = recorded.find<juce::TextEditor>("song_information_album");
    REQUIRE(title.onReturnKey != nullptr);
    REQUIRE(album.onEscapeKey != nullptr);

    title.onReturnKey();
    REQUIRE(recorded.submitted.size() == 1);
    CHECK(recorded.submitted.front().title == "Title");

    album.onEscapeKey();
    CHECK(recorded.cancels == 1);
    CHECK(recorded.submitted.size() == 1);
}

// The focus ring is a keyboard signal: neither OK nor Cancel takes focus from a click.
TEST_CASE(
    "Song information view buttons never take focus from a click",
    "[ui][song-information][focus-ring]")
{
    const juce::ScopedJuceInitialiser_GUI scoped_gui;
    RecordedSongInformationView recorded{SongMetadata{}};

    CHECK(testing::clickFocusedControls(recorded.view).empty());
}

} // namespace rock_hero::editor::ui
