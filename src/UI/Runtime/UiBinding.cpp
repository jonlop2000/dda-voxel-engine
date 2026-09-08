#include "UI/Runtime/UiBinding.h"

#include <utility>

namespace ui
{

void UiBindingSet::bind(std::string name, const UiDataVersion* source, ApplyFn apply)
{
    Binding binding{};
    binding.name = std::move(name);
    binding.source = source;
    binding.apply = std::move(apply);
    bindings_.push_back(std::move(binding));
}

UiBindingStats UiBindingSet::sync(UiTree& tree, uint64_t treeRevision)
{
    const bool treeRebuilt = treeRevision != lastTreeRevision_;
    lastTreeRevision_ = treeRevision;

    UiBindingStats stats{};
    stats.total = bindings_.size();
    for (Binding& binding : bindings_)
    {
        if (binding.source == nullptr || !binding.apply)
        {
            continue;
        }

        const uint64_t sourceVersion = binding.source->value();
        if (!treeRebuilt && binding.appliedVersion == sourceVersion)
        {
            continue;
        }

        binding.apply(tree);
        binding.appliedVersion = sourceVersion;
        ++stats.applied;
    }

    return stats;
}

} // namespace ui
