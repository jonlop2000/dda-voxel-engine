#include "UI/Runtime/UiTheme.h"

#include "UI/Runtime/UiTypes.h"

namespace ui
{

uint32_t themeColor(const UiThemeColorRole& role, float alpha)
{
    return packColor(role.r, role.g, role.b, alpha);
}

const UiTheme& defaultUiTheme()
{
    static const UiTheme theme{};
    return theme;
}

} // namespace ui
