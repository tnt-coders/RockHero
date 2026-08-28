#include <cstddef>
#include <optional>
#include <rock_hero/editor/core/testing/chart_editing_fixture.h>
#include <rock_hero/editor/core/testing/editor_controller_test_harness.h>
#include <utility>
#include <vector>

namespace rock_hero::editor::core
{

namespace
{

// The controller wired for the attack-family scenarios at the bottom of this file: the shared
// six-string chart opened through the normal route, the same shape the arpeggio-hold suite uses.
// The scenarios above it predate the struct and still spell their own setup out; nothing about
// them depends on the difference.
struct AttackToggleFixture
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

    // The shared six-string chart by default; a scenario needing a different stream passes its own.
    explicit AttackToggleFixture(common::core::Chart chart = makeTestChart())
    {
        controller.attachView(view);
        // Hoisted out of the assertion: a Catch2 macro mentions its expression a second time, and
        // the never-run mention reads a moved-from operand that CI's use-after-move check sees.
        const bool loaded =
            loadChartArrangement(controller, project_services, audio, {}, std::move(chart));
        REQUIRE(loaded);
    }
};

// A measure-2 chord ringing under a later note, so a stop stated on that note reaches a shape to
// belong to rather than being swept away as inert. The third note is left a plain pick: the tap the
// composition scenario needs is what the toggle under test authors.
[[nodiscard]] common::core::Chart makeShapeChart()
{
    common::core::Chart chart;
    chart.tuning.strings = {"E2", "A2", "D3", "G3", "B3", "E4"};
    chart.notes = {
        makeTestNote({.measure = 2, .beat = 1}, 1, 3, common::core::Fraction{2}),
        makeTestNote({.measure = 2, .beat = 1}, 2, 5, common::core::Fraction{2}),
        makeTestNote({.measure = 2, .beat = 2}, 3, 12, common::core::Fraction{1, 2}),
    };
    return chart;
}

} // namespace

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
    controller.onChartSustainAdjustRequested(1);
    const auto* chart = chartOrNull(controller);
    const common::core::ChartNote original = chart->notes[0];

    controller.onChartTechniqueToggleRequested(ChartTechnique::PickSlide);
    chart = chartOrNull(controller);
    const common::core::ChartNote& scrape = chart->notes[0];
    CHECK(scrape.attack == common::core::NoteAttack::PickSlide);
    CHECK(scrape.fret == original.fret);
    // The required terminal, which ends the ring by definition — there is no second coordinate
    // left to check it against.
    REQUIRE(scrape.slide_out.has_value());

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
    CHECK(chart->notes[0].keyframes.empty());
    CHECK(chart->notes[1].keyframes.empty());
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
    controller.onChartSustainAdjustRequested(1);
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
    chart_with_vibrato.notes[0].vibrato = common::core::VibratoState::Narrow;
    const bool loaded = loadChartArrangement(
        controller, project_services, audio, {}, std::move(chart_with_vibrato));
    REQUIRE(loaded);

    // Marquee both measure-2 chord members: the vibrato note plus a plain one.
    controller.onChartPointerDown(pointerEvent(20.0f, 160.0f));
    controller.onChartPointerDrag(pointerEvent(60.0f, 239.0f));
    controller.onChartPointerUp(pointerEvent(60.0f, 239.0f));

    controller.onChartTechniqueToggleRequested(ChartTechnique::Dead);
    const auto* chart = chartOrNull(controller);
    CHECK_FALSE(chart->notes[0].dead);
    CHECK(common::core::isShaking(chart->notes[0].vibrato));
    CHECK(chart->notes[1].dead);
}

// The two vibrato verbs are toggles of their OWN tier on one width axis, which is the whole of
// their law: `V` clears a scope already at the ordinary tier and `Shift+V` a scope already at the
// wide one, while either pressed on the other tier is an ordinary set that REPLACES it. Nothing
// cycles, and nothing clears on the way through — a replacement that went off first would show a
// still note for one entry and cost two undos to walk back.
TEST_CASE("EditorController toggles each vibrato tier and replaces the other", "[core][chart]")
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

    // A still note takes the ordinary tier.
    controller.onChartTechniqueToggleRequested(ChartTechnique::Vibrato);
    const auto* chart = chartOrNull(controller);
    CHECK(chart->notes[0].vibrato == common::core::VibratoState::Narrow);

    // Shift+V over that same note REPLACES the tier: one entry, and the note never passes through
    // not shaking. A history move first, so this press runs the verb's law rather than reversing
    // the press above through the toggle window.
    controller.onUndoRequested();
    controller.onRedoRequested();
    controller.onChartTechniqueToggleRequested(ChartTechnique::WideVibrato);
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].vibrato == common::core::VibratoState::Wide);
    // One entry, so ONE undo walks the replacement back to the ordinary tier rather than to a
    // still note.
    controller.onUndoRequested();
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].vibrato == common::core::VibratoState::Narrow);
    controller.onRedoRequested();

    // Each key clears only its OWN tier: V over a wide note is a replacement, not a clear.
    controller.onChartTechniqueToggleRequested(ChartTechnique::Vibrato);
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].vibrato == common::core::VibratoState::Narrow);

    // ...and pressed again on the tier it names, it clears.
    controller.onUndoRequested();
    controller.onRedoRequested();
    controller.onChartTechniqueToggleRequested(ChartTechnique::Vibrato);
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].vibrato == common::core::VibratoState::Off);
}

// Uniform scope, unchanged by the axis: anything short of "every anchor already stands at this
// tier" means SET, so a mixed selection levels onto the pressed tier instead of clearing the
// notes that already carry it. The discriminating case is a selection where one note is ALREADY
// wide — an any-of reading would call that selection carried and clear the lot.
TEST_CASE("EditorController vibrato toggle levels a mixed selection", "[core][chart]")
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
    common::core::Chart mixed = makeTestChart();
    mixed.notes[0].vibrato = common::core::VibratoState::Wide;
    // Hoisted out of the assertion: a Catch2 macro mentions its expression a second time, and the
    // never-run mention reads a moved-from operand that CI's use-after-move check does see.
    const bool loaded =
        loadChartArrangement(controller, project_services, audio, {}, std::move(mixed));
    REQUIRE(loaded);

    // Marquee both measure-2 chord members: a wide note plus a still one.
    controller.onChartPointerDown(pointerEvent(20.0f, 160.0f));
    controller.onChartPointerDrag(pointerEvent(60.0f, 239.0f));
    controller.onChartPointerUp(pointerEvent(60.0f, 239.0f));

    controller.onChartTechniqueToggleRequested(ChartTechnique::WideVibrato);
    const auto* chart = chartOrNull(controller);
    CHECK(chart->notes[0].vibrato == common::core::VibratoState::Wide);
    CHECK(chart->notes[1].vibrato == common::core::VibratoState::Wide);

    // Now every anchor stands at the tier, so the same press clears the whole selection.
    controller.onUndoRequested();
    controller.onRedoRequested();
    controller.onChartTechniqueToggleRequested(ChartTechnique::WideVibrato);
    chart = chartOrNull(controller);
    CHECK(chart->notes[0].vibrato == common::core::VibratoState::Off);
    CHECK(chart->notes[1].vibrato == common::core::VibratoState::Off);
}

// The right-hand tap joins the shared toggle window like every other row: the second press inside
// it REVERSES the first entry rather than authoring a second, which is what lets the pair leave the
// note field-for-field as it stood and leave no history entry behind.
TEST_CASE("EditorController toggles the right-hand tap with exact restoration", "[core][chart]")
{
    AttackToggleFixture fixture;

    // The string-1 note at measure 2 beat 1 carries fret 3, so the strike has somewhere to land.
    // The sustain adjust first gives it a real tail, so the round trip is field-exact.
    click(fixture.controller, 40.0f, 220.0f);
    fixture.controller.onChartSustainAdjustRequested(1);
    const common::core::ChartNote original = chartOrNull(fixture.controller)->notes[0];

    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Tap);
    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[0].attack == common::core::NoteAttack::Tap);
    CHECK(chart->notes[0].fret == original.fret);

    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Tap);
    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[0] == original);

    // No trace: the next undo reaches past the pair to the sustain adjust that preceded it.
    fixture.controller.onUndoRequested();
    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[0].sustain == g_fixture_sustain);
}

// Uniform scope over a chord, and the clear that follows once every member carries it. Pins the
// all-of decision an any-of regression would silently invert, on a whole chord in one entry.
TEST_CASE("EditorController tap toggle levels a chord and then clears it", "[core][chart]")
{
    AttackToggleFixture fixture;

    // One member becomes a tap first, so the marquee below is mixed.
    click(fixture.controller, 40.0f, 220.0f);
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Tap);

    // Marquee both measure-2 chord members: a tap plus a plain note.
    fixture.controller.onChartPointerDown(pointerEvent(20.0f, 160.0f));
    fixture.controller.onChartPointerDrag(pointerEvent(60.0f, 239.0f));
    fixture.controller.onChartPointerUp(pointerEvent(60.0f, 239.0f));

    // Anything short of "all of them already" means SET, so the press levels the whole chord.
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Tap);
    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[0].attack == common::core::NoteAttack::Tap);
    CHECK(chart->notes[1].attack == common::core::NoteAttack::Tap);

    // A history move COMMITS that entry and closes the toggle window, so the press below runs the
    // verb's law rather than reversing the one above.
    fixture.controller.onUndoRequested();
    fixture.controller.onRedoRequested();
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Tap);
    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[0].attack == common::core::NoteAttack::Pick);
    CHECK(chart->notes[1].attack == common::core::NoteAttack::Pick);
}

// The four attack verbs share ONE field, so each press states its own attack and replaces whatever
// else stood there — the vibrato pair's law one level up. Nothing cycles, and nothing passes
// through the plain pick on the way, which a clear-then-set would cost two undos to walk back.
TEST_CASE("EditorController slap and pop replace each other in one entry", "[core][chart]")
{
    AttackToggleFixture fixture;

    click(fixture.controller, 40.0f, 220.0f);

    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Slap);
    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[0].attack == common::core::NoteAttack::Slap);

    // P over a slapped note is an ordinary SET. A history move first, so this press runs the law
    // rather than reversing the one above through the toggle window.
    fixture.controller.onUndoRequested();
    fixture.controller.onRedoRequested();
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Pop);
    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[0].attack == common::core::NoteAttack::Pop);

    // One entry, so ONE undo walks the replacement back to the slap rather than to a plain pick.
    fixture.controller.onUndoRequested();
    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[0].attack == common::core::NoteAttack::Slap);
    fixture.controller.onRedoRequested();

    // ...and pressed on the attack it names, P clears back to the plain pick.
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Pop);
    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[0].attack == common::core::NoteAttack::Pick);
}

// The typing family's gate, both halves: no scope means a silent no-op, and the armed caret is the
// scope when nothing was clicked, because arming re-derives the selection from what sits under it.
TEST_CASE(
    "EditorController attack toggle is inert with no scope and acts at the caret", "[core][chart]")
{
    AttackToggleFixture fixture;

    // An empty slot at measure 4 beat 1 (6.0s to x = 120): the click arms the caret there, and the
    // caret's re-derivation leaves the selection empty since nothing is there to select.
    click(fixture.controller, 120.0f, 220.0f);
    const EditorViewState* const state = stateOrNull(fixture.view.last_state);
    REQUIRE(state != nullptr);
    REQUIRE(state->chart_edit.selected_notes.empty());

    // No operand, so the press changes nothing and inserts nothing — it is not an error either.
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Slap);
    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    REQUIRE(chart->notes.size() == 3);
    CHECK(chart->notes[0].attack == common::core::NoteAttack::Pick);
    CHECK(chart->notes[1].attack == common::core::NoteAttack::Pick);
    CHECK(chart->notes[2].attack == common::core::NoteAttack::Pick);

    // Stepping the caret a measure back lands it ON the sustained string-1 note at measure 3, and
    // the armed-caret invariant makes that note the selection — which is the whole of the caret
    // fallback for a verb whose operand is a note.
    fixture.controller.onChartCaretStepRequested(ChartStepDirection::Left, true);
    CHECK(state->chart_edit.selected_notes == std::vector<std::size_t>{2});

    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Slap);
    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[2].attack == common::core::NoteAttack::Slap);
}

// Mixed validity, decided by the one rule authority rather than by this row: a tap strikes from
// nowhere and needs somewhere to land (E4), so the open string with no node is skipped while the
// fretted member takes it. The slap is the control — it strikes the string itself, so the same
// open string is an ordinary slap.
TEST_CASE("EditorController tap toggle skips a note with nothing to strike", "[core][chart]")
{
    common::core::Chart open_string = makeTestChart();
    open_string.notes[0].fret = 0;
    AttackToggleFixture fixture{std::move(open_string)};

    // Marquee both measure-2 chord members: the open string plus the fret-5 note.
    fixture.controller.onChartPointerDown(pointerEvent(20.0f, 160.0f));
    fixture.controller.onChartPointerDrag(pointerEvent(60.0f, 239.0f));
    fixture.controller.onChartPointerUp(pointerEvent(60.0f, 239.0f));

    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Tap);
    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[0].attack == common::core::NoteAttack::Pick);
    CHECK(chart->notes[0].fret == 0);
    CHECK(chart->notes[1].attack == common::core::NoteAttack::Tap);

    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Slap);
    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[0].attack == common::core::NoteAttack::Slap);
    CHECK(chart->notes[1].attack == common::core::NoteAttack::Slap);
}

// Converting AWAY from the scrape, through the same planner the pick-slide row already runs: the
// travel was gesture geometry, and as a tap's own fret statement it would be a fiction, so the
// path and its required terminal go in the same entry.
TEST_CASE("EditorController tap conversion drops the scrape's path", "[core][chart]")
{
    AttackToggleFixture fixture;

    click(fixture.controller, 40.0f, 220.0f);
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::PickSlide);
    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    REQUIRE(chart->notes[0].slide_out.has_value());

    // A history move commits the scrape and closes its window, so T below is a fresh conversion
    // rather than that press's reversal.
    fixture.controller.onUndoRequested();
    fixture.controller.onRedoRequested();

    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Tap);
    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[0].attack == common::core::NoteAttack::Tap);
    CHECK_FALSE(chart->notes[0].slide_out.has_value());
    CHECK(chart->notes[0].keyframes.empty());
}

// A stop the hand takes with no stroke at all has no ring to state, and the ring rule refuses a
// struck note without one — so an attack written onto a silent hold is the fixpoint's refusal and
// the note is skipped. Not this row's rule: the live sibling is inert over the same stop.
TEST_CASE("EditorController attack toggles skip a silently held stop", "[core][chart]")
{
    AttackToggleFixture fixture;

    // The string-1 member becomes a silent hold, and stays the selection under the armed caret.
    click(fixture.controller, 40.0f, 220.0f);
    fixture.controller.onChartSilentHoldToggleRequested();
    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    REQUIRE(chart->notes[0].attack == common::core::NoteAttack::None);

    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Slap);
    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[0].attack == common::core::NoteAttack::None);
    CHECK(chart->notes[0].sustain == common::core::Fraction{});

    fixture.controller.onChartLeftTapRequested();
    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[0].attack == common::core::NoteAttack::None);
}

// The attack and the fretting hand's full mute are independent statements, so a slapped dead note
// carries both: the picking hand's stroke and the string damped into an unpitched click.
TEST_CASE("EditorController composes the slap with the dead note", "[core][chart]")
{
    AttackToggleFixture fixture;

    click(fixture.controller, 40.0f, 220.0f);
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Slap);
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Dead);

    const common::core::Chart* const chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[0].attack == common::core::NoteAttack::Slap);
    CHECK(chart->notes[0].dead);
}

// The same independence on the emphasis axis: a popped note is loud by nature and accents anyway,
// which is exactly why the scrape row leaves emphasis alone too.
TEST_CASE("EditorController composes the pop with the accent", "[core][chart]")
{
    AttackToggleFixture fixture;

    click(fixture.controller, 40.0f, 220.0f);
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Pop);
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Accent);

    const common::core::Chart* const chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    CHECK(chart->notes[0].attack == common::core::NoteAttack::Pop);
    CHECK(chart->notes[0].emphasis == common::core::NoteEmphasis::Accent);
}

// The arpeggio hold's fourth case reads the ATTACK, so a tap this verb just authored reaches it
// with no case of its own: the onset belongs to the picking hand, so the press states a held stop
// under it instead of converting away a sound the charter wrote.
TEST_CASE("The arpeggio hold states a stop under a toggled tap", "[core][chart]")
{
    AttackToggleFixture fixture{makeShapeChart()};

    // The string-3 note at measure 2 beat 2 (2.5s to x = 50, string 3 to y = 140).
    click(fixture.controller, 50.0f, 140.0f);
    fixture.controller.onChartTechniqueToggleRequested(ChartTechnique::Tap);
    const common::core::Chart* chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    REQUIRE(chart->notes[2].attack == common::core::NoteAttack::Tap);

    fixture.controller.onChartSilentHoldToggleRequested();
    chart = chartOrNull(fixture.controller);
    REQUIRE(chart != nullptr);
    REQUIRE(chart->notes.size() == 3);
    CHECK(chart->notes[2].attack == common::core::NoteAttack::Tap);
    CHECK(chart->notes[2].fret == 12);
    CHECK(chart->notes[2].held == std::optional{0});
}

} // namespace rock_hero::editor::core
