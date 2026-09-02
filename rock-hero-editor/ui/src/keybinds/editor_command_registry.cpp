#include "keybinds/editor_command_registry.h"

#include <array>
#include <cstddef>

namespace rock_hero::editor::ui
{

namespace
{

// Chords register lowercase letters (the mapping set asserts on uppercase-without-shift) with
// commandModifier, which is the Ctrl key on Windows. KeyPress matching is exact on modifiers, so
// a Ctrl+Z entry does not fire on Ctrl+Shift+Z or Ctrl+Alt+Z; letter matching is
// case-insensitive, so uppercase key codes delivered by the OS still match.
[[nodiscard]] juce::KeyPress chord(int key_code, int modifier_flags = 0)
{
    return juce::KeyPress{key_code, juce::ModifierKeys{modifier_flags}, 0};
}

// Builds the one authoritative command table (ids, names, categories, default chords) that
// dispatch, keymap persistence, and the keymap UI all read.
[[nodiscard]] std::vector<EditorCommandSpec> makeRegistry()
{
    constexpr int command = juce::ModifierKeys::commandModifier;
    constexpr int shift = juce::ModifierKeys::shiftModifier;

    std::vector<EditorCommandSpec> registry;
    registry.push_back(
        EditorCommandSpec{
            .id = EditorCommandId::OpenProject,
            .name = "Open...",
            .category = "File",
            .default_keypresses = {chord('o', command)},
        });
    registry.push_back(
        EditorCommandSpec{
            .id = EditorCommandId::ImportSong,
            .name = "Import...",
            .category = "File",
            // Ctrl+Shift+O avoids the Ctrl+I italics muscle-memory collision (plan 46 tier A).
            .default_keypresses = {chord('o', command | shift)},
        });
    registry.push_back(
        EditorCommandSpec{
            .id = EditorCommandId::SaveProject,
            .name = "Save",
            .category = "File",
            .default_keypresses = {chord('s', command)},
        });
    registry.push_back(
        EditorCommandSpec{
            .id = EditorCommandId::SaveProjectAs,
            .name = "Save As...",
            .category = "File",
            .default_keypresses = {chord('s', command | shift)},
        });
    registry.push_back(
        EditorCommandSpec{
            .id = EditorCommandId::PublishSong,
            .name = "Publish...",
            .category = "File",
            .default_keypresses = {chord('p', command | shift)},
        });
    registry.push_back(
        EditorCommandSpec{
            .id = EditorCommandId::CloseProject,
            .name = "Close",
            .category = "File",
            .default_keypresses = {chord('w', command)},
        });
    registry.push_back(
        EditorCommandSpec{
            .id = EditorCommandId::ExitEditor,
            .name = "Exit",
            .category = "File",
            // Ctrl+Q is the app-owned quit chord; the OS separately owns Alt+F4, which needs no
            // registration.
            .default_keypresses = {chord('q', command)},
        });
    registry.push_back(
        EditorCommandSpec{
            .id = EditorCommandId::Undo,
            .name = "Undo",
            .category = "Edit",
            .default_keypresses = {chord('z', command)},
        });
    registry.push_back(
        EditorCommandSpec{
            .id = EditorCommandId::Redo,
            .name = "Redo",
            .category = "Edit",
            // Ctrl+Shift+Z is the DAW-convention alternative; alternatives per command are
            // first-class in the mapping set.
            .default_keypresses = {chord('y', command), chord('z', command | shift)},
        });
    registry.push_back(
        EditorCommandSpec{
            .id = EditorCommandId::ShowActions,
            .name = "Actions...",
            .category = "Edit",
            // Shift+/ types "?" — REAPER's actions-list key. The key code addresses the "/"
            // key, so the default only lands on layouts where "?" lives there; elsewhere the
            // user rebinds, which stores their own layout's chord.
            .default_keypresses = {chord('/', shift)},
        });
    registry.push_back(
        EditorCommandSpec{
            .id = EditorCommandId::PlayPause,
            .name = "Play/Pause",
            .category = "Transport",
            .default_keypresses = {chord(juce::KeyPress::spaceKey)},
        });
    registry.push_back(
        EditorCommandSpec{
            .id = EditorCommandId::ToggleWaveform,
            .name = "Show Waveform",
            .category = "View",
            .default_keypresses = {chord(juce::KeyPress::F5Key)},
        });
    registry.push_back(
        EditorCommandSpec{
            .id = EditorCommandId::ToggleUndoHistory,
            .name = "Undo History",
            .category = "View",
            .default_keypresses = {chord(juce::KeyPress::F8Key)},
        });
    registry.push_back(
        EditorCommandSpec{
            .id = EditorCommandId::TogglePreview3D,
            .name = "3D Preview",
            .category = "View",
            .default_keypresses = {chord(juce::KeyPress::F3Key)},
        });
    registry.push_back(
        EditorCommandSpec{
            .id = EditorCommandId::InsertToneChange,
            // "at Cursor" = the marker rule (E2): the armed caret when one exists, else the
            // transport position — the same "one position concept" play follows, so the insert
            // always lands where play would pick up.
            .name = "Insert Tone Change at Cursor",
            .category = "Tone",
            // Exact modifier matching gives the guard-against-Alt for free: Ctrl+Alt+T does not
            // match, keeping the Ctrl+Alt namespace with the fine-tier authoring composition.
            .default_keypresses = {chord('t', command)},
        });

    // The grammar verbs (plan 53 Phase 1b, total rebindability): one command per (chord, verb)
    // pair, so the precision/reach tiers are separate commands and every binding is individually
    // rebindable. The defaults below ARE the interaction grammar's modifier algebra; the algebra is
    // no longer enforced, only shipped.
    constexpr int alt = juce::ModifierKeys::altModifier;
    const auto add = [&registry](
                         EditorCommandId id,
                         const char* name,
                         const char* category,
                         std::vector<juce::KeyPress>
                             chords) {
        registry.push_back(
            EditorCommandSpec{
                .id = id,
                .name = name,
                .category = category,
                .default_keypresses = std::move(chords),
            });
    };

    // Navigation. The Ctrl aliases on the jump chords preserve the held-Ctrl navigation that
    // would otherwise read as broken.
    add(EditorCommandId::CaretStepLeft,
        "Step Caret Left",
        "Navigation",
        {chord(juce::KeyPress::leftKey)});
    add(EditorCommandId::CaretStepRight,
        "Step Caret Right",
        "Navigation",
        {chord(juce::KeyPress::rightKey)});
    add(EditorCommandId::CaretStepUp,
        "Step Caret Up",
        "Navigation",
        {chord(juce::KeyPress::upKey)});
    add(EditorCommandId::CaretStepDown,
        "Step Caret Down",
        "Navigation",
        {chord(juce::KeyPress::downKey)});
    add(EditorCommandId::CaretMeasureJumpLeft,
        "Jump Measure Left",
        "Navigation",
        {chord(juce::KeyPress::leftKey, command)});
    add(EditorCommandId::CaretMeasureJumpRight,
        "Jump Measure Right",
        "Navigation",
        {chord(juce::KeyPress::rightKey, command)});
    add(EditorCommandId::CaretJumpChartStart,
        "Jump to Chart Start",
        "Navigation",
        {chord(juce::KeyPress::homeKey), chord(juce::KeyPress::homeKey, command)});
    add(EditorCommandId::CaretJumpChartEnd,
        "Jump to Chart End",
        "Navigation",
        {chord(juce::KeyPress::endKey), chord(juce::KeyPress::endKey, command)});
    add(EditorCommandId::CaretJumpPreviousSection,
        "Jump to Previous Section",
        "Navigation",
        {chord(juce::KeyPress::pageUpKey), chord(juce::KeyPress::pageUpKey, command)});
    add(EditorCommandId::CaretJumpNextSection,
        "Jump to Next Section",
        "Navigation",
        {chord(juce::KeyPress::pageDownKey), chord(juce::KeyPress::pageDownKey, command)});

    // Selection.
    add(EditorCommandId::TimeSelectionExtendLeft,
        "Extend Time Selection Left",
        "Selection",
        {chord(juce::KeyPress::leftKey, shift)});
    add(EditorCommandId::TimeSelectionExtendRight,
        "Extend Time Selection Right",
        "Selection",
        {chord(juce::KeyPress::rightKey, shift)});
    add(EditorCommandId::TimeSelectionExtendMeasureLeft,
        "Extend Time Selection Left (Measure)",
        "Selection",
        {chord(juce::KeyPress::leftKey, command | shift)});
    add(EditorCommandId::TimeSelectionExtendMeasureRight,
        "Extend Time Selection Right (Measure)",
        "Selection",
        {chord(juce::KeyPress::rightKey, command | shift)});
    add(EditorCommandId::TimeSelectionExtendPreviousSection,
        "Extend Time Selection to Previous Section",
        "Selection",
        {chord(juce::KeyPress::pageUpKey, shift)});
    add(EditorCommandId::TimeSelectionExtendNextSection,
        "Extend Time Selection to Next Section",
        "Selection",
        {chord(juce::KeyPress::pageDownKey, shift)});
    add(EditorCommandId::TimeSelectionExtendChartStart,
        "Extend Time Selection to Chart Start",
        "Selection",
        {chord(juce::KeyPress::homeKey, shift)});
    add(EditorCommandId::TimeSelectionExtendChartEnd,
        "Extend Time Selection to Chart End",
        "Selection",
        {chord(juce::KeyPress::endKey, shift)});
    add(EditorCommandId::SelectionMoveLeft,
        "Move Selection Left",
        "Selection",
        {chord(juce::KeyPress::leftKey, alt)});
    add(EditorCommandId::SelectionMoveRight,
        "Move Selection Right",
        "Selection",
        {chord(juce::KeyPress::rightKey, alt)});
    add(EditorCommandId::SelectionMoveUp,
        "Move Selection Up",
        "Selection",
        {chord(juce::KeyPress::upKey, alt)});
    add(EditorCommandId::SelectionMoveDown,
        "Move Selection Down",
        "Selection",
        {chord(juce::KeyPress::downKey, alt)});
    add(EditorCommandId::SelectionDelete,
        "Delete Selection",
        "Selection",
        {chord(juce::KeyPress::deleteKey)});
    // Cancel/Clear sits with the selection verbs (its user-visible rungs disarm the caret and
    // clear the selection); its 0x1708 id stays in the authoring block — id blocks are
    // historical hints, the registry row owns the category.
    add(EditorCommandId::CancelDismiss,
        "Cancel / Clear",
        "Selection",
        {chord(juce::KeyPress::escapeKey)});

    // Authoring ("Editing" would collide with the Edit menu category in the dialog).
    add(EditorCommandId::SustainLengthen,
        "Lengthen Sustain",
        "Authoring",
        {chord(juce::KeyPress::rightKey, alt | shift)});
    add(EditorCommandId::SustainShorten,
        "Shorten Sustain",
        "Authoring",
        {chord(juce::KeyPress::leftKey, alt | shift)});
    add(EditorCommandId::FretShiftUp,
        "Shift Frets Up",
        "Authoring",
        {chord(juce::KeyPress::upKey, alt | shift)});
    add(EditorCommandId::FretShiftDown,
        "Shift Frets Down",
        "Authoring",
        {chord(juce::KeyPress::downKey, alt | shift)});
    add(EditorCommandId::NeutralInsert,
        "Insert Note / Point",
        "Authoring",
        {chord(juce::KeyPress::insertKey)});
    // The `Shift` plane, stated once for the technique block (signed 2026-08-25; supersedes the
    // separate sibling and collision readings as their superset — both stay true as instances).
    // The LETTER is the index; `Shift` is that letter's second slot. `Shift` is not a semantic
    // operator in this map — it is a disambiguator: the letter carries all the meaning, and
    // `Shift` says only which claimant of that letter you mean, with the plain key going to the
    // meaning a charter reaches for first. That is why a sibling (`Shift+H`) and a collision
    // (`Shift+X`) share the plane without sharing a kind — and never needed to. (`Shift+A` held a
    // third kind of claimant, the arpeggio hold, for part of 2026-08-25 before that verb re-signed
    // to plain `N`; the rule absorbed it without a third example, which is the rule working.)
    // The per-key comments below state only their own local facts;
    // the full map and its record live in `docs/plans/in-progress/keymap-matrix.md`.
    //
    // `Shift+X` is the X letter's second claimant rather than a claim that a scrape is a kind of
    // dead note; a scrape and a full mute are both unpitched noise, so the shared letter is a
    // real kinship. `P` stayed free for pop, so the mutes moved to `M`/`X` and left the scrape
    // here.
    add(EditorCommandId::ChartPickSlideToggle,
        "Toggle Pick Slide",
        "Authoring",
        {chord('x', shift)});
    // `L` matches the claim's direction, not Guitar Pro's technique letter: the claim is stored on
    // the arriving note and reaches backward to its predecessor, which is the shape of GP's "Tie
    // note" (L) — GP's H links the selected note FORWARD to the next, so an H habit here authored
    // an off-by-one link (the 2026-08-12 technique-letter amendment in keymap-matrix.md moved the
    // default and freed H for the harmonics). One key covers both motions because no direction is
    // stored: which way the connection runs is read back from the predecessor. Shift+L carries the
    // same verb extended with TRAVEL (walkthrough W10): its keyframe clause — severing a gesture
    // at a selected junction — is built; the tie/slide-link half is not.
    add(EditorCommandId::ChartLegatoToggle, "Toggle Legato", "Authoring", {chord('l')});
    add(EditorCommandId::ChartKeyframeDisconnect,
        "Disconnect Keyframe",
        "Authoring",
        {chord('l', shift)});
    // The charting marks already declare the tap family — one letter T, plate fill polarity as the
    // hand signature — so the keymap mirrors the visible structure: plain T toggles the right-hand
    // tap, Shift+T states the left-hand one. Ctrl stays the app-command plane
    // (Save/Open/tone change), which is why the earlier Ctrl+H default moved here. The two are not
    // the same KIND of verb, and the labels say so: the left-hand tap is a statement no toggle may
    // withdraw, so it has no "Toggle".
    add(EditorCommandId::ChartLeftTap, "Left-Hand Tap", "Authoring", {chord('t', shift)});
    add(EditorCommandId::ChartTapToggle, "Toggle Right-Hand Tap", "Authoring", {chord('t')});
    // `S` and `P` are the slap and pop PLATE letters — what the lane already draws — rather than
    // name letters, and a plate letter outranks a name letter, which is exactly why the scrape had
    // to take `Shift+X` above rather than either of these. All four — these two, the tap above and
    // the scrape — are toggles of one FIELD, so a press over a scope carrying another attack
    // replaces it in one entry.
    add(EditorCommandId::ChartSlapToggle, "Toggle Slap", "Authoring", {chord('s')});
    add(EditorCommandId::ChartPopToggle, "Toggle Pop", "Authoring", {chord('p')});
    // Two PLAIN letters rather than one letter with a `Shift` slot: the mutes are independent
    // properties a note may carry at once (a dead string inside a palm-muted chord), and each has
    // its own free letter, so neither has to claim the other's second slot. `M` is the palm; `X`
    // is what standard tab writes a dead note as and what both our surfaces draw, which is also
    // why the format field is named `dead`.
    add(EditorCommandId::ChartPalmMuteToggle, "Toggle Palm Mute", "Authoring", {chord('m')});
    add(EditorCommandId::ChartDeadNoteToggle, "Toggle Dead Note", "Authoring", {chord('x')});
    // Dynamics rather than technique, and two PLAIN letters rather than one letter with a `Shift`
    // slot. `A` was settled for the accent 2026-08-07; the ghost takes `G` (user 2026-08-18): the
    // two are opposite POLES of one axis, and each has its own first letter, so neither has to
    // claim the other's second slot. `Shift+A` held the arpeggio hold for part of 2026-08-25 and
    // is back to its heavy-accent RESERVATION, the hold having re-signed to plain `N` the same day
    // (see keymap-matrix.md). A heavy accent, if it ever lands, is plain `A` cycling the emphasis
    // axis rather than a chord.
    add(EditorCommandId::ChartAccentToggle, "Toggle Accent", "Authoring", {chord('a')});
    add(EditorCommandId::ChartGhostToggle, "Toggle Ghost Note", "Authoring", {chord('g')});
    // `V` is vibrato's own first letter and was settled 2026-08-12; `Shift+V` went LIVE with the
    // wide tier 2026-08-28, taking up the reservation it had held since the same day — the `Shift`
    // plane used exactly as intended, a magnitude variant of the plain key's own technique. That
    // also closes the recorded whammy-bar alternative on this chord: `W` keeps whammy outright.
    // The two are toggles of their own tiers rather than one cycling verb, so pressing either on
    // a scope already at the other tier simply replaces it. Tremolo could not have its own first
    // letter — `T` is the tap's — so it takes `R` for REPEAT (user 2026-08-19), which is what the
    // technique is: both surfaces already describe the teeth as "repeated attacks", so the
    // mnemonic states the rule rather than borrowing a spare letter.
    add(EditorCommandId::ChartVibratoToggle, "Toggle Vibrato", "Authoring", {chord('v')});
    add(EditorCommandId::ChartWideVibratoToggle,
        "Toggle Wide Vibrato",
        "Authoring",
        {chord('v', shift)});
    add(EditorCommandId::ChartTremoloToggle, "Toggle Tremolo", "Authoring", {chord('r')});
    // A BARE letter for a verb that is neither a technique nor a dynamic: `N` for "note type", the
    // conversion between a sounding note and a silently-held shape member (user 2026-08-25, after
    // a same-day `Shift+A` signing — a common charting verb earns a bare key). The letter was
    // verified unclaimed across the matrix and this registry, so it collides with nothing and
    // reserves nothing.
    add(EditorCommandId::ChartSilentHoldToggle, "Arpeggio Hold", "Authoring", {chord('n')});

    // Value entry: digit N types into the armed row's payload; the numpad chord is a
    // first-class alias of the same command.
    for (int digit = 0; digit <= 9; ++digit)
    {
        static constexpr std::array<const char*, 10> g_digit_names{
            "Type Digit 0",
            "Type Digit 1",
            "Type Digit 2",
            "Type Digit 3",
            "Type Digit 4",
            "Type Digit 5",
            "Type Digit 6",
            "Type Digit 7",
            "Type Digit 8",
            "Type Digit 9",
        };
        add(static_cast<EditorCommandId>(static_cast<int>(EditorCommandId::TypeDigit0) + digit),
            g_digit_names.at(static_cast<std::size_t>(digit)),
            "Value Entry",
            {chord('0' + digit), chord(juce::KeyPress::numberPad0 + digit)});
    }

    // Grid & zoom. The numpad add/subtract keys arrive as their character key codes on
    // Windows — doKeyDown has no VK_ADD/VK_SUBTRACT case and doKeyChar's numpad remap covers
    // digits only (juce_Windowing_windows.cpp:3141-3161, :3178-3195) — so the bare '+'/'-'
    // chords ARE the numpad bindings and juce::KeyPress::numberPadAdd/Subtract never match
    // (registering them would ship lying chips). Shift+'=' is the main-row plus; it and '+'
    // display as one grouped "+" chip. The off-grammar neighbors stay as convenience aliases
    // until something better claims them: '=' unshifted on the plus side, Shift+'-' ('_') on the
    // minus side — symmetric slop around the +/- grammar.
    add(EditorCommandId::GridFiner,
        "Grid Finer",
        "Grid & Zoom",
        {chord('=', shift), chord('+'), chord('=')});
    add(EditorCommandId::GridCoarser,
        "Grid Coarser",
        "Grid & Zoom",
        {chord('-'), chord('-', shift)});
    add(EditorCommandId::ZoomIn,
        "Zoom In",
        "Grid & Zoom",
        {chord('=', command | shift), chord('+', command), chord('=', command)});
    add(EditorCommandId::ZoomOut,
        "Zoom Out",
        "Grid & Zoom",
        {chord('-', command), chord('-', command | shift)});
    // Ctrl+G is the snap toggle rather than a grammar composition, because snap is a MODE and not
    // a per-gesture tier: it belongs with the grid's own commands, not in the Alt authoring plane.
    // Plain `G` is the ghost-note technique, and exact modifier matching keeps the two apart.
    add(EditorCommandId::ToggleGridSnap, "Grid Snap", "Grid & Zoom", {chord('g', command)});

    // TEMPORARY SIGHTING RIG (2026-09-01, delete when one value is settled): the provisional
    // STANDARD is the three-member accumulation minimum, and this key sights the previous
    // two-member picture beside it, live. F6 is the established sighting-key slot (the
    // actual-ring style toggle held it until 4876e379 retired it).
    add(EditorCommandId::ToggleSpanMinimumSighting,
        "Sight Two-Member Spans",
        "View",
        {chord(juce::KeyPress::F6Key)});

    return registry;
}

} // namespace

// Meyers-singleton table: built once on first use, immutable afterwards.
const std::vector<EditorCommandSpec>& editorCommandRegistry()
{
    static const std::vector<EditorCommandSpec> g_registry = makeRegistry();
    return g_registry;
}

// Lookup by id; nullptr for unknown or retired ids, which is what lets a stale persisted
// keymap drop them generically instead of needing per-id migration.
const EditorCommandSpec* findEditorCommandSpec(juce::CommandID command_id)
{
    for (const EditorCommandSpec& spec : editorCommandRegistry())
    {
        if (toJuceCommandId(spec.id) == command_id)
        {
            return &spec;
        }
    }
    return nullptr;
}

} // namespace rock_hero::editor::ui
