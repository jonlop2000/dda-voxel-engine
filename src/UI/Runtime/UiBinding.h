#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "UI/Runtime/UiElement.h"

namespace ui
{

// a version-counter data source (with bindings). Game/controller code bumps
// the version whenever the underlying value changes; bindings re-apply only when the
// version they last applied is out of date.
class UiDataVersion
{
public:
    uint64_t value() const { return value_; }
    void bump() { ++value_; }

private:
    uint64_t value_ = 1;
};

struct UiBindingStats
{
    size_t total = 0;
    size_t applied = 0;
};

class UiBindingSet
{
public:
    using ApplyFn = std::function<void(UiTree&)>;

    void bind(std::string name, const UiDataVersion* source, ApplyFn apply);

    // applies bindings whose source version changed since their last apply. a
    // changed treeRevision (e.g. the retained tree was rebuilt) re-applies all
    // bindings, because a fresh tree starts from authored defaults.
    UiBindingStats sync(UiTree& tree, uint64_t treeRevision = 0);

    size_t bindingCount() const { return bindings_.size(); }

private:
    struct Binding
    {
        std::string name{};
        const UiDataVersion* source = nullptr;
        ApplyFn apply{};
        uint64_t appliedVersion = 0;
    };

    std::vector<Binding> bindings_{};
    uint64_t lastTreeRevision_ = ~0ull;
};

} // namespace ui
