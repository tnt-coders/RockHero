#include <rock_hero/editor/core/testing/chart_editing_fixture.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>

namespace rock_hero::editor::core
{

// The pick-slide toggle authors a scrape from a plain note and back within the session. The
// second press lands inside the toggle window (D14 ruling 4, extended to the scrape 2026-08-18),
// so it REVERSES the first press's entry exactly rather than authoring a second one — which is
// what lets it restore things the plain clear law could never put back, like a sustain the
// default grew or a glide a conversion consumed.
TEST_CASE("EditorController toggles pick slides with exact restoration", "[core][chart]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);
    REQUIRE(loadChartArrangement(controller, project_services, audio));

    // Select the string-1 note and give it a real tail first, so the toggle round trip is
    // field-exact (a zero sustain would legitimately gain the minimum gesture window).
    click(controller, 40.0f, 220.0f);
    controller.onChartSustainAdjustRequested(1, false);
    const auto* chart = chartOrNull(controller);
    const common::core::ChartNote original = chart->notes[0];

    controller.onChartTechniqueToggleRequested(ChartTechnique::PickSlide);
    chart = chartOrNull(controller);
    const common::core::ChartNote& scrape = chart->notes[0];
    CHECK(scrape.attack == common::core::NoteAttack::PickSlide);
    CHECK(scrape.fret == original.fret);
    REQUIRE(scrape.slide_out.has_value());
    if (scrape.slide_out.has_value())
    {
        CHECK(scrape.slide_out->offset == scrape.sustain);
    }

    // Toggling back inside the window restores the note field-for-field.
    controller.onChartTechniqueToggleRequested(ChartTechnique::PickSlide);
    chart = chartOrNull(controller);
    CHECK(chart->notes[0] == original);

    // And it leaves NO TRACE in the history: the reversal consumed the scrape's own entry, so the
    // next undo reaches past it to the sustain adjust that preceded the pair. That is the whole
    // difference between a true toggle and a do/undo pair.
    controller.onUndoRequested();
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].attack == common::core::NoteAttack::Pick);
    CHECK_FALSE(chart->notes[0].slide_out.has_value());
    CHECK(chart->notes[0].sustain == g_fixture_sustain);
}

// Uniform scope on a mixed selection: any plain note present makes the whole selection become
// scrapes; only an all-scrape selection reverts. Pins the all-of decision an any-of regression
// would silently invert.
TEST_CASE("EditorController pick-slide toggle applies uniform scope", "[core][chart]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);
    REQUIRE(loadChartArrangement(controller, project_services, audio));

    // One note becomes a scrape first, so the marquee selection below is mixed.
    click(controller, 40.0f, 220.0f);
    controller.onChartTechniqueToggleRequested(ChartTechnique::PickSlide);

    // Marquee both measure-1 chord members: a scrape plus a plain note.
    controller.onChartPointerDown(pointerEvent(20.0f, 160.0f));
    controller.onChartPointerDrag(pointerEvent(60.0f, 239.0f));
    controller.onChartPointerUp(pointerEvent(60.0f, 239.0f));

    controller.onChartTechniqueToggleRequested(ChartTechnique::PickSlide);
    const auto* chart = chartOrNull(controller);
    CHECK(chart->notes[0].attack == common::core::NoteAttack::PickSlide);
    CHECK(chart->notes[1].attack == common::core::NoteAttack::PickSlide);

    // A history move COMMITS the entry above and closes the toggle window, and undo-then-redo
    // lands the chart back in this exact state - so the press below exercises the uniform-scope
    // law rather than a reversal (the window itself is pinned in the round-trip case above).
    controller.onUndoRequested();
    controller.onRedoRequested();

    // Now all-scrape: the same intent reverts the whole selection in one entry.
    controller.onChartTechniqueToggleRequested(ChartTechnique::PickSlide);
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].attack == common::core::NoteAttack::Pick);
    CHECK(chart->notes[1].attack == common::core::NoteAttack::Pick);
    CHECK(chart->notes[0].slides.empty());
    CHECK(chart->notes[1].slides.empty());
    CHECK_FALSE(chart->notes[0].slide_out.has_value());
    CHECK_FALSE(chart->notes[1].slide_out.has_value());
}

// The mutes join the shared toggle window rather than owning a law of their own: a second press
// while the selection and the history top still prove the first was this verb's reverses it
// exactly and leaves NO history entry behind, so the next undo reaches past the pair.
TEST_CASE("EditorController toggles a palm mute with exact restoration", "[core][chart]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);
    REQUIRE(loadChartArrangement(controller, project_services, audio));

    // A preceding entry so the undo below has somewhere to land past the toggle pair.
    click(controller, 40.0f, 220.0f);
    controller.onChartSustainAdjustRequested(1, false);
    const auto* chart = chartOrNull(controller);
    const common::core::ChartNote original = chart->notes[0];

    controller.onChartTechniqueToggleRequested(ChartTechnique::PalmMute);
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].palm_mute);
    CHECK_FALSE(chart->notes[0].dead);

    controller.onChartTechniqueToggleRequested(ChartTechnique::PalmMute);
    chart = chartOrNull(controller);
    CHECK(chart->notes[0] == original);

    controller.onUndoRequested();
    chart = chartOrNull(controller);
    CHECK_FALSE(chart->notes[0].palm_mute);
    CHECK(chart->notes[0].sustain == g_fixture_sustain);
}

// The both-muted note the two-flag model exists for, authored the way the user authors it: press
// one verb, then the other. Each writes only its own field, so the second press does not disturb
// what the first wrote, and a third clears only the flag it owns.
TEST_CASE("EditorController sets both mutes on one note", "[core][chart]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);
    REQUIRE(loadChartArrangement(controller, project_services, audio));

    click(controller, 40.0f, 220.0f);
    controller.onChartTechniqueToggleRequested(ChartTechnique::PalmMute);
    controller.onChartTechniqueToggleRequested(ChartTechnique::Dead);
    const auto* chart = chartOrNull(controller);
    CHECK(chart->notes[0].palm_mute);
    CHECK(chart->notes[0].dead);

    // The dead press was the last edit, so it closed the palm window: this press means the palm
    // verb's ordinary law, which clears the palm flag and leaves the X standing.
    controller.onChartTechniqueToggleRequested(ChartTechnique::PalmMute);
    chart = chartOrNull(controller);
    CHECK_FALSE(chart->notes[0].palm_mute);
    CHECK(chart->notes[0].dead);
}

// The emphasis verbs join the same toggle window, and for them it does something the mutes never
// needed it for. A mute is its own inverse — clearing the flag restores what was there. An axis
// is NOT: the accent overwrote the ghost, and "off" for the accent verb is Normal, so only an
// exact reversal can put the ghost back. This pins that the second press restores the GHOST
// rather than settling at Normal.
TEST_CASE("EditorController accent toggle restores an overwritten ghost", "[core][chart]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);
    REQUIRE(loadChartArrangement(controller, project_services, audio));

    click(controller, 40.0f, 220.0f);
    controller.onChartTechniqueToggleRequested(ChartTechnique::Ghost);
    // A history move commits that entry and closes the GHOST's window, so the accent press below
    // is an ordinary press rather than a reversal of it.
    controller.onUndoRequested();
    controller.onRedoRequested();
    const auto* chart = chartOrNull(controller);
    REQUIRE(common::core::isGhosted(chart->notes[0].emphasis));
    const common::core::ChartNote ghosted = chart->notes[0];

    controller.onChartTechniqueToggleRequested(ChartTechnique::Accent);
    chart = chartOrNull(controller);
    CHECK(common::core::isAccented(chart->notes[0].emphasis));
    CHECK_FALSE(common::core::isGhosted(chart->notes[0].emphasis));

    controller.onChartTechniqueToggleRequested(ChartTechnique::Accent);
    chart = chartOrNull(controller);
    CHECK(chart->notes[0] == ghosted);

    // And the pair left no entry of its own, so undo reaches past it to the ghost press.
    controller.onUndoRequested();
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].emphasis == common::core::NoteEmphasis::Normal);
}

// Uniform scope over the axis, the same law the mutes obey: any selected note not already at this
// end makes the press SET it on the whole selection; only a selection wholly at it returns to
// Normal.
TEST_CASE("EditorController emphasis toggle applies uniform scope", "[core][chart]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);
    REQUIRE(loadChartArrangement(controller, project_services, audio));

    // One note is accented first, so the marquee selection below is mixed.
    click(controller, 40.0f, 220.0f);
    controller.onChartTechniqueToggleRequested(ChartTechnique::Accent);

    // Marquee both measure-2 chord members: an accented note plus a plain one.
    controller.onChartPointerDown(pointerEvent(20.0f, 160.0f));
    controller.onChartPointerDrag(pointerEvent(60.0f, 239.0f));
    controller.onChartPointerUp(pointerEvent(60.0f, 239.0f));

    controller.onChartTechniqueToggleRequested(ChartTechnique::Accent);
    const auto* chart = chartOrNull(controller);
    CHECK(common::core::isAccented(chart->notes[0].emphasis));
    CHECK(common::core::isAccented(chart->notes[1].emphasis));

    controller.onUndoRequested();
    controller.onRedoRequested();

    controller.onChartTechniqueToggleRequested(ChartTechnique::Accent);
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].emphasis == common::core::NoteEmphasis::Normal);
    CHECK(chart->notes[1].emphasis == common::core::NoteEmphasis::Normal);
}

// Uniform scope: any selected note lacking the mute makes the press SET it on the whole
// selection; only an all-muted selection clears. Pins the all-of decision an any-of regression
// would silently invert.
TEST_CASE("EditorController mute toggle applies uniform scope", "[core][chart]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);
    REQUIRE(loadChartArrangement(controller, project_services, audio));

    // One note is muted first, so the marquee selection below is mixed.
    click(controller, 40.0f, 220.0f);
    controller.onChartTechniqueToggleRequested(ChartTechnique::PalmMute);

    // Marquee both measure-2 chord members: a muted note plus a plain one.
    controller.onChartPointerDown(pointerEvent(20.0f, 160.0f));
    controller.onChartPointerDrag(pointerEvent(60.0f, 239.0f));
    controller.onChartPointerUp(pointerEvent(60.0f, 239.0f));

    controller.onChartTechniqueToggleRequested(ChartTechnique::PalmMute);
    const auto* chart = chartOrNull(controller);
    CHECK(chart->notes[0].palm_mute);
    CHECK(chart->notes[1].palm_mute);

    // A history move COMMITS that entry and closes the toggle window, and undo-then-redo lands
    // the chart back in this exact state - so the press below exercises the uniform-scope law
    // rather than a reversal.
    controller.onUndoRequested();
    controller.onRedoRequested();

    controller.onChartTechniqueToggleRequested(ChartTechnique::PalmMute);
    chart = chartOrNull(controller);
    CHECK_FALSE(chart->notes[0].palm_mute);
    CHECK_FALSE(chart->notes[1].palm_mute);
}

// The eligible-subset skip reaching the user: a dead note sounds no pitch, so X over a selection
// holding a vibrato note mutes what it can and leaves that note exactly as it was, rather than
// refusing the whole edit for every note in the selection.
TEST_CASE("EditorController dead-note toggle skips a vibrato note", "[core][chart]")
{
    FakeTransport transport;
    ConfigurableSongAudio audio;
    FakeProjectServices project_services;
    EditorController controller{
        audioPorts(transport, audio),
        defaultControllerServices(),
        noopExitFunction(),
        EditorController::ProjectOperations{
            .open_function = project_services.openFunction(),
        }
    };
    FakeEditorView view;
    controller.attachView(view);
    common::core::Chart chart_with_vibrato = makeTestChart();
    chart_with_vibrato.notes[0].vibrato = true;
    REQUIRE(loadChartArrangement(
        controller, project_services, audio, {}, std::move(chart_with_vibrato)));

    // Marquee both measure-2 chord members: the vibrato note plus a plain one.
    controller.onChartPointerDown(pointerEvent(20.0f, 160.0f));
    controller.onChartPointerDrag(pointerEvent(60.0f, 239.0f));
    controller.onChartPointerUp(pointerEvent(60.0f, 239.0f));

    controller.onChartTechniqueToggleRequested(ChartTechnique::Dead);
    const auto* chart = chartOrNull(controller);
    CHECK_FALSE(chart->notes[0].dead);
    CHECK(chart->notes[0].vibrato);
    CHECK(chart->notes[1].dead);
}

} // namespace rock_hero::editor::core
