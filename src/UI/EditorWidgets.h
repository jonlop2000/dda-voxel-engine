#pragma once

#include <imgui.h>

namespace EditorWidgets
{

// section header with cyan accent color
void SectionHeader(const char* label);

// collapsible section header (returns true if open)
bool SectionHeaderCollapsible(const char* label, bool defaultOpen = true);

// slider with reset button (returns true if value changed)
bool SliderWithReset(const char* label, float* value, float min, float max, float resetValue,
                     const char* format = "%.2f");

// integer slider with reset button
bool SliderIntWithReset(const char* label, int* value, int min, int max, int resetValue);

// display a label: value pair with cyan accent on the value
void ValueLabel(const char* label, const char* format, ...);

// display a warning value (orange/red) if above threshold
void ValueLabelWarning(const char* label, float value, float warningThreshold,
                       const char* format = "%.2f");

// help marker (?) with tooltip
void HelpMarker(const char* description);

// colored text helpers
void TextCyan(const char* text);
void TextWarning(const char* text);
void TextError(const char* text);
void TextSuccess(const char* text);

// compact button (smaller padding)
bool CompactButton(const char* label);

// toggle button that changes color when active
bool ToggleButton(const char* label, bool* active);

// horizontal separator with optional label
void Separator();
void SeparatorWithLabel(const char* label);

// vertical spacing helper
void Spacing(int lines = 1);

// Begin/End a styled group box
void BeginGroupBox(const char* label);
void EndGroupBox();

// keyboard shortcut hint (displayed in dimmed text)
void KeyboardHint(const char* hint);

// property row: label on left, widget on right (call before widget)
void PropertyLabel(const char* label, float labelWidth = 120.0f);

} // namespace EditorWidgets
