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
            .id = EditorCommandId::CycleActualRingLook,
            .name = "Actual Ring Mark (3D Preview)",
            .category = "View",
            // F1 means "show me the diagnostic" in the game already; the editor had it free.
            .default_keypresses = {chord(juce::KeyPress::F1Key)},
        });
    registry.push_back(
        EditorCommandSpec{
            .id = EditorCommandId::ToggleActualRingRevealStyle,
            // Named for what ticking it does, since it registers as a two-state toggle rather
            // than a cycle: ticked draws the rings as tails, unticked outlines them.
            .name = "Reveal Actual Rings as Tails",
            .category = "View",
            // The 2D reveal's sighting switch beside the 3D mark's on F1; F6 was the nearest free
            // function key (F5 waveform, F8 undo history, F3 preview).
            .default_keypresses = {chord(juce::KeyPress::F6Key)},
        });
    registry.push_back(
        EditorCommandSpec{
            .id = EditorCommandId::ToggleActualRingTailedNotes,
            .name = "Actual Ring Mark: Tailed Notes",
            .category = "View",
            // The rig's two filters sit on F1's own key under a modifier each: they narrow the
            // mark that key cycles, so they are read together and pressed together.
            .default_keypresses = {chord(juce::KeyPress::F1Key, shift)},
        });
    registry.push_back(
        EditorCommandSpec{
            .id = EditorCommandId::ToggleActualRingChordMembers,
            .name = "Actual Ring Mark: Chord Members",
            .category = "View",
            .default_keypresses = {chord(juce::KeyPress::F1Key, command)},
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
    add(EditorCommandId::SelectionMoveFineLeft,
        "Move Selection Left (Fine)",
        "Selection",
        {chord(juce::KeyPress::leftKey, command | alt)});
    add(EditorCommandId::SelectionMoveFineRight,
        "Move Selection Right (Fine)",
        "Selection",
        {chord(juce::KeyPress::rightKey, command | alt)});
    add(EditorCommandId::SelectionMoveFineUp,
        "Move Selection Up (Fine)",
        "Selection",
        {chord(juce::KeyPress::upKey, command | alt)});
    add(EditorCommandId::SelectionMoveFineDown,
        "Move Selection Down (Fine)",
        "Selection",
        {chord(juce::KeyPress::downKey, command | alt)});
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
    add(EditorCommandId::SustainLengthenFine,
        "Lengthen Sustain (Fine)",
        "Authoring",
        {chord(juce::KeyPress::rightKey, command | alt | shift)});
    add(EditorCommandId::SustainShortenFine,
        "Shorten Sustain (Fine)",
        "Authoring",
        {chord(juce::KeyPress::leftKey, command | alt | shift)});
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
    // Shift+X rides the X FAMILY rather than claiming a scrape is a kind of dead note: the Shift
    // plane resolves letter COLLISIONS as well as naming siblings, and a scrape and a full mute
    // are both unpitched noise, which makes the shared letter a real kinship. `P` stayed free for
    // pop, so the mutes moved to M/X and left the scrape here.
    add(EditorCommandId::ChartPickSlideToggle,
        "Toggle Pick Slide",
        "Authoring",
        {chord('x', shift)});
    // `L` matches the claim's direction, not Guitar Pro's technique letter: the claim is stored on
    // the arriving note and reaches backward to its predecessor, which is the shape of GP's "Tie
    // note" (L) — GP's H links the selected note FORWARD to the next, so an H habit here authored
    // an off-by-one link (the 2026-08-12 technique-letter amendment in keymap-matrix.md moved the
    // default and freed H for the harmonics). One key covers both motions because no direction is
    // stored: which way the connection runs is read back from the predecessor. Shift+L is reserved
    // for the tie/slide-link verb (walkthrough W10, unbuilt).
    add(EditorCommandId::ChartLegatoToggle, "Toggle Legato", "Authoring", {chord('l')});
    // The charting marks already declare the tap family — one letter T, plate fill polarity as the
    // hand signature — so the keymap mirrors the visible structure: plain T is reserved for the
    // right-hand tap, Shift+T states the left-hand one. Shift+letter is the typed family's sibling
    // modifier; Ctrl stays the app-command plane (Save/Open/tone change), which is why the earlier
    // Ctrl+H default moved here.
    add(EditorCommandId::ChartLeftTap, "Left-Hand Tap", "Authoring", {chord('t', shift)});
    // Two PLAIN letters rather than a sibling pair: the mutes are independent properties a note
    // may carry at once (a dead string inside a palm-muted chord), and `Shift` means "the related
    // sibling technique" everywhere else in this map, which a pair could only misstate. `M` is the
    // palm; `X` is what standard tab writes a dead note as and what both our surfaces draw, which
    // is also why the format field is named `dead`.
    add(EditorCommandId::ChartPalmMuteToggle, "Toggle Palm Mute", "Authoring", {chord('m')});
    add(EditorCommandId::ChartDeadNoteToggle, "Toggle Dead Note", "Authoring", {chord('x')});
    // Dynamics rather than technique, and two PLAIN letters rather than a `Shift` pair. `A` was
    // settled for the accent 2026-08-07; the ghost takes `G` (user 2026-08-18). The two are
    // opposite POLES of one axis rather than one being a variant of the other, and this map's
    // `Shift` plane states a MODIFIED form of the plain key's technique — so the pole reading
    // would have misused it, exactly as it would have for the two independent mutes on `M`/`X`.
    //
    // `Shift+A` is deliberately left unbound rather than merely unused: it is reserved for a
    // possible HEAVY accent, which is a magnitude variant of `A` and therefore precisely what the
    // plane is for. That is the same shape `Shift+V` already carries for a wide vibrato, and
    // spending the chord on the ghost would have closed it off.
    add(EditorCommandId::ChartAccentToggle, "Toggle Accent", "Authoring", {chord('a')});
    add(EditorCommandId::ChartGhostToggle, "Toggle Ghost Note", "Authoring", {chord('g')});
    // `V` is vibrato's own first letter and was settled 2026-08-12; `Shift+V` stays reserved for a
    // WIDE vibrato, the magnitude variant the Shift plane is for. Tremolo could not have its own
    // first letter — `T` is the tap's — so it takes `R` for REPEAT (user 2026-08-19), which is what
    // the technique is: both surfaces already describe the teeth as "repeated attacks", so the
    // mnemonic states the rule rather than borrowing a spare letter.
    add(EditorCommandId::ChartVibratoToggle, "Toggle Vibrato", "Authoring", {chord('v')});
    add(EditorCommandId::ChartTremoloToggle, "Toggle Tremolo", "Authoring", {chord('r')});

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
