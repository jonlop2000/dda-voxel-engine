#pragma once

#include <cstdint>

namespace ui
{

// a theme color role stores the hue only. alpha is an emphasis decision made at
// the use site through themeColor(), because packColor() premultiplies the rgb
// channels by alpha — a stored packed color cannot have its alpha swapped later.
struct UiThemeColorRole
{
    float r = 1.0f;
    float g = 1.0f;
    float b = 1.0f;
};

// runtime ui theme tokens. default values reproduce the legacy
// hardcoded overlay-smoke palette byte-for-byte, so the existing runtime ui
// smokes (including pixel validation) double as the regression net for theme
// refactors. animation timings are declared here for the upcoming tween slice.
struct UiTheme
{
    // --- color roles ---
    UiThemeColorRole panelInk{0.015f, 0.020f, 0.026f};       // panel backgrounds
    UiThemeColorRole accent{0.04f, 0.78f, 0.88f};            // primary accent (cyan)
    UiThemeColorRole accentAlt{0.96f, 0.12f, 0.42f};         // secondary accent (magenta)
    UiThemeColorRole foreground{0.92f, 0.96f, 0.88f};        // text / rules (cream)
    UiThemeColorRole info{0.18f, 0.44f, 0.56f};              // informational (teal)
    UiThemeColorRole infoHover{0.22f, 0.54f, 0.68f};
    UiThemeColorRole positive{0.66f, 0.82f, 0.30f};          // positive/quality (lime)
    UiThemeColorRole positiveHover{0.74f, 0.90f, 0.38f};
    UiThemeColorRole neutral{0.30f, 0.38f, 0.48f};           // neutral rows (slate)
    UiThemeColorRole neutralHover{0.38f, 0.48f, 0.60f};
    UiThemeColorRole buttonInk{0.08f, 0.11f, 0.15f};         // default button states
    UiThemeColorRole buttonHoverInk{0.14f, 0.24f, 0.30f};
    UiThemeColorRole buttonDisabledInk{0.07f, 0.08f, 0.10f};
    UiThemeColorRole textDisabled{0.48f, 0.52f, 0.50f};

    // --- typography (text pixel heights) ---
    float titleTextHeight = 28.0f;
    float headingTextHeight = 20.0f;
    float bodyTextHeight = 16.0f;
    float captionTextHeight = 14.0f;

    // --- animation timings (seconds; consumed by the tween system) ---
    float fadeInSeconds = 0.18f;
    float fadeOutSeconds = 0.12f;
    float slideSeconds = 0.22f;
};

uint32_t themeColor(const UiThemeColorRole& role, float alpha);
const UiTheme& defaultUiTheme();

} // namespace ui
