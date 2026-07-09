#include "main_window/undo_history_overlay.h"

#include <algorithm>
#include <cstddef>

namespace rock_hero::editor::ui
{

UndoHistoryOverlay::UndoHistoryOverlay()
{
    setInterceptsMouseClicks(false, false);
}

void UndoHistoryOverlay::setHistory(const core::UndoHistoryState& history)
{
    if (m_history == history)
    {
        return;
    }
    m_history = history;
    repaint();
}

void UndoHistoryOverlay::paint(juce::Graphics& graphics)
{
    const juce::Rectangle<float> panel = getLocalBounds().toFloat();
    graphics.setColour(juce::Colours::black.withAlpha(0.82f));
    graphics.fillRoundedRectangle(panel, 5.0f);
    graphics.setColour(juce::Colours::white.withAlpha(0.18f));
    graphics.drawRoundedRectangle(panel.reduced(0.5f), 5.0f, 1.0f);

    const int total = static_cast<int>(m_history.labels.size());
    const int undo_depth = static_cast<int>(m_history.position);
    const int redo_depth = total - undo_depth;

    juce::Rectangle<int> area = getLocalBounds().reduced(10, 8);

    graphics.setColour(juce::Colours::white);
    graphics.setFont(juce::Font{juce::FontOptions{13.0f}}.boldened());
    graphics.drawText("Undo History", area.removeFromTop(18), juce::Justification::centredLeft);

    graphics.setColour(juce::Colours::white.withAlpha(0.65f));
    graphics.setFont(juce::Font{juce::FontOptions{11.0f}});
    graphics.drawText(
        juce::String(total) + " entries  (undo " + juce::String(undo_depth) + " / redo " +
            juce::String(redo_depth) + ")",
        area.removeFromTop(15),
        juce::Justification::centredLeft);
    area.removeFromTop(4);

    graphics.setFont(juce::Font{juce::FontOptions{12.0f}});
    constexpr int row_height = 16;
    const int max_rows = std::max(1, area.getHeight() / row_height);

    // Newest first, so a freshly pushed entry appears at the top while the user watches. The cursor
    // sits at undo_depth: entries at or above it are redoable (dimmed), the entry just below it is
    // the next undo (highlighted), and the clean marker names the saved revision.
    int drawn = 0;
    for (int index = total - 1; index >= 0 && drawn < max_rows; --index, ++drawn)
    {
        const bool redoable = index >= undo_depth;
        const bool next_undo = index == undo_depth - 1;
        const bool saved_here = m_history.clean_position.has_value() &&
                                static_cast<int>(*m_history.clean_position) == index + 1;

        juce::Colour colour =
            redoable ? juce::Colours::white.withAlpha(0.4f) : juce::Colours::white.withAlpha(0.9f);
        if (next_undo)
        {
            colour = juce::Colours::yellow;
        }
        graphics.setColour(colour);

        juce::String text = (next_undo ? juce::String{"> "} : juce::String{"   "}) +
                            juce::String{m_history.labels[static_cast<std::size_t>(index)]};
        if (saved_here)
        {
            text += "   (saved)";
        }
        graphics.drawText(text, area.removeFromTop(row_height), juce::Justification::centredLeft);
    }

    if (total > max_rows)
    {
        graphics.setColour(juce::Colours::white.withAlpha(0.4f));
        graphics.drawText(
            "+ " + juce::String(total - max_rows) + " older",
            area.removeFromTop(row_height),
            juce::Justification::centredLeft);
    }
}

} // namespace rock_hero::editor::ui
