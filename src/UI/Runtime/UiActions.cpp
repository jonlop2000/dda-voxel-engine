#include "UI/Runtime/UiActions.h"

#include <utility>

namespace ui
{

void UiActionDispatcher::registerHandler(std::string action, Handler handler)
{
    if (action.empty() || handler == nullptr)
    {
        return;
    }

    handlers_[std::move(action)] = std::move(handler);
}

bool UiActionDispatcher::hasHandler(std::string_view action) const
{
    return handlers_.find(std::string(action)) != handlers_.end();
}

UiActionDispatchResult UiActionDispatcher::dispatch(const UiActionEvent& event) const
{
    UiActionDispatchResult result{};
    result.elementId = event.elementId;
    result.action = event.action;
    result.payload = event.payload;

    if (event.action.empty())
    {
        return result;
    }

    const auto it = handlers_.find(event.action);
    if (it == handlers_.end())
    {
        return result;
    }

    it->second(event);
    result.handled = true;
    return result;
}

void UiActionDispatcher::clear()
{
    handlers_.clear();
}

} // namespace ui
