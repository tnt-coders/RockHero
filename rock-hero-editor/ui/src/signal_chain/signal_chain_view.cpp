#include "signal_chain_view.h"

#include "shared/editor_theme.h"
#include "signal_chain/insert_slot_view.h"
#include "signal_chain/plugin_drag.h"
#include "signal_chain/plugin_tile_view.h"
#include "signal_chain/signal_chain_view_metrics.h"

#include <algorithm>
#include <optional>
#include <rock_hero/common/audio/plugin/plugin_chain_limits.h>
#include <rock_hero/common/audio/shared/gain.h>
#include <string>
#include <utility>

namespace rock_hero::editor::ui
{

using core::SignalChainBlockPlacement;

namespace
{

constexpr int g_panel_inset{8};
constexpr int g_header_height{34};
constexpr int g_signal_path_padding{20};
constexpr int g_signal_path_min_cell_width{128};
constexpr int g_signal_preview_animation_ms{250};
constexpr double g_signal_preview_animation_start_speed{1.0};
constexpr double g_signal_preview_animation_end_speed{0.0};
constexpr std::size_t g_signal_path_min_block_count{common::audio::g_max_signal_chain_plugins};
constexpr int g_output_gain_width{72};
constexpr int g_gain_slider_width{32};
constexpr int g_gain_meter_width{28};
constexpr int g_gain_meter_gap{2};
constexpr int g_gain_meter_vertical_inset{2};
constexpr int g_gain_value_height{20};
constexpr int g_input_control_width{72};
constexpr int g_calibrate_button_height{26};
constexpr int g_output_gain_visual_width{
    g_gain_slider_width + g_gain_meter_gap + g_gain_meter_width
};
const juce::Colour g_panel_border{juce::Colours::black.withAlpha(0.45f)};
const juce::Colour g_path_background{juce::Colour{0xff101318}};
const juce::Colour g_signal_path_line{juce::Colours::white.withAlpha(0.82f)};
const juce::Colour g_signal_path_slot_marker{juce::Colours::white.withAlpha(0.12f)};

// Width of the break the signal line leaves at each fixed cell centre, sized to clear the 28 px
// "+" insert affordance (insert_slot_view.cpp) with a little air so the glyph reads against the
// dark path surface instead of the bright line. Plugin tiles are far wider than the break, so
// occupied cells cover their gap entirely.
constexpr int g_signal_path_node_gap{34};

// Computes the scrollable content width for the signal path. The block count comes from
// SignalChainBlockLayout, the single owner of the minimum-block-count rule.
[[nodiscard]] int chainContentWidth(std::size_t block_count, int viewport_width) noexcept
{
    const int natural_width = (static_cast<int>(block_count) * g_signal_path_min_cell_width) +
                              (g_signal_path_padding * 2);
    return std::max(viewport_width, natural_width);
}

// Returns the inner path bounds shared by painting and child layout.
[[nodiscard]] juce::Rectangle<int> signalPathArea(juce::Rectangle<int> bounds)
{
    return bounds.reduced(g_signal_path_padding, 0);
}

// Computes one fixed visual block cell by index.
[[nodiscard]] juce::Rectangle<int> blockCellBounds(
    juce::Rectangle<int> path_area, std::size_t cell_index, std::size_t block_count)
{
    const int cell_width = std::max(1, path_area.getWidth() / static_cast<int>(block_count));
    return path_area.withX(path_area.getX() + (static_cast<int>(cell_index) * cell_width))
        .withWidth(cell_width);
}

// Places one plugin block-plus-label view inside a fixed signal-path cell.
[[nodiscard]] juce::Rectangle<int> pluginBlockBounds(
    juce::Rectangle<int> path_area, std::size_t block_index, std::size_t block_count)
{
    const juce::Rectangle<int> cell = blockCellBounds(path_area, block_index, block_count);
    const int view_width = std::min(g_signal_plugin_view_width, std::max(1, cell.getWidth() - 10));
    const int view_height =
        std::min(g_signal_plugin_view_height, std::max(1, path_area.getHeight() - 6));
    auto bounds = cell.withSizeKeepingCentre(view_width, view_height);

    // The caption extends below the block, so align the block itself to the path center instead
    // of centering the whole component.
    const int ideal_top = path_area.getCentreY() - (g_signal_block_height / 2);
    const int min_top = path_area.getY();
    const int max_top = std::max(min_top, path_area.getBottom() - view_height);
    bounds.setY(std::clamp(ideal_top, min_top, max_top));
    return bounds;
}

// Keeps JUCE's normal editable slider textbox while shifting only the vertical track left enough
// to sit as a compact pair beside the output meter.
class OutputGainSliderLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    // Reuses the stock drawing while aligning the track with the meter column and the textbox
    // with the bottom control row.
    juce::Slider::SliderLayout getSliderLayout(juce::Slider& slider) override
    {
        auto layout = juce::LookAndFeel_V4::getSliderLayout(slider);
        if (slider.getSliderStyle() == juce::Slider::LinearVertical &&
            slider.getTextBoxPosition() == juce::Slider::TextBoxBelow &&
            slider.getWidth() >= g_output_gain_visual_width)
        {
            layout.sliderBounds.setX((slider.getWidth() - g_output_gain_visual_width) / 2);
            layout.sliderBounds.setWidth(g_gain_slider_width);

            const int thumb_radius = getSliderThumbRadius(slider);
            const int meter_top = g_gain_meter_vertical_inset;
            const int meter_height = slider.getHeight() - g_calibrate_button_height -
                                     g_panel_inset - (g_gain_meter_vertical_inset * 2);
            layout.sliderBounds.setY(meter_top + thumb_radius);
            layout.sliderBounds.setHeight(std::max(1, meter_height - (thumb_radius * 2)));

            const int text_box_y = slider.getHeight() - g_calibrate_button_height +
                                   ((g_calibrate_button_height - g_gain_value_height) / 2);
            layout.textBoxBounds.setY(std::max(0, text_box_y));
        }

        return layout;
    }
};

} // namespace

// Paints the signal rail behind fixed block placeholders and plugin tiles.
class SignalChainView::SignalPathContent final : public juce::Component
{
public:
    // Stores the current fixed block count so empty positions stay visible on the path.
    void setBlockCount(std::size_t block_count)
    {
        if (m_block_count == block_count)
        {
            return;
        }

        m_block_count = block_count;
        repaint();
    }

    // Draws the dark path surface, full-width signal line, and subtle fixed-position markers.
    void paint(juce::Graphics& g) override
    {
        const auto bounds = getLocalBounds();
        g.fillAll(g_path_background);

        const auto path_area = signalPathArea(bounds);
        const std::size_t block_count = m_block_count;
        if (path_area.isEmpty() || block_count == 0)
        {
            return;
        }

        const int path_y = path_area.getCentreY();
        g.setColour(g_signal_path_line);
        // The line runs in segments between the fixed cell centres, breaking at each one so the
        // slot affordances (the "+" insert button and the marker dot) sit on the dark surface
        // rather than under the bright line.
        auto segment_start = static_cast<float>(bounds.getX());
        const auto draw_segment = [&g, path_y](float from_x, float to_x) {
            if (to_x > from_x)
            {
                g.drawLine(
                    from_x, static_cast<float>(path_y), to_x, static_cast<float>(path_y), 3.0f);
            }
        };
        for (std::size_t index = 0; index < block_count; ++index)
        {
            const float centre_x =
                static_cast<float>(blockCellBounds(path_area, index, block_count).getCentreX());
            const float half_gap = static_cast<float>(g_signal_path_node_gap) / 2.0f;
            draw_segment(segment_start, centre_x - half_gap);
            segment_start = std::max(segment_start, centre_x + half_gap);
        }
        draw_segment(segment_start, static_cast<float>(bounds.getRight()));

        g.setColour(g_signal_path_slot_marker);
        for (std::size_t index = 0; index < block_count; ++index)
        {
            const auto marker_bounds =
                blockCellBounds(path_area, index, block_count).withSizeKeepingCentre(8, 8);
            g.fillEllipse(marker_bounds.toFloat());
        }
    }

private:
    // Number of fixed visual blocks the path renderer reserves.
    std::size_t m_block_count{};
};

namespace
{

// Configures a vertical gain slider with the shared gain range and dB suffix.
void configureGainSlider(juce::Slider& slider, const juce::String& component_id)
{
    slider.setComponentID(component_id);
    slider.setSliderStyle(juce::Slider::LinearVertical);
    slider.setRange(common::audio::minimumGainDb(), common::audio::maximumGainDb(), 0.1);
    slider.setValue(common::audio::defaultGainDb(), juce::dontSendNotification);
    slider.setDoubleClickReturnValue(true, common::audio::defaultGainDb());
    slider.setTextBoxStyle(
        juce::Slider::TextBoxBelow, false, g_output_gain_width, g_gain_value_height);
    slider.setTextValueSuffix(" dB");
}

} // namespace

// Creates the signal-chain controls and routes user intents through the owner.
SignalChainView::SignalChainView(Listener& listener)
    : m_listener(listener)
    , m_input_meter(AudioLevelMeterOrientation::Vertical)
    , m_output_gain_slider_look_and_feel(std::make_unique<OutputGainSliderLookAndFeel>())
    , m_output_meter(AudioLevelMeterOrientation::Vertical)
    , m_chain_content(std::make_unique<SignalPathContent>())
    , m_block_layout(g_signal_path_min_block_count)
{
    setComponentID("signal_chain_view");

    m_input_meter.setComponentID("input_meter");
    addAndMakeVisible(m_input_meter);
    m_input_calibrate_button.setComponentID("input_calibrate_button");
    m_input_calibrate_button.setButtonText("Calibrate");
    m_input_calibrate_button.setWantsKeyboardFocus(false);
    m_input_calibrate_button.setMouseClickGrabsKeyboardFocus(false);
    m_input_calibrate_button.onClick = [this] { m_listener.onInputCalibrationPressed(); };
    addAndMakeVisible(m_input_calibrate_button);

    // The designer file strip stays hidden until setToneDesignerState reports the designer
    // active, so project mode keeps its plain header (addChildComponent, not addAndMakeVisible).
    const auto configure_tone_button = [this](
                                           juce::TextButton& button,
                                           const char* component_id,
                                           const char* text,
                                           std::function<void()>
                                               on_click) {
        button.setComponentID(component_id);
        button.setButtonText(text);
        button.setWantsKeyboardFocus(false);
        button.setMouseClickGrabsKeyboardFocus(false);
        button.onClick = std::move(on_click);
        addChildComponent(button);
    };
    configure_tone_button(
        m_tone_new_button, "tone_new_button", "New", [this] { m_listener.onNewTonePressed(); });
    configure_tone_button(m_tone_open_button, "tone_open_button", "Open...", [this] {
        m_listener.onOpenToneFilePressed();
    });
    configure_tone_button(
        m_tone_save_button, "tone_save_button", "Save", [this] { m_listener.onSaveTonePressed(); });
    configure_tone_button(m_tone_save_as_button, "tone_save_as_button", "Save As...", [this] {
        m_listener.onSaveToneAsPressed();
    });
    configure_tone_button(m_tone_import_button, "tone_import_button", "Import Tone...", [this] {
        m_listener.onImportTonePressed();
    });
    configure_tone_button(m_tone_export_button, "tone_export_button", "Export Tone...", [this] {
        m_listener.onExportTonePressed();
    });

    configureGainSlider(m_output_gain_slider, "output_gain_slider");
    m_output_gain_slider.setLookAndFeel(m_output_gain_slider_look_and_feel.get());
    m_output_gain_slider.onDragStart = [this] { m_output_gain_dragging = true; };
    m_output_gain_slider.onValueChange = [this] {
        const double gain_db = m_output_gain_slider.getValue();
        if (m_output_gain_dragging)
        {
            m_listener.onOutputGainPreviewChanged(gain_db);
            return;
        }

        m_listener.onOutputGainChanged(gain_db);
    };
    m_output_gain_slider.onDragEnd = [this] {
        m_output_gain_dragging = false;
        m_listener.onOutputGainChanged(m_output_gain_slider.getValue());
    };
    addAndMakeVisible(m_output_gain_slider);
    m_output_meter.setComponentID("output_gain_meter");
    // The meter is visually inside the slider component's textbox-width footprint. Let it
    // consume pointer hits so meter clicks do not pass through as slider adjustments.
    m_output_meter.setInterceptsMouseClicks(true, false);
    addAndMakeVisible(m_output_meter);

    m_chain_viewport.setComponentID("signal_chain_viewport");
    m_chain_content->setComponentID("signal_chain_content");
    m_chain_viewport.setViewedComponent(m_chain_content.get(), false);
    m_chain_viewport.setScrollBarsShown(false, true);
    addAndMakeVisible(m_chain_viewport);

    setState(core::SignalChainViewState{});
}

// Detaches the custom slider look-and-feel before owned children are destroyed.
SignalChainView::~SignalChainView()
{
    auto& animator = juce::Desktop::getInstance().getAnimator();
    for (const std::unique_ptr<PluginTileView>& tile : m_plugin_tiles)
    {
        if (tile != nullptr)
        {
            animator.cancelAnimation(tile.get(), false);
        }
    }

    m_chain_viewport.setViewedComponent(nullptr, false);
    m_output_gain_slider.setLookAndFeel(nullptr);
}

// Stores the render state and updates controls whose enabledness is derived outside the view.
// Repaints the header title with the selected tone's name appended.
void SignalChainView::setToneName(std::string tone_name)
{
    if (m_tone_name == tone_name)
    {
        return;
    }
    m_tone_name = std::move(tone_name);
    repaint();
}

// Applies the Tone Designer document state: header title, dirty marker, and the file strip.
void SignalChainView::setToneDesignerState(const core::ToneDesignerViewState& state)
{
    if (m_tone_designer == state)
    {
        return;
    }

    m_tone_designer = state;
    m_tone_new_button.setVisible(state.active);
    m_tone_open_button.setVisible(state.active);
    m_tone_save_button.setVisible(state.active);
    m_tone_save_as_button.setVisible(state.active);
    resized();
    repaint();
}

void SignalChainView::setState(const core::SignalChainViewState& state)
{
    m_block_layout.applyPlugins(state.plugins);
    m_state = state;
    // Project-mode tone-file commands follow their availability flags; the designer strip owns
    // the header in designer mode and these flags are false there.
    m_tone_import_button.setVisible(m_state.tone_import_enabled);
    m_tone_export_button.setVisible(m_state.tone_export_enabled);
    m_input_calibrate_button.setEnabled(m_state.input_calibrate_enabled);
    m_output_gain_slider.setEnabled(m_state.output_gain_controls_enabled);
    m_output_gain_slider.setValue(m_state.output_gain_db, juce::dontSendNotification);
    m_chain_viewport.setVisible(m_state.disabled_message.empty());
    m_chain_content->setBlockCount(m_block_layout.blockCount());
    rebuildPluginTiles();
    resized();
    repaint();
}

// Applies the live-rig meter values without rebuilding plugin tiles or changing controls.
void SignalChainView::setMeterLevels(
    common::audio::AudioMeterLevel input_level, common::audio::AudioMeterLevel output_level)
{
    m_input_meter.setLevel(input_level);
    m_output_meter.setLevel(output_level);
}

// Draws a compact plugin-chain view with gain labels and an empty-chain placeholder.
void SignalChainView::paint(juce::Graphics& g)
{
    const auto bounds = getLocalBounds();
    g.fillAll(editorTheme().panel_background);
    g.setColour(g_panel_border);
    g.drawRect(bounds);

    auto area = bounds.reduced(g_panel_inset);

    // Input label above the left meter.
    const auto input_label_area =
        area.removeFromLeft(g_input_control_width).removeFromTop(g_header_height);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions{12.0f});
    g.drawFittedText("Input", input_label_area, juce::Justification::centred, 1);

    // Output gain label above the right slider and post-fader meter group.
    const auto output_label_area =
        area.removeFromRight(g_output_gain_width).removeFromTop(g_header_height);
    g.drawFittedText("Output", output_label_area, juce::Justification::centred, 1);

    // Center header with title.
    area.removeFromLeft(g_panel_inset);
    area.removeFromRight(g_panel_inset);
    auto header = area.removeFromTop(g_header_height);

    g.setColour(editorTheme().panel_header);
    g.fillRect(header);
    g.setColour(juce::Colours::white);
    g.setFont(juce::FontOptions{16.0f, juce::Font::bold});
    // The designer header names the file-backed tone document (with its dirty marker) instead of
    // a project catalog tone; the file strip occupies the header's right side while active.
    const juce::String header_title =
        m_tone_designer.active
            ? juce::String{"Tone Designer - "} + juce::String{m_tone_designer.document_name} +
                  juce::String{m_tone_designer.dirty ? "*" : ""}
            : (m_tone_name.empty() ? juce::String{"Signal Chain"}
                                   : juce::String{"Signal Chain - "} + juce::String{m_tone_name});
    g.drawFittedText(header_title, header.reduced(8, 0), juce::Justification::centredLeft, 1);

    area.removeFromTop(g_panel_inset);
    const auto chain_area = area;
    if (!m_state.disabled_message.empty())
    {
        g.setColour(juce::Colours::lightgrey);
        g.setFont(juce::FontOptions{14.0f});
        g.drawFittedText(m_state.disabled_message, area, juce::Justification::centredLeft, 2);
        return;
    }

    if (m_state.plugins.empty())
    {
        g.setColour(juce::Colours::lightgrey);
        g.setFont(juce::FontOptions{14.0f});
        g.drawFittedText("No plugins loaded", chain_area, juce::Justification::centred, 1);
        return;
    }
}

// Keeps gain sliders on the sides and plugin tiles in the center.
void SignalChainView::resized()
{
    auto area = getLocalBounds().reduced(g_panel_inset);

    // Input meter on the left, with calibration command anchored below it.
    auto input_control_area = area.removeFromLeft(g_input_control_width);
    input_control_area.removeFromTop(g_header_height);
    auto calibrate_area = input_control_area.removeFromBottom(
        std::min(g_calibrate_button_height, input_control_area.getHeight()));
    input_control_area.removeFromBottom(std::min(g_panel_inset, input_control_area.getHeight()));
    auto input_meter_area = input_control_area.withSizeKeepingCentre(
        g_gain_meter_width, input_control_area.getHeight());
    m_input_meter.setBounds(input_meter_area.reduced(0, g_gain_meter_vertical_inset));
    m_input_calibrate_button.setBounds(calibrate_area);

    // Output gain flows into its post-fader meter, with one centered readout below the pair.
    auto output_control_area = area.removeFromRight(g_output_gain_width);
    output_control_area.removeFromTop(g_header_height);
    auto output_slider_area = output_control_area.withSizeKeepingCentre(
        g_output_gain_width, output_control_area.getHeight());
    auto output_meter_area = output_slider_area.withTrimmedBottom(
        std::min(g_calibrate_button_height + g_panel_inset, output_slider_area.getHeight()));
    output_meter_area.setX(
        output_slider_area.getX() + ((g_output_gain_width - g_output_gain_visual_width) / 2) +
        g_gain_slider_width + g_gain_meter_gap);
    output_meter_area.setWidth(g_gain_meter_width);
    m_output_gain_slider.setBounds(output_slider_area);
    m_output_meter.setBounds(output_meter_area.reduced(0, g_gain_meter_vertical_inset));

    // Leave a gap between the sliders and the center content.
    area.removeFromLeft(g_panel_inset);
    area.removeFromRight(g_panel_inset);

    // The designer file strip sits right-aligned in the same header band paint() titles.
    auto header = area.removeFromTop(g_header_height);
    auto strip = header.reduced(0, 3);
    constexpr int tone_button_gap = 6;
    const auto place_tone_button = [&strip](juce::TextButton& button, int width) {
        button.setBounds(strip.removeFromRight(width));
        strip.removeFromRight(tone_button_gap);
    };
    place_tone_button(m_tone_save_as_button, 78);
    place_tone_button(m_tone_save_button, 56);
    place_tone_button(m_tone_open_button, 68);
    place_tone_button(m_tone_new_button, 52);

    // Project-mode commands reuse the same right edge; the two sets are never visible together.
    auto project_strip = header.reduced(0, 3);
    const auto place_project_button = [&project_strip](juce::TextButton& button, int width) {
        button.setBounds(project_strip.removeFromRight(width));
        project_strip.removeFromRight(tone_button_gap);
    };
    place_project_button(m_tone_export_button, 100);
    place_project_button(m_tone_import_button, 100);

    area.removeFromTop(g_panel_inset);
    m_chain_viewport.setBounds(area);
    const int content_height = std::max(0, m_chain_viewport.getMaximumVisibleHeight());
    const int content_width = chainContentWidth(m_block_layout.blockCount(), area.getWidth());
    m_chain_content->setSize(content_width, content_height);
    layoutSignalPathContent(TileLayoutMotion::Immediate);
}

// Positions placeholders and moves plugin tiles using either immediate or animated bounds.
void SignalChainView::layoutSignalPathContent(TileLayoutMotion motion)
{
    const auto path_area = signalPathArea(m_chain_content->getLocalBounds());
    const std::size_t block_count = m_block_layout.blockCount();
    const SignalChainBlockPlacement& active_placement = m_block_layout.activePlacement();
    auto& animator = juce::Desktop::getInstance().getAnimator();
    const bool has_free_block = m_state.plugins.size() < block_count;

    for (std::size_t index = 0; index < m_insert_slots.size(); ++index)
    {
        const std::unique_ptr<InsertSlotView>& slot = m_insert_slots[index];
        if (slot == nullptr)
        {
            continue;
        }

        slot->setVisible(true);
        const bool is_empty = !active_placement.pluginAtBlock(index).has_value();
        slot->setEditingEnabled(
            is_empty,
            m_state.insert_plugin_enabled && has_free_block && is_empty,
            m_state.move_plugins_enabled);
        slot->setBounds(blockCellBounds(path_area, index, block_count));
    }

    for (std::size_t index = 0; index < m_plugin_tiles.size(); ++index)
    {
        const std::unique_ptr<PluginTileView>& tile = m_plugin_tiles[index];
        if (tile == nullptr)
        {
            continue;
        }

        tile->setVisible(true);
        std::size_t block_index = index;
        if (const std::optional<std::size_t> placed_block = active_placement.blockForPlugin(index);
            placed_block.has_value() && *placed_block < block_count)
        {
            block_index = *placed_block;
        }

        const juce::Rectangle<int> target_bounds =
            pluginBlockBounds(path_area, block_index, block_count);
        if (motion == TileLayoutMotion::Animated)
        {
            if (animator.getComponentDestination(tile.get()) != target_bounds)
            {
                animator.animateComponent(
                    tile.get(),
                    target_bounds,
                    1.0f,
                    g_signal_preview_animation_ms,
                    false,
                    g_signal_preview_animation_start_speed,
                    g_signal_preview_animation_end_speed);
            }
        }
        else
        {
            animator.cancelAnimation(tile.get(), false);
            tile->setAlpha(1.0f);
            tile->setBounds(target_bounds);
        }
    }
}

// Converts an empty fixed block location into the matching linear insertion index.
void SignalChainView::insertPluginAtBlockLocation(std::size_t block_index)
{
    if (!m_state.insert_plugin_enabled)
    {
        return;
    }

    std::optional<std::size_t> chain_index = m_block_layout.insertionIndexForBlock(block_index);
    if (chain_index.has_value())
    {
        m_listener.onInsertPluginPressed(*chain_index, block_index);
    }
}

// Converts a tile drop on a fixed block location into the existing move-plugin intent.
void SignalChainView::movePluginToBlockLocation(
    std::string instance_id, std::size_t source_index, std::size_t destination_index)
{
    if (!m_state.move_plugins_enabled)
    {
        return;
    }

    if (source_index >= m_state.plugins.size() || destination_index >= m_state.plugins.size() ||
        source_index == destination_index)
    {
        return;
    }

    m_listener.onMovePluginPressed(
        std::move(instance_id), destination_index, pluginBlockAssignments());
}

// Centralizes drop finalization so every target preserves preview and no-op placement behavior.
void SignalChainView::completePluginDrop(
    const juce::var& drag_description, std::optional<SignalChainBlockLayout::DropIntent> intent)
{
    std::optional<DraggedPlugin> plugin = parsePluginDragDescription(drag_description);
    if (!plugin.has_value())
    {
        clearPluginMovePreview();
        return;
    }

    const SignalChainBlockLayout::DropCompletion completion =
        m_block_layout.completeDrop(plugin->source_index, std::move(intent));
    if (completion.layout_changed)
    {
        layoutSignalPathContent(TileLayoutMotion::Animated);
        m_chain_content->repaint();
    }

    if (completion.move_destination_index.has_value())
    {
        // The placement is passed with the move intent, keyed by pre-move instance IDs, so core
        // can apply it after the backend returns the reordered chain snapshot.
        movePluginToBlockLocation(
            std::move(plugin->instance_id),
            plugin->source_index,
            *completion.move_destination_index);
    }
    else if (completion.layout_changed)
    {
        // An order-preserving drop (e.g. opening a gap) changes only the visual placement and
        // never round-trips through the controller, so report it directly or it would not persist.
        reportSignalChainPlacement();
    }
}

// Hands the current block placement to the controller so it is written on the next capture.
void SignalChainView::reportSignalChainPlacement()
{
    m_listener.onSignalChainPlacementChanged(pluginBlockAssignments());
}

// Converts the cached placement into the instance-keyed boundary shape expected by editor core.
std::vector<core::PluginBlockAssignment> SignalChainView::pluginBlockAssignments() const
{
    std::vector<core::PluginBlockAssignment> assignments;
    const SignalChainBlockPlacement& placement = m_block_layout.cachedPlacement();
    if (placement.pluginCount() != m_state.plugins.size())
    {
        return assignments;
    }

    assignments.reserve(m_state.plugins.size());
    const std::vector<std::size_t>& blocks = placement.blocks();
    for (std::size_t index = 0; index < m_state.plugins.size(); ++index)
    {
        assignments.push_back(
            core::PluginBlockAssignment{
                .instance_id = m_state.plugins[index].instance_id,
                .block_index = blocks[index],
            });
    }

    return assignments;
}

// Stores a drag-hover preview and relayouts the chain if the preview target changed.
void SignalChainView::previewPluginMove(
    std::size_t source_index, SignalChainBlockLayout::DropIntent intent)
{
    if (!m_block_layout.previewMove(source_index, std::move(intent)))
    {
        return;
    }

    layoutSignalPathContent(TileLayoutMotion::Animated);
    m_chain_content->repaint();
}

// Removes any drag-hover preview and restores the cached controller layout.
void SignalChainView::clearPluginMovePreview()
{
    if (!m_block_layout.clearPreview())
    {
        return;
    }

    layoutSignalPathContent(TileLayoutMotion::Animated);
    m_chain_content->repaint();
}

// Lets JUCE deliver the target drop callback before source mouse-up can clear the preview.
void SignalChainView::clearPluginMovePreviewAsync()
{
    const juce::Component::SafePointer<SignalChainView> safe_this{this};
    (void)juce::MessageManager::callAsync([safe_this] {
        if (safe_this != nullptr)
        {
            safe_this->clearPluginMovePreview();
        }
    });
}

// Recreates child tiles from the latest controller state so each control carries a stable ID.
void SignalChainView::rebuildPluginTiles()
{
    for (const std::unique_ptr<InsertSlotView>& slot : m_insert_slots)
    {
        if (slot != nullptr)
        {
            m_chain_content->removeChildComponent(slot.get());
        }
    }

    for (const std::unique_ptr<PluginTileView>& tile : m_plugin_tiles)
    {
        if (tile != nullptr)
        {
            juce::Desktop::getInstance().getAnimator().cancelAnimation(tile.get(), false);
            m_chain_content->removeChildComponent(tile.get());
        }
    }

    m_insert_slots.clear();
    m_plugin_tiles.clear();
    if (!m_state.disabled_message.empty())
    {
        return;
    }

    m_plugin_tiles.reserve(m_state.plugins.size());
    for (const core::PluginViewState& plugin : m_state.plugins)
    {
        auto tile = std::make_unique<PluginTileView>(plugin, *this, m_listener);
        tile->setEditEnabled(m_state.move_plugins_enabled, m_state.remove_plugins_enabled);
        m_chain_content->addAndMakeVisible(*tile);
        m_plugin_tiles.push_back(std::move(tile));
    }

    const std::size_t block_count = m_block_layout.blockCount();
    const SignalChainBlockPlacement& cached_placement = m_block_layout.cachedPlacement();
    m_insert_slots.reserve(block_count);
    const bool has_free_block = m_state.plugins.size() < block_count;
    for (std::size_t index = 0; index < block_count; ++index)
    {
        auto slot = std::make_unique<InsertSlotView>(index, *this);
        const bool is_empty = !cached_placement.pluginAtBlock(index).has_value();
        slot->setEditingEnabled(
            is_empty,
            m_state.insert_plugin_enabled && has_free_block && is_empty,
            m_state.move_plugins_enabled);
        m_chain_content->addAndMakeVisible(*slot);
        m_insert_slots.push_back(std::move(slot));
    }
}

} // namespace rock_hero::editor::ui
