#pragma once

#include <functional>
#include <string_view>
#include <utility>
#include <vector>

#include "engine/render/IRenderPass.h"

namespace engine::render
{

class PassRegistry
{
public:
    using RecordFn = std::function<void(const RenderPassContext&)>;
    using EnabledFn = std::function<bool(const FrameInputs&)>;

    void clear() { entries_.clear(); }

    void add(IRenderPass* pass)
    {
        entries_.push_back(Entry{
            pass->name(),
            pass,
            [pass](const RenderPassContext& context) { pass->record(context); },
            [pass](const FrameInputs& inputs) { return pass->enabled(inputs); },
        });
    }

    void add(std::string_view name, RecordFn record, EnabledFn enabled = {})
    {
        entries_.push_back(Entry{name, nullptr, std::move(record), std::move(enabled)});
    }

    void createAll(VulkanContext& ctx, const PassCreateInfo& createInfo) const
    {
        for (const Entry& entry : entries_)
        {
            if (entry.pass != nullptr)
            {
                entry.pass->create(ctx, createInfo);
            }
        }
    }

    void onResizeAll(VulkanContext& ctx, const PassCreateInfo& createInfo) const
    {
        for (const Entry& entry : entries_)
        {
            if (entry.pass != nullptr)
            {
                entry.pass->onResize(ctx, createInfo);
            }
        }
    }

    void recordAll(const RenderPassContext& context) const
    {
        for (const Entry& entry : entries_)
        {
            if (!entry.enabled || entry.enabled(context.inputs))
            {
                entry.record(context);
            }
        }
    }

    void destroyAll(VulkanContext& ctx) const
    {
        for (const Entry& entry : entries_)
        {
            if (entry.pass != nullptr)
            {
                entry.pass->destroy(ctx);
            }
        }
    }

    [[nodiscard]] std::vector<std::string_view> names() const
    {
        std::vector<std::string_view> result;
        result.reserve(entries_.size());
        for (const Entry& entry : entries_)
        {
            result.push_back(entry.name);
        }
        return result;
    }

    [[nodiscard]] std::vector<std::string_view> enabledNames(const FrameInputs& inputs) const
    {
        std::vector<std::string_view> result;
        result.reserve(entries_.size());
        for (const Entry& entry : entries_)
        {
            if (!entry.enabled || entry.enabled(inputs))
            {
                result.push_back(entry.name);
            }
        }
        return result;
    }

    [[nodiscard]] size_t size() const { return entries_.size(); }

private:
    struct Entry
    {
        std::string_view name;
        IRenderPass* pass = nullptr;
        RecordFn record;
        EnabledFn enabled;
    };

    std::vector<Entry> entries_;
};

}  // namespace engine::render
