#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>

#include "UI/Runtime/UiContext.h"

namespace ui
{

struct UiActionDispatchResult
{
    std::string elementId{};
    std::string action{};
    std::string payload{};
    bool handled = false;
};

class UiActionDispatcher
{
public:
    using Handler = std::function<void(const UiActionEvent&)>;

    void registerHandler(std::string action, Handler handler);
    bool hasHandler(std::string_view action) const;
    UiActionDispatchResult dispatch(const UiActionEvent& event) const;
    void clear();

private:
    std::unordered_map<std::string, Handler> handlers_{};
};

} // namespace ui
